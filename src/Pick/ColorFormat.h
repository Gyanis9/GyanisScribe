/**
 * @file ColorFormat.h
 * @brief 取色器的颜色写法：HEX、RGB 与 HSL 三种，以及它们的存储标记
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QColor>
#include <QString>
#include <optional>

#include <string_view>

namespace GyanisScribe::Pick
{

    /**
     * @brief 一种颜色写法
     * @details 被用例钉住的枚举新成员只能追加在末尾。
     */
    enum class ColorFormat
    {
        Hex, ///< #RRGGBB
        Rgb, ///< rgb(r, g, b)
        Hsl, ///< hsl(h, s%, l%)
    };

    /**
     * @brief 写法的存储标记
     * @param format 写法
     * @return 写进配置的短标记，区分大小写
     */
    [[nodiscard]] std::string_view colorFormatToken(ColorFormat format);

    /**
     * @brief 写法的界面名
     * @param format 写法
     * @return 下拉框与提示里出现的那三个字（HEX / RGB / HSL）
     */
    [[nodiscard]] QString colorFormatLabel(ColorFormat format);

    /**
     * @brief 下拉框序号对应的写法
     * @param index 下拉框序号
     * @return 写法；序号越界返回空，调用方跳过而不是写坏配置
     */
    [[nodiscard]] std::optional<ColorFormat> colorFormatAt(int index);

    /**
     * @brief 写法在下拉框里的序号
     * @param format 写法
     * @return 序号
     */
    [[nodiscard]] int indexOfColorFormat(ColorFormat format);

    /**
     * @brief 下拉框的全部项名
     * @return 与 colorFormatAt 同序的界面名列表
     */
    [[nodiscard]] QStringList colorFormatLabels();

    /**
     * @brief 把配置里的标记读回写法
     * @param token 存储标记
     * @return 写法；不认识时返回错误并列出可用标记
     */
    [[nodiscard]] Core::Expected<ColorFormat> parseColorFormat(std::string_view token);

    /**
     * @brief 把配置里存的标记读回写法，坏值按 HEX 顶上
     * @param token 存储标记，来自设置项
     * @param complaint 读不出时的中文原因，供调用方记日志；成功时清空
     * @return 可用的写法
     * @details 两个调用点（屏幕取色与贴图取色）共用这一处回落：各自抄一份时，
     *          一边改了默认写法另一边不会跟着动，用户在两处取到的颜色写法就会不一样。
     */
    [[nodiscard]] ColorFormat colorFormatOrDefault(std::string_view token, std::string &complaint);

    /**
     * @brief 按写法把颜色排成一串可复制的文本
     * @param color 颜色
     * @param format 写法
     * @return 例如 #3C78C8、rgb(60, 120, 200)、hsl(214, 56%, 51%)
     * @details HSL 的角度与百分数都四舍五入到整数：小数位再多也不比一格的色差更有信息，
     *          而复制出去粘进 CSS 时带小数反而要被再裁一次。
     */
    [[nodiscard]] QString formatColor(const QColor &color, ColorFormat format);

} // namespace GyanisScribe::Pick
