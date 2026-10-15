#include "Plugins/OcrEngines/TesseractOcrEngine.h"

#include "Base/Paths.h"
#include "Base/Logger.h"
#include "Base/Text.h"
#include "Plugins/OcrEngines/OcrEngineFactory.h"
#include "Core/Ocr/OcrPreprocessor.h"

#include <tesseract/capi.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace GyanisScribe::Plugins
{
    namespace
    {
        // 认不出字的最小图尺寸：比这更小 Tesseract 会给出随机结果，不如直接拒
        constexpr int kMinimumImageEdge = 8;

        // 判定「同一个文本行」的纵向容差除数：词框高度的一半之内算同一行
        constexpr int kLineMergeToleranceDivisor = 2;

        // Tesseract 给的置信度是 0-100
        constexpr double kConfidencePercentScale = 100.0;

        // 这条通道送识别前的放大倍率，与系统那一条通道取同一个上限档（理由见 recognize 里的实测读数）
        constexpr int kEnlargeFactor = Core::kMaximumEnlargeFactor;

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
            Base::Logger::error("Tesseract 后端建不出句柄：TessBaseAPICreate 返回空");
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
                GyanisScribe::Base::uiFormat(
                        GyanisScribe::Base::uiTextUtf8(
                                "语言包目录 {} 里没有本程序认识的 traineddata。请至少放入 eng.traineddata（要认中文再加 chi_sim.traineddata），或在设置的 OCR 页把目录指过去"),
                        Base::Paths::toDisplayText(m_dataDirectory).toStdString())
                        .c_str());
    }

    QStringList TesseractOcrEngine::supportedLanguages() const
    {
        QStringList found;
        for (const auto &tag: Core::supportedOcrLanguageTags())
        {
            const auto model = m_dataDirectory / (modelFileNameFor(tag) + QStringLiteral(".traineddata")).toStdString();
            if (std::filesystem::exists(model))
            {
                found << tag;
            }
        }
        return found;
    }

    Base::Expected<void> TesseractOcrEngine::ensureLoaded(const QString &languageTag)
    {
        if (m_api == nullptr)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InternalError,
                                               GyanisScribe::Base::uiTextUtf8("Tesseract 句柄没建出来（内存分配失败）。请重启本程序，若仍然如此请改用系统自带识别或稍后再试")});
        }

        if (m_loadedLanguage == languageTag)
        {
            return {};
        }

        const auto model = m_dataDirectory / (modelFileNameFor(languageTag) + QStringLiteral(".traineddata")).toStdString();
        if (!std::filesystem::exists(model))
        {
            return std::unexpected(Base::Error{
                    Base::ErrorCode::DependencyMissing,
                    GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("语言「{}」需要的模型文件不在：{}。请在设置的 OCR 页改选已有语言，或把该文件放进语言包目录"),
                                                 languageTag.toStdString(), Base::Paths::toDisplayText(model).toStdString())});
        }

        // Init3 会换掉上一份语言：不先释放旧语言就重复 Init 是 Tesseract 允许的行为，但语言状态以这次为准
        // 例外：Tesseract 与 Leptonica 的 C 接口按本地码页开文件，这一处不走 toDisplayText
        const int initialized = TessBaseAPIInit3(m_api.get(), m_dataDirectory.string().c_str(), modelFileNameFor(languageTag).toUtf8().constData());
        if (initialized != 0)
        {
            m_loadedLanguage.clear();
            return std::unexpected(Base::Error{
                    Base::ErrorCode::InternalError,
                    GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("Tesseract 初始化语言 {} 失败（返回值 {}）。请检查语言包是否完整或与本程序用的库版本一致"),
                                                 languageTag.toStdString(), initialized)});
        }

        m_loadedLanguage = languageTag;
        return {};
    }

    Base::Expected<Core::OcrResult> TesseractOcrEngine::recognize(const QImage &image, const QString &language)
    {
        if (image.isNull() || image.width() < kMinimumImageEdge || image.height() < kMinimumImageEdge)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument,
                                               GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("图片太小或为空（{}×{}），认不出字。请至少框住 {}×{} 的区域"),
                                                                            image.width(), image.height(), kMinimumImageEdge, kMinimumImageEdge)});
        }

        const QString requestedLanguage = language.isEmpty() ? QString::fromUtf8(Core::kDefaultOcrLanguage) : language;
        if (const auto loaded = ensureLoaded(requestedLanguage); !loaded)
        {
            return std::unexpected(loaded.error());
        }

        // 灰度 + 2× 放大，与系统那一条通道走同一个入口。现测（随包 eng 语言包、同一张 420×64 渲染图）：
        // 7pt 小字不放大只回来 2/3 个词且平均置信度 0.446，放大 2× 后 3/3 且 0.920；3× 与 4× 又掉回 2/3，
        // 所以倍率就取预处理模块夹住的那一档，不再往上试
        const QImage gray = Core::prepareForRecognition(image, kEnlargeFactor).convertToFormat(QImage::Format_Grayscale8);
        // 形参顺序是 (数据, 宽, 高, 每像素字节数, 每行字节数)：把「高」与「每像素字节数」写反，
        // Leptonica 会按 高×8 位深去建图并报「Cannot convert RAW image to Pix with bpp = 512」，
        // 识别一定失败——这条是靠真语言包的端到端用例才看得见的那一格
        TessBaseAPISetImage(m_api.get(), static_cast<const unsigned char *>(gray.constBits()), gray.width(), gray.height(), 1, static_cast<int>(gray.bytesPerLine()));

        if (TessBaseAPIRecognize(m_api.get(), nullptr) != 0)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InternalError,
                                               GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("Tesseract 识别这一张图失败（语言 {}）。请重试一次，或换一种语言再试"),
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
            // 送进去的是放大过的那一张，方框要按同一个倍率折回去，才与用户看到的原图同系；
            // 与系统那一条通道一样四舍五入，截断会让整框系统性偏左偏上
            const auto fold    = [](int value) { return static_cast<int>(std::lround(static_cast<float>(value) / static_cast<float>(kEnlargeFactor))); };
            bool       stepped = true;
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
                    words.push_back(RawWord{QString::fromUtf8(rawText), QRect(fold(left), fold(top), fold(right - left), fold(bottom - top)),
                                            static_cast<double>(confidence) / kConfidencePercentScale});
                    // 这份文本是 Tesseract 分配的，必须交回去，否则每认一个词漏一个串
                    TessDeleteText(const_cast<char *>(rawText));
                }
                stepped = TessPageIteratorNext(cursor, tesseract::RIL_WORD) != 0;
            }
            TessResultIteratorDelete(iterator);
        }

        Core::OcrResult result;
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
                        lastLine.words.push_back(Core::OcrWord{word.text, word.box});
                        lastLine.boundingBox = lastLine.boundingBox.united(word.box);
                        continue;
                    }
                }
            }

            Core::OcrLine line;
            line.words       = {Core::OcrWord{word.text, word.box}};
            line.boundingBox = word.box;
            result.lines.push_back(line);
        }
        if (!words.empty())
        {
            // 置信度只在真有词的时候算：一个词都没有就交回空，界面那一格说「未提供」，不打一个 0% 冒充读数
            result.averageConfidence = confidenceSum / static_cast<double>(words.size());
        }
        return result;
    }

} // namespace GyanisScribe::Plugins
