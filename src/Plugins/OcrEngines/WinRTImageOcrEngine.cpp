#include "Plugins/OcrEngines/WinRTImageOcrEngine.h"

#include "Base/Logger.h"
#include "Base/Text.h"
#include "Plugins/OcrEngines/OcrEngineFactory.h"
#include "Core/Ocr/OcrPreprocessor.h"

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QStringList>

// Collections 必须排在其它投影之前：它给出 IIterable/IVectorView 的实现体，
// 缺了它任何 .Size()/.GetAt() 都是「auto 返回函数未定义」（实测 C3779）
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#include <cmath>
#include <cstring>
#include <format>
#include <future>
#include <vector>

namespace GyanisScribe::Plugins
{
    namespace
    {
        // 认不出字的最小图尺寸：与 Tesseract 后端同一道门槛，界面对两个后端的拒绝形状要一致
        constexpr int kMinimumImageEdge = 8;

        /**
         * @brief 把 WinRT 字符串换成 QString
         * @param text 系统给回的字符串
         * @return 按 UTF-16 逐码位搬过来的串
         * @note 走「指针 + 长度」而不是零终止：识别结果里出现 NUL 时零终止会静默截断文本
         */
        QString fromWinRTString(const winrt::hstring &text)
        {
            return QString::fromWCharArray(text.data(), static_cast<int>(text.size()));
        }

        /**
         * @brief 判断系统语言标签是否对上本程序对外的标签
         * @param systemTag 系统标签，可能带区域后缀，如 zh-Hans-CN
         * @param publicTag 对外标签，如 zh-Hans 或 en
         * @return true 表示对外标签的每一段都与系统标签开头各段相等
         * @note 按 '-' 分段逐段比，整串前缀会认错：en 对 en-** 成立，但对 "english…" 这类串也成立
         */
        bool matchesPublicTag(const QString &systemTag, const QString &publicTag)
        {
            const auto systemParts = systemTag.split(QLatin1Char('-'));
            const auto publicParts = publicTag.split(QLatin1Char('-'));
            if (systemParts.size() < publicParts.size())
            {
                return false;
            }
            for (qsizetype index = 0; index < publicParts.size(); ++index)
            {
                if (systemParts[index].compare(publicParts[index], Qt::CaseInsensitive) != 0)
                {
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief 在一条一次性线程上跑 WinRT 调用
         * @tparam Task 可调用体，返回类型即本函数返回类型
         * @param task 要在那条线程上执行的工作
         * @return task 的返回值
         * @details 每条调用自己把套间设成 MTA：Qt 的界面线程是 STA，而 .get() 会堵在调用线程上，
         *          在 STA 上等异步完成要靠自己那台消息泵——界面线程等的时候它不会转。
         */
        template<typename Task>
        auto runOnWinRTThread(Task task)
        {
            auto worker = std::async(std::launch::async,
                                     [task = std::move(task)]() mutable
                                     {
                                         winrt::init_apartment(winrt::apartment_type::multi_threaded);
                                         return task();
                                     });
            return worker.get();
        }
    } // namespace

    WinRTImageOcrEngine::WinRTImageOcrEngine()
    {
        const auto systemTags = runOnWinRTThread(
                []()
                {
                    QStringList tags;
                    try
                    {
                        auto const languages = winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages();
                        for (uint32_t index = 0; index < static_cast<uint32_t>(languages.Size()); ++index)
                        {
                            tags << fromWinRTString(languages.GetAt(index).LanguageTag());
                        }
                    } catch (const winrt::hresult_error &failure)
                    {
                        // 枚举本身报错说明系统组件不在场，与「装了语言但没一个对得上」在用户侧是同一条提示
                        Base::Logger::warning(
                                std::format("Windows 系统识别语言枚举失败：{}（HRESULT 0x{:08X}）", winrt::to_string(failure.message()), static_cast<uint32_t>(failure.code())));
                    }
                    return tags;
                });

        // 对外词表只有一个真源：这里按 Core::supportedOcrLanguageTags() 的顺序收，界面看到的就是这一份
        for (const auto &publicTag: Core::supportedOcrLanguageTags())
        {
            for (const auto &systemTag: systemTags)
            {
                if (matchesPublicTag(systemTag, publicTag))
                {
                    m_systemTagByPublicTag.insert(publicTag, systemTag);
                    break;
                }
            }
        }
    }

    QString WinRTImageOcrEngine::name() const
    {
        return QString::fromUtf8(GyanisScribe::Base::uiTextUtf8("Windows 系统识别").c_str());
    }

    bool WinRTImageOcrEngine::isAvailable() const
    {
        return !m_systemTagByPublicTag.isEmpty();
    }

    QString WinRTImageOcrEngine::unavailableReason() const
    {
        if (isAvailable())
        {
            return {};
        }
        return QString::fromUtf8(GyanisScribe::Base::uiTextUtf8("这台机器的 Windows 没装本程序认识的识别语言包（系统识别一个可用语言都没有）。请在 设置 → 时间和语言 → 语音 "
                                                                "里装上「光学字符识别」，或在设置的 OCR 页改用 Tesseract 并放好语言包")
                                         .c_str());
    }

    QStringList WinRTImageOcrEngine::supportedLanguages() const
    {
        QStringList found;
        for (const auto &tag: Core::supportedOcrLanguageTags())
        {
            if (m_systemTagByPublicTag.contains(tag))
            {
                found << tag;
            }
        }
        return found;
    }

    Base::Expected<Core::OcrResult> WinRTImageOcrEngine::recognize(const QImage &image, const QString &language)
    {
        if (image.isNull() || image.width() < kMinimumImageEdge || image.height() < kMinimumImageEdge)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument,
                                               GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("图片太小或为空（{}×{}），认不出字。请至少框住 {}×{} 的区域"),
                                                                            image.width(), image.height(), kMinimumImageEdge, kMinimumImageEdge)});
        }

