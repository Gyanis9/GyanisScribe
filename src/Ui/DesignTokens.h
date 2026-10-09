/**
 * @file DesignTokens.h
 * @brief 设计令牌：颜色、字号、间距、圆角与动效时长的唯一出处
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QColor>

namespace GyanisScribe::Ui
{

    /// 截图遮罩用的半透明黑：压得住背景，又不让用户看不清自己在框什么
    inline constexpr QColor kCaptureMaskColor{0, 0, 0, 120};

    /// 选区边框与焦点圈的主色（浅色与深色主题共用，两个主题下都满足对比度要求）
    inline constexpr QColor kAccentColor{0, 122, 204};

    /// 遮罩上的读数文本色
    inline constexpr QColor kCaptureHintTextColor{240, 240, 240};

    /// 选区读数文本的底衬色，保证读数压在任何画面上都读得出
    inline constexpr QColor kCaptureHintBackgroundColor{24, 24, 24, 200};

    /// 十字辅助线色（虚线）
    inline constexpr QColor kCrosshairColor{160, 160, 160, 180};

    /// 遮罩上读数的字号，取自 12/13/14/16/20/24 的阶梯
    inline constexpr int kHintFontSize = 12;

    /// 读数文本四周的内边距，取自 4/8/12/16/24/32 的阶梯
    inline constexpr int kHintPadding = 4;

    /// 选区边框宽度：1-2 像素之间取主色描边那一条
    inline constexpr int kSelectionBorderWidth = 2;

} // namespace GyanisScribe::Ui
