// 验收回放工具：把语料帧序列喂给长截图拼接、把渲染图（或真值行）喂给表格检测与阅读顺序导出，
// 并把 4K 帧的编码与环形缓冲读数打到 stdout。与 UiShotTool / UiAuditTool 并列，
// 不进安装包、不挂产品入口：它服务 A 类验收，产物只进临时目录。

#include "Core/Capture/FrameStitch.h"
#include "Core/Content/CsvWriter.h"
#include "Core/Content/MarkdownBuilder.h"
#include "Core/Content/TableDetector.h"
#include "Core/Ocr/IOcrEngine.h"
#include "Core/Ocr/OcrLanguage.h"
#include "Core/Ocr/OcrTypes.h"
#include "Core/Rewind/FrameRingBuffer.h"
#include "Core/Rewind/RewindConfig.h"
#include "Core/Rewind/RewindFrameCodec.h"
#include "Core/Rewind/RewindTypes.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"
#include "Platform/IPlatformIntegration.h"
#include "Plugins/OcrEngines/OcrEngineFactory.h"

#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace
{
    /// 读一张图；读不到就报错返回空图
    QImage loadImage(const QString &path)
    {
        QImage image{path};
        if (image.isNull())
        {
            std::fprintf(stderr, "cannot read image: %s\n", path.toStdString().c_str());
        }
        return image;
    }

    /// 建一个能认字的引擎；不可用就打印原因并返回空
    std::unique_ptr<GyanisScribe::Core::IOcrEngine> makeEngine(GyanisScribe::Core::SettingsStore &settings)
    {
        auto integration = GyanisScribe::Platform::createPlatformIntegration();
        auto engine      = GyanisScribe::Plugins::createOcrEngine(settings, *integration);
        if (engine == nullptr || !engine->isAvailable())
        {
            std::printf("OCR-UNAVAILABLE %s\n", engine == nullptr ? "" : engine->unavailableReason().toStdString().c_str());
            return {};
        }
        return engine;
    }

    /// 把一张图或一份真值行跑成 OcrResult
    std::optional<GyanisScribe::Core::OcrResult> readOcrResult(const QString &source, bool fromLines, GyanisScribe::Core::SettingsStore &settings)
    {
        GyanisScribe::Core::OcrResult result;
        result.language = QString::fromUtf8(GyanisScribe::Core::kDefaultOcrLanguage);
        if (fromLines)
        {
            QFile file{source};
            if (!file.open(QIODevice::ReadOnly))
            {
                std::fprintf(stderr, "cannot read lines: %s\n", source.toStdString().c_str());
                return std::nullopt;
            }
            int row = 0;
            while (!file.atEnd())
            {
                ++row;
                const QString line = QString::fromUtf8(file.readLine()).trimmed();
                if (line.isEmpty())
                {
                    continue;
                }
                // 真值行是基准，坏行不能静默跳过：少一行就会把缺失的格子算成产品漏检，红记到产品头上
                const QStringList fields  = line.split(QLatin1Char('\t'));
                bool              numeric = true;
                int               values[4]{};
                if (fields.size() >= 5)
                {
                    for (int field = 0; field < 4; ++field)
                    {
                        values[field] = fields[field].toInt(&numeric);
                        if (!numeric)
                        {
                            break;
                        }
                    }
                }
                if (fields.size() < 5 || !numeric)
                {
                    std::fprintf(stderr, "ERROR bad lines row %d: %s\n", row, line.toStdString().c_str());
                    return std::nullopt;
                }
                const QRect box{values[0], values[1], values[2], values[3]};
                result.lines.push_back(GyanisScribe::Core::OcrLine{{GyanisScribe::Core::OcrWord{fields.mid(4).join(QLatin1Char('\t')), box}}, box});
            }
            return result;
        }

        const auto image = loadImage(source);
        if (image.isNull())
        {
            return std::nullopt;
        }
        auto engine = makeEngine(settings);
        if (engine == nullptr)
        {
            return std::nullopt;
        }
        const auto language   = settings.stringValue(GyanisScribe::Core::kOcrLanguage, QString::fromUtf8(GyanisScribe::Core::kDefaultOcrLanguage));
        auto       recognized = engine->recognize(image, language);
        if (!recognized)
        {
            std::printf("OCR-FAILED %s\n", recognized.error().message.c_str());
            return std::nullopt;
        }
        return *recognized;
    }

    /// 单元格与续行里的换行/制表压成空格：判据按空白归一后比对
    std::string flatText(QString text)
    {
        text.replace(QLatin1Char('\n'), QLatin1Char(' '));
        text.replace(QLatin1Char('\t'), QLatin1Char(' '));
        return text.simplified().toStdString();
    }

    /// 表格真值一格一行：把同一水平带上的单格行并成一条识别行，检测器才看得到行内多词
    GyanisScribe::Core::OcrResult mergeSameBandLines(GyanisScribe::Core::OcrResult result)
    {
        GyanisScribe::Core::OcrResult merged;
        merged.language          = result.language;
        merged.averageConfidence = result.averageConfidence;
        for (const auto &line: result.lines)
        {
            if (line.words.isEmpty())
            {
                continue;
            }
            bool joined = false;
            for (auto &target: merged.lines)
            {
                // 同一行上的格子纵带必然重叠；上下相邻的两行之间留着行距，不会并到一起
                if (line.boundingBox.top() < target.boundingBox.bottom() && target.boundingBox.top() < line.boundingBox.bottom())
                {
                    target.words += line.words;
                    target.boundingBox = target.boundingBox.united(line.boundingBox);
                    joined             = true;
                    break;
                }
            }
            if (!joined)
            {
                merged.lines.push_back(line);
            }
        }
        std::sort(merged.lines.begin(), merged.lines.end(),
                  [](const GyanisScribe::Core::OcrLine &left, const GyanisScribe::Core::OcrLine &right)
                  {
                      if (left.boundingBox.top() != right.boundingBox.top())
                      {
                          return left.boundingBox.top() < right.boundingBox.top();
                      }
                      return left.boundingBox.left() < right.boundingBox.left();
                  });
        for (auto &line: merged.lines)
        {
            std::sort(line.words.begin(), line.words.end(),
                      [](const GyanisScribe::Core::OcrWord &left, const GyanisScribe::Core::OcrWord &right) { return left.boundingBox.left() < right.boundingBox.left(); });
        }
        return merged;
    }

    int runStitch(const QStringList &arguments)
    {
        if (arguments.size() != 4)
        {
            std::fprintf(stderr, "usage: AcceptanceTool stitch <framesDir> <outPng>\n");
            return 2;
        }
        QDir              directory{arguments[2]};
        const QStringList frames = directory.entryList({QStringLiteral("frame-*.png")}, QDir::Files, QDir::Name);
        if (frames.isEmpty())
        {
            std::fprintf(stderr, "no frames in %s\n", arguments[2].toStdString().c_str());
            return 3;
        }
        const QImage first = loadImage(directory.filePath(frames.front()));
        if (first.isNull())
        {
            return 3;
        }

        GyanisScribe::Core::FrameStitcher stitcher{first};
        int                               seams  = 0;
        int                               failed = 0;
        for (qsizetype index = 1; index < frames.size(); ++index)
        {
            const auto appended = stitcher.append(loadImage(directory.filePath(frames[index])));
            if (appended)
            {
                ++seams;
            } else
            {
                ++failed;
                std::printf("SEAM-FAIL %s %s\n", frames[index].toStdString().c_str(), appended.error().message.c_str());
            }
        }
        const auto snapshot = stitcher.snapshot();
        if (!snapshot.save(arguments[3], "PNG"))
        {
            std::fprintf(stderr, "cannot write %s\n", arguments[3].toStdString().c_str());
            return 3;
        }
        std::printf("FRAMES %lld SEAMS %d FAILED %d HEIGHT %d WIDTH %d STICKY-TOP %d STICKY-BOTTOM %d\n", static_cast<long long>(frames.size()), seams, failed, snapshot.height(),
                    snapshot.width(), stitcher.stickyTopRowsForTesting(), stitcher.stickyBottomRowsForTesting());
        return 0;
    }

    int runTables(const QStringList &arguments, bool fromLines)
    {
        if (arguments.size() != 4)
        {
            std::fprintf(stderr, "usage: AcceptanceTool %s <source> <outCsv>\n", fromLines ? "tables-from-lines" : "tables");
            return 2;
        }
        GyanisScribe::Core::SettingsStore settings{std::filesystem::temp_directory_path() / "acceptance-ocr.ini"};
        const auto                        parsed = readOcrResult(arguments[2], fromLines, settings);
        if (!parsed)
        {
            return 3;
        }
        const auto result = fromLines ? mergeSameBandLines(*parsed) : *parsed;
        const auto table  = GyanisScribe::Core::detectTable(result);
        std::printf("FOUND %d COMPLEX %d ROWS %d COLS %d CONF %.2f\n", table.found ? 1 : 0, table.complex ? 1 : 0, table.rows, table.columns, table.confidence);
        for (int row = 0; row < table.rows; ++row)
        {
            for (int column = 0; column < table.columns; ++column)
            {
                std::printf("CELL %d\t%d\t%s\n", row, column, flatText(table.cells[row][column]).c_str());
            }
        }
        if (table.complex)
        {
            std::printf("COMPLEX-REASON %s\n", flatText(table.complexReason).c_str());
            return 0;
        }
        if (!table.found)
        {
            return 0;
        }
        const auto written = GyanisScribe::Core::writeCsvFile(std::filesystem::path{arguments[3].toStdWString()}, table);
        if (!written)
        {
            std::printf("CSV-FAILED %s\n", written.error().message.c_str());
            return 3;
        }
        return 0;
    }

    int runMarkdown(const QStringList &arguments, bool fromLines)
    {
        if (arguments.size() != 4)
        {
            std::fprintf(stderr, "usage: AcceptanceTool %s <source> <outMd>\n", fromLines ? "markdown-from-lines" : "markdown");
            return 2;
        }
        GyanisScribe::Core::SettingsStore settings{std::filesystem::temp_directory_path() / "acceptance-ocr.ini"};
        const auto                        result = readOcrResult(arguments[2], fromLines, settings);
        if (!result)
        {
            return 3;
        }
        const QString markdown = GyanisScribe::Core::buildMarkdown(*result, std::nullopt);
        QFile         file{arguments[3]};
        if (!file.open(QIODevice::WriteOnly))
        {
            std::fprintf(stderr, "cannot write %s\n", arguments[3].toStdString().c_str());
            return 3;
        }
        file.write(markdown.toUtf8());
        std::printf("MD-BYTES %lld\n", static_cast<long long>(markdown.toUtf8().size()));
        return 0;
    }

    /// 按声明把整页长图切成视口帧、回贴粘性头尾，并写出期望成品
    int runSlice(const QStringList &arguments)
    {
        if (arguments.size() != 7)
        {
            std::fprintf(stderr, "usage: AcceptanceTool slice <pagePng> <metaJson> <outDir> <viewport> <step>\n");
            return 2;
        }
        const auto page = loadImage(arguments[2]);
        if (page.isNull())
        {
            return 3;
        }
        QFile metaFile{arguments[3]};
        if (!metaFile.open(QIODevice::ReadOnly))
        {
            std::fprintf(stderr, "cannot read meta: %s\n", arguments[3].toStdString().c_str());
            return 3;
        }
        const auto meta          = QJsonDocument::fromJson(metaFile.readAll()).object();
        const int  top           = meta.value(QStringLiteral("sticky_top")).toInt();
        const int  bottom        = meta.value(QStringLiteral("sticky_bottom")).toInt();
        const int  contentBottom = meta.value(QStringLiteral("content_bottom")).toInt(page.height() - bottom);
        const int  viewport      = arguments[5].toInt();
        const int  step          = arguments[6].toInt();
        // 帧内可滚动区是视口去掉粘性头尾的那一段：它至少要容下一次步进加最小重叠，否则拼接器本来就会判失败
        const int contentRows = contentBottom - top;
        const int middle      = viewport - top - bottom;
        if (viewport <= 0 || step <= 0 || top < 0 || bottom < 0 || middle <= GyanisScribe::Core::kMinimumOverlapRows || step > middle - GyanisScribe::Core::kMinimumOverlapRows ||
            contentRows < middle || contentBottom > page.height())
        {
            std::fprintf(stderr, "bad viewport/step: cannot leave a usable overlap\n");
            return 2;
        }

        std::error_code             ignored;
        const std::filesystem::path outDir{arguments[4].toStdWString()};
        std::filesystem::create_directories(outDir, ignored);
        for (const auto &entry: std::filesystem::directory_iterator(outDir, ignored))
        {
            const auto name = entry.path().filename().wstring();
            if (name.starts_with(L"frame-") && entry.path().extension() == L".png")
            {
                std::filesystem::remove(entry.path(), ignored);
            }
        }

        QList<int> offsets;
        for (int offset = 0; offset + viewport <= contentBottom; offset += step)
        {
            offsets.push_back(offset);
        }
        // 最后一段要让可滚动区的底边正好落在内容底：按视口底对齐会漏掉底带那么高的一截内容
        const int last = contentRows - middle;
        if (offsets.isEmpty() || offsets.back() < last)
        {
            // 最后一段与上一段的公共内容不足最小重叠时补一段把它摊开，接缝搜索要求至少 24 行
            if (!offsets.isEmpty() && offsets.back() + middle - last < GyanisScribe::Core::kMinimumOverlapRows && last - step > offsets.back())
            {
                offsets.push_back(last - step);
            }
            offsets.push_back(last);
        }
        // 拼接器的首帧连同粘性底带一起进图，单帧页没有接缝把底带裁掉，期望成品注定对不上
        if (offsets.size() < 2)
        {
            std::fprintf(stderr, "ERROR single-frame page: need at least two frames\n");
            return 3;
        }

        int index = 0;
        for (const int offset: offsets)
        {
            QImage frame = page.copy(0, offset, page.width(), qMin(viewport, page.height() - offset));
            if (frame.height() < viewport)
            {
                QImage padded{page.width(), viewport, QImage::Format_ARGB32_Premultiplied};
                padded.fill(Qt::transparent);
                QPainter painter{&padded};
                painter.drawImage(0, 0, frame);
                painter.end();
                frame = padded;
            }
            if (top > 0)
            {
                QPainter painter{&frame};
                painter.drawImage(0, 0, page.copy(0, 0, page.width(), top));
            }
            if (bottom > 0)
            {
                QPainter painter{&frame};
                painter.drawImage(0, viewport - bottom, page.copy(0, page.height() - bottom, page.width(), bottom));
            }
            const auto path = outDir / std::format("frame-{:03d}.png", index);
            if (!frame.save(QString::fromStdWString(path.wstring()), "PNG"))
            {
                std::fprintf(stderr, "ERROR cannot write %s\n", QString::fromStdWString(path.wstring()).toStdString().c_str());
                return 3;
            }
            ++index;
        }
        // 成品与产品里的长图一致：首帧连同粘性标题栏一起进图，粘性底带在接缝里被裁掉
        const auto expected = outDir / "expected.png";
        if (!page.copy(0, 0, page.width(), contentBottom).save(QString::fromStdWString(expected.wstring()), "PNG"))
        {
            std::fprintf(stderr, "ERROR cannot write %s\n", QString::fromStdWString(expected.wstring()).toStdString().c_str());
            return 3;
        }
        std::printf("FRAMES %d\n", index);
        return 0;
    }

    /// 两张图逐行比对：尺寸与行数差分开报，尺寸不同就不做行比对
    int runDiff(const QStringList &arguments)
    {
        if (arguments.size() != 4)
        {
            std::fprintf(stderr, "usage: AcceptanceTool diff <aPng> <bPng>\n");
            return 2;
        }
        const auto left  = loadImage(arguments[2]).convertToFormat(QImage::Format_ARGB32);
        const auto right = loadImage(arguments[3]).convertToFormat(QImage::Format_ARGB32);
        if (left.isNull() || right.isNull())
        {
            return 3;
        }
        const bool sameSize = left.size() == right.size();
        if (!sameSize)
        {
            std::printf("SIZE-MATCH 0 HEIGHT-DELTA %d ROW-DIFF na\n", left.height() - right.height());
            return 0;
        }
        int differingRows = 0;
        for (int y = 0; y < left.height(); ++y)
        {
            if (std::memcmp(left.constScanLine(y), right.constScanLine(y), static_cast<std::size_t>(left.width()) * 4U) != 0)
            {
                ++differingRows;
            }
        }
        std::printf("SIZE-MATCH 1 HEIGHT-DELTA 0 ROW-DIFF %d\n", differingRows);
        return 0;
    }

    int runRewind4k(const QStringList &arguments)
    {
        if (arguments.size() != 4)
        {
            std::fprintf(stderr, "usage: AcceptanceTool rewind4k <frameCount> <outDir>\n");
            return 2;
        }
        const int frameCount = arguments[2].toInt();
        if (frameCount <= 0)
        {
            std::fprintf(stderr, "frameCount must be positive\n");
            return 2;
        }
        std::error_code             ignored;
        const std::filesystem::path outDir{arguments[3].toStdWString()};
        std::filesystem::create_directories(outDir, ignored);

        // 按产品的默认档开环形缓冲（十秒、五帧每秒、100 MB 预算）；这一批帧够不着时间上限，量的是 4K 编码吞吐与环形缓冲接入，不是预算打满
        GyanisScribe::Core::FrameRingBuffer ring{GyanisScribe::Core::kDefaultRewindBufferSeconds, GyanisScribe::Core::kDefaultRewindFrameRate,
                                                 GyanisScribe::Core::kRewindMemoryBudgetMb};
        QElapsedTimer                       watch;
        watch.start();
        qint64 encodedBytes = 0;
        for (int index = 0; index < frameCount; ++index)
        {
            QImage frame{QSize{3840, 2160}, QImage::Format_ARGB32_Premultiplied};
            frame.fill(QColor{24, 32, 40});
            {
                QPainter painter{&frame};
                painter.fillRect(QRect{(index * 37) % 3400, 180, 400, 300}, QColor{210, 180, 40});
                painter.setPen(Qt::white);
                painter.drawText(QRect{120, 120, 1200, 60}, Qt::AlignLeft, QStringLiteral("4K 验收帧 %1").arg(index));
            }
            // 首帧落盘当证据：吞吐读数要能对上喂进去的画面，编码前存的是原始帧
            if (index == 0)
            {
                const auto evidence = outDir / "frame-000.png";
                if (!frame.save(QString::fromStdWString(evidence.wstring()), "PNG"))
                {
                    std::fprintf(stderr, "ERROR cannot write %s\n", QString::fromStdWString(evidence.wstring()).toStdString().c_str());
                    return 3;
                }
            }
            const auto encoded = GyanisScribe::Core::encodeRewindFrame(frame);
            if (!encoded)
            {
                std::printf("ENCODE-FAILED %s\n", encoded.error().message.c_str());
                return 3;
            }
            encodedBytes += encoded->encodedPixels.size();

            GyanisScribe::Core::RewindFrameSample sample;
            sample.id                = static_cast<quint64>(index + 1);
            sample.monotonicMs       = static_cast<qint64>(index) * 200;
            sample.capturedAt        = QDateTime::currentDateTime();
            sample.encodedPixels     = encoded->encodedPixels;
            sample.thumbnail         = encoded->thumbnail;
            sample.logicalSize       = QSize{3840, 2160};
            sample.devicePixelRatio  = 1.0;
            sample.screenName        = QStringLiteral("ACCEPTANCE-4K");
            sample.screenLogicalRect = QRect{0, 0, 3840, 2160};
            sample.isCandidate       = true;
            const auto appended      = ring.append(std::move(sample));
            if (!appended)
            {
                std::printf("APPEND-FAILED %s\n", appended.error().message.c_str());
                return 3;
            }
        }
        const auto stats = ring.stats();
        std::printf("4K FRAMES %d ENCODED-BYTES %lld MS %lld SAMPLES %d UNIQUE-BYTES %lld THUMB-BYTES %lld OVER-BUDGET %d\n", frameCount, static_cast<long long>(encodedBytes),
                    static_cast<long long>(watch.elapsed()), stats.sampleCount, static_cast<long long>(stats.uniquePixelBytes), static_cast<long long>(stats.thumbnailBytes),
                    stats.overBudget ? 1 : 0);
        return 0;
    }
} // namespace

