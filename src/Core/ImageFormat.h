/**
 * @file ImageFormat.h
 * @brief 成品图片的保存格式词表：档位、配置里的取值、文件名后缀与质量档
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QString>

#include <array>
#include <string_view>
#include <vector>

namespace GyanisScribe::Core
{

    /**
     * @brief 成品图片的保存格式
     * @details 只列 Qt 的图像插件在两边平台都编得进来的那三种：PNG 无损、JPG 与 WebP 用来把
     *          照片类的截图压小。BMP/GIF/TIFF 这些能写但没人拿截图当它们用，列进设置页就是噪声。
     */
    enum class ImageFormat
    {
        Png,  ///< 无损，带 alpha；默认档
        Jpeg, ///< 有损，压照片最省
        Webp, ///< 有损，同画质比 JPG 更小，且支持透明
    };

    /// 一档保存格式：存进配置的 token、设置页标签，与文件名后缀（三者必须同解）
    struct ImageFormatChoice
    {
        ImageFormat      format;    ///< 这一档
        std::string_view token;     ///< 配置里存的值，词表在这里唯一
        std::string_view label;     ///< 设置页上的标签，界面按 UTF-8 取
        std::string_view extension; ///< 文件名后缀，也是交给 Qt 的格式名
    };

    /// 全部格式档位，顺序就是设置页下拉的顺序
    inline constexpr std::array<ImageFormatChoice, 3> kImageFormatChoices{
            ImageFormatChoice{ImageFormat::Png, "Png", "PNG（无损）", "png"},
            ImageFormatChoice{ImageFormat::Jpeg, "Jpeg", "JPG", "jpg"},
            ImageFormatChoice{ImageFormat::Webp, "Webp", "WebP", "webp"},
    };

    /**
     * @brief 取一档格式存在配置里的那个值
     * @param format 目标档位
     * @return token；词表外的枚举值返回空，由调用方决定怎么报
     */
    [[nodiscard]] std::string_view imageFormatToken(ImageFormat format) noexcept;

    /**
     * @brief 取一档格式的文件名后缀
     * @param format 目标档位
     * @return 小写后缀（不带点）；词表外的枚举值退回 png
     */
    [[nodiscard]] std::string_view imageFormatExtension(ImageFormat format) noexcept;

    /**
     * @brief 解析配置里存的保存格式
     * @param stored 配置读出的文本
     * @return 对应档位；读不出时返回 InvalidArgument 并列出可取的几个值
     * @details 不替用户猜一档：猜错就是把同一批成品按另一种压缩写出去，事后无从找回原样。
     */
    [[nodiscard]] Core::Expected<ImageFormat> parseImageFormat(const QString &stored);

    /**
     * @brief 这台机器的 Qt 真写得出来的格式档位
     * @return 按词表顺序筛过一遍的档位；PNG 编在 Qt6Gui 里，所以至少有一条
     * @details 图像编码器是插件：容器里那份 Qt 6.4 没有 WebP 的写插件，Windows 那份 6.11 有。
     *          设置页摆出写不出来的那一档，就是让用户选一次、失败一次，还拿到一句
     *          「检查目录能不能写」的错话。
     */
    [[nodiscard]] std::vector<ImageFormat> writableImageFormats();

    /**
     * @brief 这一档在本机写不写得出来
     * @param format 目标档位
     * @return 可写为真
     */
    [[nodiscard]] bool isImageFormatWritable(ImageFormat format);

    /// 有损档的质量下限：再低就只剩块状噪声，没人会要
    inline constexpr int kMinimumImageQuality = 10;

    /// 有损档的质量上限，也是 Qt 认的最大值
    inline constexpr int kMaximumImageQuality = 100;

    /// 没填那一档取的质量
    inline constexpr int kDefaultImageQuality = 95;

    /**
     * @brief 校验配置里存的质量档
     * @param stored 配置读出的整数
     * @return 在区间内时返回原值；越界返回 InvalidArgument，文案点名可取区间
     * @details 越界不夹取：把 0 悄悄当成「按默认」会让用户以为压得不够狠而实际压到最低。
     */
    [[nodiscard]] Core::Expected<int> parseImageQuality(int stored);

} // namespace GyanisScribe::Core
