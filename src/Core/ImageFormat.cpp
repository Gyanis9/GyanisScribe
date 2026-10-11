#include "Core/ImageFormat.h"

#include "Core/Choices.h"
#include "Core/Text.h"

#include <QImageWriter>

#include <algorithm>
#include <expected>

namespace GyanisScribe::Core
{

    std::string_view imageFormatToken(const ImageFormat format) noexcept
    {
        for (const auto &choice: kImageFormatChoices)
        {
            if (choice.format == format)
            {
                return choice.token;
            }
        }
        return {};
    }

    std::string_view imageFormatExtension(const ImageFormat format) noexcept
    {
        for (const auto &choice: kImageFormatChoices)
        {
            if (choice.format == format)
            {
                return choice.extension;
            }
        }
        // 枚举里漏了档就退回默认档：命名规则不能因为少一行表就写出一个没有后缀的文件
        return "png";
    }

    Core::Expected<ImageFormat> parseImageFormat(const QString &stored)
    {
        const auto wanted = stored.toStdString();
        for (const auto &choice: kImageFormatChoices)
        {
            if (wanted == choice.token)
            {
                return choice.format;
            }
        }
        // 猜一档就是把同一批成品按另一种压缩写出去，事后无从找回原样
        return std::unexpected(Core::unknownTokenError(kImageFormatChoices, stored, Core::uiTextUtf8("保存格式"), Core::uiTextUtf8("截图页"), "Save/ImageFormat"));
    }

    std::vector<ImageFormat> writableImageFormats()
    {
        const auto               supported = QImageWriter::supportedImageFormats();
        std::vector<ImageFormat> writable;
        for (const auto &choice: kImageFormatChoices)
        {
            // 插件报的是格式名（"png"、"jpg"、"webp"），与词表里的后缀同一套写法
            if (supported.contains(QByteArray(choice.extension.data(), static_cast<qsizetype>(choice.extension.size()))))
            {
                writable.push_back(choice.format);
            }
        }
        return writable;
    }

    bool isImageFormatWritable(const ImageFormat format)
    {
        const auto writable = writableImageFormats();
        return std::find(writable.begin(), writable.end(), format) != writable.end();
    }

    Core::Expected<int> parseImageQuality(const int stored)
    {
        if (stored < kMinimumImageQuality || stored > kMaximumImageQuality)
        {
            return std::unexpected(Core::Error{
                    .code = Core::ErrorCode::InvalidArgument,
                    .message =
                            Core::uiFormat(Core::uiTextUtf8("保存质量：取值 {} 不在可取区间里，只能是 {} 到 {}。请在设置的截图页重选，或删掉 Save/ImageQuality 这一项回到默认档"),
                                           stored, kMinimumImageQuality, kMaximumImageQuality),
            });
        }
        return stored;
    }

} // namespace GyanisScribe::Core