int main(int argc, char *argv[])
{
    // 4K 帧里要画中文，字体库只在 QGuiApplication 下才可用：QCoreApplication 里第一次 drawText 会一直等下去
    QGuiApplication   application(argc, argv);
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() < 2)
    {
        std::fprintf(stderr, "usage: AcceptanceTool <stitch|slice|diff|tables|markdown|tables-from-lines|markdown-from-lines|rewind4k> ...\n");
        return 2;
    }
    const QString &mode = arguments[1];
    if (mode == QStringLiteral("stitch"))
    {
        return runStitch(arguments);
    }
    if (mode == QStringLiteral("slice"))
    {
        return runSlice(arguments);
    }
    if (mode == QStringLiteral("diff"))
    {
        return runDiff(arguments);
    }
    if (mode == QStringLiteral("tables"))
    {
        return runTables(arguments, false);
    }
    if (mode == QStringLiteral("tables-from-lines"))
    {
        return runTables(arguments, true);
    }
    if (mode == QStringLiteral("markdown"))
    {
        return runMarkdown(arguments, false);
    }
    if (mode == QStringLiteral("markdown-from-lines"))
    {
        return runMarkdown(arguments, true);
    }
    if (mode == QStringLiteral("rewind4k"))
    {
        return runRewind4k(arguments);
    }
    std::fprintf(stderr, "unknown mode: %s\n", mode.toStdString().c_str());
    return 2;
}