        const QString requestedLanguage = language.isEmpty() ? QString::fromUtf8(Core::kDefaultOcrLanguage) : language;
        const auto    systemTag         = m_systemTagByPublicTag.value(requestedLanguage);
        if (systemTag.isEmpty())
        {
            return std::unexpected(
                    Base::Error{Base::ErrorCode::DependencyMissing,
                                GyanisScribe::Base::uiFormat(
                                        GyanisScribe::Base::uiTextUtf8(
                                                "系统里没有「{}」的识别语言包。现在能用的是 {}。请在 设置 → 时间和语言 → 语音 里装上需要的语言包，或在设置的 OCR 页改选语言"),
                                        requestedLanguage.toStdString(), supportedLanguages().join(QStringLiteral("、")).toStdString())});
        }

        // 放大倍率在这里显式取出来：返回的方框在放大后的坐标里，要按同一个数折回用户看到的那一张
        const int  enlargeFactor = Core::kMaximumEnlargeFactor;
        const auto prepared      = Core::prepareForRecognition(image, enlargeFactor);

        // PNG 编码放在调用线程：QImage 不是线程安全的，跨线程只带这段已经编好的字节
        QByteArray encoded;
        QBuffer    bytesDevice(&encoded);
        bytesDevice.open(QIODevice::WriteOnly);
        if (!prepared.save(&bytesDevice, "PNG") || encoded.isEmpty())
        {
            return std::unexpected(
                    Base::Error{Base::ErrorCode::InternalError, GyanisScribe::Base::uiTextUtf8("这一张图没能编成 PNG。请重试一次，或先在标注窗里另存一份 PNG 再识别")});
        }

        std::vector<uint8_t> pngBytes(static_cast<size_t>(encoded.size()));
        std::memcpy(pngBytes.data(), encoded.constData(), static_cast<size_t>(encoded.size()));
        const std::wstring tag = systemTag.toStdWString();

