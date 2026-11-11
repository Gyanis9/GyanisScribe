#include "Base/Types.h"
#include "Core/Steps/StepHtmlBuilder.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <filesystem>
#include <vector>

using namespace GyanisScribe::Core;

namespace
{
    RecordedStep textStep(const QString &description, const QString &imagePath = {})
    {
        RecordedStep step;
        step.kind        = StepKind::Click;
        step.at          = QDateTime::currentDateTime();
        step.description = description;
        step.imagePath   = imagePath;
        return step;
    }

    /// 按出现顺序取出 HTML 里所有 data URI 的解码字节
    std::vector<QByteArray> dataUriPayloads(const QString &html)
    {
        const QString           marker = QStringLiteral("data:image/jpeg;base64,");
        std::vector<QByteArray> payloads;
        auto                    start = qsizetype{0};
        while (true)
        {
            const auto position = html.indexOf(marker, start);
            if (position < 0)
            {
                break;
            }
            const auto valueStart = position + marker.size();
            const auto valueEnd   = html.indexOf(QLatin1Char('"'), valueStart);
            if (valueEnd <= valueStart)
            {
                break;
            }
            payloads.push_back(QByteArray::fromBase64(html.mid(valueStart, valueEnd - valueStart).toLatin1()));
            start = valueEnd + 1;
        }
        return payloads;
    }

    /// 从 HTML 里取第一条 data URI 的解码字节；没有 data URI 时返回空
    QByteArray firstDataUriBytes(const QString &html)
    {
        const auto payloads = dataUriPayloads(html);
        return payloads.empty() ? QByteArray{} : payloads.front();
    }
} // namespace

/// 说明里的标签/引号被转义，不把用户文本当 HTML
TEST(StepHtmlBuilder, EscapesUserText)
{
    const auto html = buildStepHtml({textStep(QStringLiteral("<script>alert(\"x\")</script> & 1"))}, QDateTime::currentDateTime());
    EXPECT_TRUE(html.contains(QStringLiteral("&lt;script&gt;")));
    EXPECT_FALSE(html.contains(QStringLiteral("<script>alert")));
    EXPECT_TRUE(html.contains(QStringLiteral("&amp;")));
}

/// 窗口标题同样是用户文本，标签、引号、撇号都要转义
TEST(StepHtmlBuilder, EscapesWindowTitles)
{
    auto step        = textStep(QStringLiteral("打开设置"));
    step.windowTitle = QStringLiteral("<b>设置 & \"引号\" '撇号'</b>");
    const auto html  = buildStepHtml({step}, QDateTime::currentDateTime());
    EXPECT_TRUE(html.contains(QStringLiteral("&lt;b&gt;设置 &amp; &quot;引号&quot; &#39;撇号&#39;&lt;/b&gt;")));
    EXPECT_FALSE(html.contains(QStringLiteral("<b>设置")));
}

/// 两步按顺序编号；无图步骤显示占位；有图步骤内嵌 JPEG data URI
TEST(StepHtmlBuilder, NumbersStepsAndEmbedsImages)
{
    QTemporaryDir dir;
    QImage        image{40, 20, QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::blue);
    const auto path = QDir{dir.path()}.filePath(QStringLiteral("step-001.png"));
    ASSERT_TRUE(image.save(path, "PNG"));
    const auto html = buildStepHtml({textStep(QStringLiteral("第一步"), path), textStep(QStringLiteral("第二步"))}, QDateTime::currentDateTime());
    EXPECT_TRUE(html.contains(QStringLiteral("01")));
    EXPECT_TRUE(html.contains(QStringLiteral("02")));
    EXPECT_TRUE(html.contains(QStringLiteral("data:image/jpeg;base64,")));
    EXPECT_TRUE(html.contains(QStringLiteral("这一张没截到")));
    EXPECT_TRUE(html.contains(QStringLiteral("charset=\"utf-8\"")));
}

/// 两步各自的截图都内嵌，且先出现第一步的图，顺序不乱
TEST(StepHtmlBuilder, EmbedsEachStepsImageInDocumentOrder)
{
    QTemporaryDir dir;
    QImage        first{30, 10, QImage::Format_ARGB32_Premultiplied};
    first.fill(Qt::blue);
    const auto firstPath = QDir{dir.path()}.filePath(QStringLiteral("first.png"));
    ASSERT_TRUE(first.save(firstPath, "PNG"));

    QImage second{10, 30, QImage::Format_ARGB32_Premultiplied};
    second.fill(Qt::red);
    const auto secondPath = QDir{dir.path()}.filePath(QStringLiteral("second.png"));
    ASSERT_TRUE(second.save(secondPath, "PNG"));

    const auto html = buildStepHtml({textStep(QStringLiteral("第一步"), firstPath), textStep(QStringLiteral("第二步"), secondPath)}, QDateTime::currentDateTime());
    EXPECT_EQ(html.count(QStringLiteral("data:image/jpeg;base64,")), 2);

    // 两张图尺寸不同，按出现顺序解码就能看出谁在前
    const auto payloads = dataUriPayloads(html);
    ASSERT_EQ(payloads.size(), 2U);
    const auto firstDecoded  = QImage::fromData(payloads[0]);
    const auto secondDecoded = QImage::fromData(payloads[1]);
    ASSERT_FALSE(firstDecoded.isNull());
    ASSERT_FALSE(secondDecoded.isNull());
    EXPECT_EQ(firstDecoded.size(), QSize(30, 10)) << "第一步的图没有排在前面";
    EXPECT_EQ(secondDecoded.size(), QSize(10, 30));
}

