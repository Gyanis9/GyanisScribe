/**
 * @file ColorFormat.h
 * @brief 取色器的颜色写法：HEX、RGB、HSL、HSV、HSI 与十进制，以及它们的存储标记
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

#include <cstddef>
#include <string_view>

namespace GyanisScribe::Pick
{

    /**
     * @brief 一种颜色写法
     * @details 被用例钉住的枚举新成员只能追加在末尾。
     */
    enum class ColorFormat
    {
        Hex,     ///< #RRGGBB
        Rgb,     ///< rgb(r, g, b)
        Hsl,     ///< hsl(h, s%, l%)，CSS 那一套；s 是 HSL 饱和
        Hsv,     ///< hsv(h, s%, v%)：设计工具的取色盘多用这一套（s 是 HSV 饱和）
        Hsi,     ///< hsi(h, s%, i%)：i 是三分量均值，色相与 HSL/HSV 同源
        Decimal, ///< 十进制三元组「r, g, b」，粘进代码当数组元素用
    };

    /// 全部写法的条数：轮换与校验共用这一个数，别在别处再写一遍字面量
    inline constexpr std::size_t kColorFormatCount = 6;

    /**
     * @brief 写法的存储标记
     * @param format 写法
     * @return 写进配置的短标记，区分大小写
     */
    [[nodiscard]] std::string_view colorFormatToken(ColorFormat format);

    /**
     * @brief 写法的界面名
     * @param format 写法
     * @return 下拉框与提示里出现的那几个字（HEX / RGB / HSL / HSV / HSI / 十进制）
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
     * @brief HEX 里字母那几位用大写还是小写
     * @details 单列一档而不是「按语言/按写法」推：粘进 CSS 与设计工具时两种形状都有人用，
     *          而这不是能从别处读出来的偏好——只有用户自己知道对面收哪一种。
     */
    enum class HexCasing
    {
        Uppercase, ///< #3C78C8：本仓此前的唯一写法
        Lowercase, ///< #3c78c8：CSS 与不少前端工具里更常见的形状
    };

    /**
     * @brief 按写法把颜色排成一串可复制的文本
     * @param color 颜色
     * @param format 写法
     * @param casing HEX 字母那几位的大小写；只有写法是 HEX 时才起作用
     * @return 例如 #3C78C8、rgb(60, 120, 200)、hsl(214, 56%, 51%)、hsv(214, 70%, 78%)、
     *         hsi(214, 53%, 50%)、60, 120, 200
     * @details 角度与百分数都四舍五入到整数：小数位再多也不比一格的色差更有信息，
     *          而复制出去粘进 CSS 时带小数反而要被再裁一次。
     */
    [[nodiscard]] QString formatColor(const QColor &color, ColorFormat format, HexCasing casing);

    /**
     * @brief 配置里那一格「用大写」开关对应的字母档
     * @param uppercase 配置读出来的布尔值
     * @return 该用的字母档
     * @details 读法只有这一处：三处现读的地方各写一遍三元，就会有人漏掉一处而让同一个颜色在不同界面里
     *          写出两种形状。
     */
    [[nodiscard]] HexCasing hexCasingOf(bool uppercase);

} // namespace GyanisScribe::Pick
