#include "Ocr/TesseractOcrEngine.h"

#include "Core/Paths.h"
#include "Core/Logger.h"
#include "Core/Text.h"
#include "Ocr/OcrEngineFactory.h"
#include "Ocr/OcrPreprocessor.h"

#include <tesseract/capi.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace GyanisScribe::Ocr
{
    namespace
    {
        // 认不出字的最小图尺寸：比这更小 Tesseract 会给出随机结果，不如直接拒
        constexpr int kMinimumImageEdge = 8;

        // 判定「同一个文本行」的纵向容差除数：词框高度的一半之内算同一行
        constexpr int kLineMergeToleranceDivisor = 2;

        // Tesseract 给的置信度是 0-100
        constexpr double kConfidencePercentScale = 100.0;

        /// 一个词的中间结果（还没分行）
        struct RawWord
        {
            QString text;
            QRect   box;
            double  confidence = 0.0; ///< 已折算成 0-1
        };
    } // namespace

    TesseractOcrEngine::TesseractOcrEngine(std::filesystem::path tessDataDirectory) : m_dataDirectory(std::move(tessDataDirectory)), m_api(nullptr, &TessBaseAPIDelete)
    {
        m_api.reset(TessBaseAPICreate());
        if (m_api == nullptr)
        {
            // 到这里只剩内存分配失败一条路，句柄为空时每次调用都会在 ensureLoaded 里报错
            Core::Logger::error("Tesseract 后端建不出句柄：TessBaseAPICreate 返回空");
        }
    }

    TesseractOcrEngine::~TesseractOcrEngine() = default;

    QString TesseractOcrEngine::name() const
    {
        return QStringLiteral("Tesseract");
    }

    QString TesseractOcrEngine::modelFileNameFor(const QString &languageTag)
    {
        if (languageTag.compare(QLatin1String("zh-Hans"), Qt::CaseInsensitive) == 0 || languageTag.compare(QLatin1String("zh-CN"), Qt::CaseInsensitive) == 0)
        {
            return QStringLiteral("chi_sim");
        }
        if (languageTag.compare(QLatin1String("en"), Qt::CaseInsensitive) == 0)
        {
            return QStringLiteral("eng");
        }
        return languageTag;
    }

    bool TesseractOcrEngine::isAvailable() const
    {
        return !supportedLanguages().isEmpty();
    }

    QString TesseractOcrEngine::unavailableReason() const
    {
        if (isAvailable())
        {
            return {};
        }
        return QString::fromUtf8(
                GyanisScribe::Core::uiFormat(
                        GyanisScribe::Core::uiTextUtf8(
                                "语言包目录 {} 里没有本程序认识的 traineddata。请至少放入 eng.traineddata（要认中文再加 chi_sim.traineddata），或在设置的 OCR 页把目录指过去"),
                        Core::Paths::toDisplayText(m_dataDirectory).toStdString())
                        .c_str());
    }

    QStringList TesseractOcrEngine::supportedLanguages() const
    {
        QStringList found;
        for (const auto &tag: supportedOcrLanguageTags())
        {
            const auto model = m_dataDirectory / (modelFileNameFor(tag) + QStringLiteral(".traineddata")).toStdString();
            if (std::filesystem::exists(model))
            {
                found << tag;
            }
        }
        return found;
    }

    Core::Expected<void> TesseractOcrEngine::ensureLoaded(const QString &languageTag)
    {
        if (m_api == nullptr)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InternalError,
                                               GyanisScribe::Core::uiTextUtf8("Tesseract 句柄没建出来（内存分配失败）。请重启本程序，若仍然如此请改用系统自带识别或稍后再试")});
        }

        if (m_loadedLanguage == languageTag)
        {
            return {};
        }

        const auto model = m_dataDirectory / (modelFileNameFor(languageTag) + QStringLiteral(".traineddata")).toStdString();
        if (!std::filesystem::exists(model))
        {
            return std::unexpected(Core::Error{
                    Core::ErrorCode::DependencyMissing,
                    GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("语言「{}」需要的模型文件不在：{}。请在设置的 OCR 页改选已有语言，或把该文件放进语言包目录"),
                                                 languageTag.toStdString(), Core::Paths::toDisplayText(model).toStdString())});
        }

        // Init3 会换掉上一份语言：不先释放旧语言就重复 Init 是 Tesseract 允许的行为，但语言状态以这次为准
        // 例外：Tesseract 与 Leptonica 的 C 接口按本地码页开文件，这一处不走 toDisplayText
        const int initialized = TessBaseAPIInit3(m_api.get(), m_dataDirectory.string().c_str(), modelFileNameFor(languageTag).toUtf8().constData());
        if (initialized != 0)
        {
            m_loadedLanguage.clear();
            return std::unexpected(Core::Error{
                    Core::ErrorCode::InternalError,
                    GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("Tesseract 初始化语言 {} 失败（返回值 {}）。请检查语言包是否完整或与本程序用的库版本一致"),
                                                 languageTag.toStdString(), initialized)});
        }

        m_loadedLanguage = languageTag;
        return {};
    }

    Core::Expected<OcrResult> TesseractOcrEngine::recognize(const QImage &image, const QString &language)
    {
        if (image.isNull() || image.width() < kMinimumImageEdge || image.height() < kMinimumImageEdge)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("图片太小或为空（{}×{}），认不出字。请至少框住 {}×{} 的区域"),
                                                                            image.width(), image.height(), kMinimumImageEdge, kMinimumImageEdge)});
        }

        const QString requestedLanguage = language.isEmpty() ? QString::fromUtf8(kDefaultOcrLanguage) : language;
        if (const auto loaded = ensureLoaded(requestedLanguage); !loaded)
        {
            return std::unexpected(loaded.error());
        }

        // 只转灰度：这一条通道没有本机对照数据，放大到底帮不帮得上说不准，就不替用户改图片尺寸
        // （系统那条通道是实测过的，结论见 OcrPreprocessor.h 的 @warning）
        const QImage gray = toGrayscale(image).convertToFormat(QImage::Format_Grayscale8);
        TessBaseAPISetImage(m_api.get(), static_cast<const unsigned char *>(gray.constBits()), gray.width(), 1, gray.height(), static_cast<int>(gray.bytesPerLine()));

        if (TessBaseAPIRecognize(m_api.get(), nullptr) != 0)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InternalError,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("Tesseract 识别这一张图失败（语言 {}）。请重试一次，或换一种语言再试"),
                                                                            requestedLanguage.toStdString())});
        }

        std::vector<RawWord> words;
        auto                *iterator = TessBaseAPIGetIterator(m_api.get());
        if (iterator != nullptr)
        {
            // ponytail: 按词框的纵坐标自己分行，因为 Tesseract 的 C 接口没有好用的行级迭代入口。
            // 上限是同一行内字号差别很大时会被拆成两行；要做到逐行准确就换成 C++ 接口按 RIL_TEXTLINE 走。
            // 游标是 ResultIterator 借出的 PageIterator：归 ResultIterator 管，不能单独释放
            auto *cursor = TessResultIteratorGetPageIterator(iterator);
            TessPageIteratorBegin(cursor);
            bool stepped = true;
            while (stepped)
            {
                int left   = 0;
                int top    = 0;
                int right  = 0;
                int bottom = 0;
                // 取不到框或取不到文本也都要往下走一步：游标不动就出不了这个循环
                const bool  boxed   = TessPageIteratorBoundingBox(cursor, tesseract::RIL_WORD, &left, &top, &right, &bottom) != 0;
                const char *rawText = TessResultIteratorGetUTF8Text(iterator, tesseract::RIL_WORD);
                if (boxed && rawText != nullptr)
                {
                    const float confidence = TessResultIteratorConfidence(iterator, tesseract::RIL_WORD);
                    words.push_back(RawWord{QString::fromUtf8(rawText), QRect(left, top, right - left, bottom - top), static_cast<double>(confidence) / kConfidencePercentScale});
                    // 这份文本是 Tesseract 分配的，必须交回去，否则每认一个词漏一个串
                    TessDeleteText(const_cast<char *>(rawText));
                }
                stepped = TessPageIteratorNext(cursor, tesseract::RIL_WORD) != 0;
            }
            TessResultIteratorDelete(iterator);
        }

        OcrResult result;
        result.language      = requestedLanguage;
        double confidenceSum = 0.0;
        for (const auto &word: words)
        {
            confidenceSum += word.confidence;

            const int tolerance = std::max(1, word.box.height() / kLineMergeToleranceDivisor);
            if (!result.lines.isEmpty())
            {
                auto &lastLine = result.lines.back();
                // 顶边相差不到半格高就算同一行；行内按横坐标顺序追加，倒序读图不会把词序打乱
                if (std::abs(lastLine.boundingBox.top() - word.box.top()) <= tolerance)
                {
                    if (word.box.left() >= lastLine.boundingBox.right())
                    {
                        lastLine.words.push_back(OcrWord{word.text, word.box});
                        lastLine.boundingBox = lastLine.boundingBox.united(word.box);
                        continue;
                    }
                }
            }

            OcrLine line;
            line.words       = {OcrWord{word.text, word.box}};
            line.boundingBox = word.box;
            result.lines.push_back(line);
        }
        if (!words.empty())
        {
            result.averageConfidence = confidenceSum / static_cast<double>(words.size());
        }
        return result;
    }

} // namespace GyanisScribe::Ocr