/// 编号按步骤在列表里的位置走，不看 RecordedStep::index；两位数补零，超过九十九步照常展开
TEST(StepHtmlBuilder, NumbersStepsByTheirPosition)
{
    std::vector<RecordedStep> steps;
    for (auto position = 0; position < 12; ++position)
    {
        auto step  = textStep(QStringLiteral("第 %1 步说明").arg(position + 1));
        step.index = 100 - position;
        steps.push_back(step);
    }
    const auto html = buildStepHtml(steps, QDateTime::currentDateTime());
    EXPECT_TRUE(html.contains(QStringLiteral("<div class=\"num\">01</div>")));
    EXPECT_TRUE(html.contains(QStringLiteral("<div class=\"num\">12</div>")));
    EXPECT_EQ(html.count(QStringLiteral("<div class=\"num\">")), 12);
}

/// 没有步骤时不出半份文档
TEST(StepHtmlBuilder, ReturnsEmptyHtmlWithoutSteps)
{
    EXPECT_TRUE(buildStepHtml({}, QDateTime::currentDateTime()).isEmpty());
}

/// 标题区写出生成时刻与步骤数；每步写出自己的时刻与窗口标题
TEST(StepHtmlBuilder, ShowsTimesCountAndWindowTitle)
{
    const QDateTime generatedAt{QDate{2026, 11, 11}, QTime{12, 34, 56}};
    auto            step = textStep(QStringLiteral("点击保存"));
    step.at              = QDateTime{QDate{2026, 11, 11}, QTime{8, 9, 10}};
    step.windowTitle     = QStringLiteral("编辑器");
    const auto html      = buildStepHtml({step}, generatedAt);
    EXPECT_TRUE(html.contains(QStringLiteral("2026-11-11 12:34:56")));
    EXPECT_TRUE(html.contains(QStringLiteral("共 1 步")));
    EXPECT_TRUE(html.contains(QStringLiteral("2026-11-11 08:09:10")));
    EXPECT_TRUE(html.contains(QStringLiteral("窗口：编辑器")));
}

/// 说明被清空时只写中性的类型标签，不把用户删掉的元素名、进程名又补回来
TEST(StepHtmlBuilder, FallsBackToNeutralKindLabelWhenEmpty)
{
    auto step               = textStep({});
    step.kind               = StepKind::WindowChange;
    step.elementName        = QStringLiteral("保存");
    step.controlType        = QStringLiteral("按钮");
    step.windowTitle        = QStringLiteral("机密合同.docx");
    step.processName        = QStringLiteral("notepad.exe");
    step.globalLogicalPoint = QPoint{12, 34};

    const auto html = buildStepHtml({step}, QDateTime::currentDateTime());
    EXPECT_TRUE(html.contains(QStringLiteral("<h2>切换窗口</h2>")));
    EXPECT_FALSE(html.contains(QStringLiteral("保存"))) << "清空说明后元素名又回到了导出里";
    EXPECT_FALSE(html.contains(QStringLiteral("notepad"))) << "清空说明后进程名又回到了导出里";

    auto clickStep       = step;
    clickStep.kind       = StepKind::Click;
    const auto clickHtml = buildStepHtml({clickStep}, QDateTime::currentDateTime());
    EXPECT_TRUE(clickHtml.contains(QStringLiteral("<h2>点击</h2>")));
    EXPECT_FALSE(clickHtml.contains(QStringLiteral("保存")));

    // 有说明的步骤原样输出，不换成类型标签
    auto described           = step;
    described.kind           = StepKind::Click;
    described.description    = QStringLiteral("自定义说明");
    const auto describedHtml = buildStepHtml({described}, QDateTime::currentDateTime());
    EXPECT_TRUE(describedHtml.contains(QStringLiteral("<h2>自定义说明</h2>")));
}

/// 点击步骤内嵌的是补画过标记的截图，序号用步骤在文档里的显示序号
TEST(StepHtmlBuilder, EmbedsMarkedScreenshotForClicks)
{
    QTemporaryDir dir;
    QImage        screen{200, 200, QImage::Format_ARGB32_Premultiplied};
    screen.fill(Qt::white);
    const auto path = QDir{dir.path()}.filePath(QStringLiteral("step-001.png"));
    ASSERT_TRUE(screen.save(path, "PNG"));

    auto step              = textStep(QStringLiteral("点击按钮"), path);
    step.clickPointInImage = QPoint{100, 100};
    const auto html        = buildStepHtml({step}, QDateTime::currentDateTime());
    const auto payload     = firstDataUriBytes(html);
    ASSERT_FALSE(payload.isEmpty());
    const auto image = QImage::fromData(payload);
    ASSERT_FALSE(image.isNull());

    // 圆环是品牌红，JPEG 编码后仍应明显偏红；白底原图不会有这种像素
    auto ring = false;
    for (auto x = 70; x <= 130; ++x)
    {
        const auto color = image.pixelColor(x, 100);
        if (color.red() > 150 && color.red() - color.green() > 40 && color.red() - color.blue() > 40)
        {
            ring = true;
            break;
        }
    }
    EXPECT_TRUE(ring) << "导出的截图没有画点击标记";
}