        return runOnWinRTThread(
                [pngBytes = std::move(pngBytes), tag, publicTag = requestedLanguage]() -> Base::Expected<Core::OcrResult>
                {
                    try
                    {
                        auto const buffer = winrt::Windows::Storage::Streams::Buffer(static_cast<uint32_t>(pngBytes.size()));
                        // 取裸指针用投影自带的 consume 扩展：它内部就是 IBufferByteAccess，
                        // 不必再拉 SDK 的 robuffer.h（那个目录不在 MSVC 默认 INCLUDE 里，干净终端下会红）
                        std::memcpy(buffer.data(), pngBytes.data(), pngBytes.size());
                        // Buffer(容量) 建出来长度是 0：不写长度就等于往流里写 0 字节，解码会拿到空流
                        buffer.Length(static_cast<uint32_t>(pngBytes.size()));

                        auto const stream = winrt::Windows::Storage::Streams::InMemoryRandomAccessStream();
                        stream.WriteAsync(buffer).get();
                        stream.Seek(0);

                        auto const decoder = winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(stream).get();
                        auto const bitmap  = decoder.GetSoftwareBitmapAsync().get();

                        // 上限来自系统自己，不硬编码：超了就只能让用户缩小范围，绝不能偷偷缩放——
                        // 方框坐标要与用户看到的那张图同系
                        const auto maximumDimension = winrt::Windows::Media::Ocr::OcrEngine::MaxImageDimension();
                        if (static_cast<uint32_t>(bitmap.PixelWidth()) > maximumDimension || static_cast<uint32_t>(bitmap.PixelHeight()) > maximumDimension)
                        {
                            return std::unexpected(
                                    Base::Error{Base::ErrorCode::InvalidArgument,
                                                GyanisScribe::Base::uiFormat(
                                                        GyanisScribe::Base::uiTextUtf8(
                                                                "这张图放大到识别尺寸（{}×{}）后超过系统上限（最长边 {}）。请分段截图后再识别，或先在标注窗里裁掉不需要的部分"),
                                                        bitmap.PixelWidth(), bitmap.PixelHeight(), maximumDimension)});
                        }

                        auto const engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(winrt::Windows::Globalization::Language(tag));
                        if (!engine)
                        {
                            return std::unexpected(
                                    Base::Error{Base::ErrorCode::DependencyMissing,
                                                GyanisScribe::Base::uiFormat(
                                                        GyanisScribe::Base::uiTextUtf8("系统说装有「{}」的识别语言，但建不出识别器。请在 设置 → 时间和语言 → 语音 里重装该语言包"),
                                                        publicTag.toStdString())});
                        }

                        auto const result = engine.RecognizeAsync(bitmap).get();

                        Core::OcrResult mapped;
                        mapped.language  = publicTag;
                        auto const lines = result.Lines();
                        for (uint32_t lineIndex = 0; lineIndex < static_cast<uint32_t>(lines.Size()); ++lineIndex)
                        {
                            Core::OcrLine line;
                            auto const    words = lines.GetAt(lineIndex).Words();
                            for (uint32_t wordIndex = 0; wordIndex < static_cast<uint32_t>(words.Size()); ++wordIndex)
                            {
                                auto const word = words.GetAt(wordIndex);
                                auto const rect = word.BoundingRect();
                                // 系统的方框是浮点物理像素，四舍五入到整数格：截断会让整行的框系统性偏左偏上。
                                // 送进去的是放大过的那一张，所以按同一个倍率折回去，框要与用户看到的原图同系
                                const auto  fold = [](const float value) { return static_cast<int>(std::lround(value / static_cast<float>(enlargeFactor))); };
                                const QRect box(fold(rect.X), fold(rect.Y), fold(rect.Width), fold(rect.Height));
                                line.words.push_back(Core::OcrWord{fromWinRTString(word.Text()), box});
                                line.boundingBox = line.boundingBox.isNull() ? box : line.boundingBox.united(box);
                            }
                            if (!line.words.isEmpty())
                            {
                                mapped.lines.push_back(std::move(line));
                            }
                        }
                        // averageConfidence 留空：Windows.Media.Ocr 不给逐词置信度，界面与导出各按「没有这一路读数」显示
                        return mapped;
                    } catch (const winrt::hresult_error &failure)
                    {
                        return std::unexpected(Base::Error{
                                Base::ErrorCode::InternalError,
                                GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("系统识别这一张图失败（{}，错误码 0x{}）。请重试一次，或改用 Tesseract 后端"),
                                                             fromWinRTString(failure.message()).toStdString(), std::format("{:08X}", static_cast<uint32_t>(failure.code())))});
                    }
                });
    }

} // namespace GyanisScribe::Plugins