/// 抓屏失败原因写在占位里，同样转义
TEST(StepHtmlBuilder, ShowsCaptureErrorInThePlaceholder)
{
    auto step         = textStep(QStringLiteral("失败的一步"));
    step.captureError = QStringLiteral("抓屏失败 <原因>");
    const auto html   = buildStepHtml({step}, QDateTime::currentDateTime());
    EXPECT_TRUE(html.contains(QStringLiteral("这一张没截到")));
    EXPECT_TRUE(html.contains(QStringLiteral("&lt;原因&gt;")));
    EXPECT_FALSE(html.contains(QStringLiteral("<原因>")));
}

/// 大图最长边缩到 1600 再按 JPEG 质量 85 内嵌；小图保持原尺寸，不放大
TEST(StepHtmlBuilder, DownsizesLargeImagesAndKeepsSmallOnes)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    QImage large{2000, 1000, QImage::Format_ARGB32_Premultiplied};
    large.fill(Qt::blue);
    const auto largePath = QDir{dir.path()}.filePath(QStringLiteral("large.png"));
    ASSERT_TRUE(large.save(largePath, "PNG"));

    QImage small{120, 60, QImage::Format_ARGB32_Premultiplied};
    small.fill(Qt::green);
    const auto smallPath = QDir{dir.path()}.filePath(QStringLiteral("small.png"));
    ASSERT_TRUE(small.save(smallPath, "PNG"));

    const auto largeHtml = buildStepHtml({textStep(QStringLiteral("大图"), largePath)}, QDateTime::currentDateTime());
    const auto largeData = firstDataUriBytes(largeHtml);
    ASSERT_FALSE(largeData.isEmpty());
    const auto largeDecoded = QImage::fromData(largeData);
    ASSERT_FALSE(largeDecoded.isNull());
    EXPECT_EQ(largeDecoded.size(), QSize(1600, 800));

    const auto smallHtml = buildStepHtml({textStep(QStringLiteral("小图"), smallPath)}, QDateTime::currentDateTime());
    const auto smallData = firstDataUriBytes(smallHtml);
    ASSERT_FALSE(smallData.isEmpty());
    const auto smallDecoded = QImage::fromData(smallData);
    ASSERT_FALSE(smallDecoded.isNull());
    EXPECT_EQ(smallDecoded.size(), QSize(120, 60)) << "小图不该为了凑 1600 而放大";
}

/// 写文件用 QSaveFile；空步骤拒绝；失败带中文原因
TEST(StepHtmlBuilder, WritesFileAndRejectsEmpty)
{
    QTemporaryDir dir;
    const auto    path = std::filesystem::path{dir.path().toStdWString()} / L"tutorial.html";

    const auto rejected = writeStepHtml(path, {}, QDateTime::currentDateTime());
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument);
    EXPECT_TRUE(QString::fromStdString(rejected.error().message).contains(QStringLiteral("还没有步骤可以导出")));
    EXPECT_FALSE(std::filesystem::exists(path)) << "被拒绝的导出不该留下文件";

    ASSERT_TRUE(writeStepHtml(path, {textStep(QStringLiteral("唯一一步"))}, QDateTime::currentDateTime()).has_value());
    EXPECT_TRUE(std::filesystem::exists(path));

    QFile file{QString::fromStdWString(path.wstring())};
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto content = QString::fromUtf8(file.readAll());
    EXPECT_TRUE(content.startsWith(QStringLiteral("<!doctype html>")));
    EXPECT_TRUE(content.contains(QStringLiteral("唯一一步")));
    EXPECT_TRUE(content.contains(QStringLiteral("<html lang=\"zh-CN\">")));
}

/// 目标写不出去时返回带路径的中文原因
TEST(StepHtmlBuilder, ReportsFileWriteFailures)
{
    QTemporaryDir dir;
    const auto    path = std::filesystem::path{dir.path().toStdWString()} / L"blocked.html";
    ASSERT_TRUE(std::filesystem::create_directory(path));

    const auto written = writeStepHtml(path, {textStep(QStringLiteral("唯一一步"))}, QDateTime::currentDateTime());
    ASSERT_FALSE(written.has_value());
    EXPECT_EQ(written.error().code, GyanisScribe::Base::ErrorCode::FileWriteFailed);
    const auto message = QString::fromStdString(written.error().message);
    EXPECT_TRUE(message.contains(QStringLiteral("步骤教程"))) << message.toStdString();
    EXPECT_TRUE(message.contains(QStringLiteral("blocked.html"))) << message.toStdString();
}
