/**
 * @file DesignTokens.h
 * @brief 设计令牌：颜色、字号、间距、圆角与动效时长的唯一出处
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Ui/ThemeKind.h"

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

    /**
     * @brief 一套主题的全部语义色
     * @details 绘制代码只认识这里的角色名，不认识具体十六进制；界面样式（Light.qss / Dark.qss）
     *          用同一批色值写规则，用例逐条比对两边同解，色值因此只可能有一处真源。
     *          每个字段都必须有真实消费方，除 icon 由 IconProvider 取用外，其余都进 QSS。
     */
    struct ThemePalette
    {
        QColor windowBackground; ///< 窗口底色，QSS 里 QWidget 与对话框的背景
        QColor surface;          ///< 面板底色：工具栏、分组框、列表项
        QColor text;             ///< 正文色，对 windowBackground 与 surface 都要满足 AA
        QColor secondaryText;    ///< 次要文本色（提示、读数），同样要满足 AA
        QColor border;           ///< 1px 边框色，用中性语义色，不用纯黑或纯白
        QColor accent;           ///< 主色：焦点圈、主按钮填充，兼作 1.4.11 的非文本对比判据
        QColor onAccent;         ///< 压在主色上的文字色
        QColor selection;        ///< 文本选区底色，正文色压在其上仍要读得出
        QColor icon;             ///< 图标描边色，替换 SVG 里的 currentColor
    };

    /// 浅色主题的语义色
    inline constexpr ThemePalette kLightPalette{
            QColor{243, 244, 246}, // windowBackground
            QColor{255, 255, 255}, // surface
            QColor{27, 27, 31},    // text
            QColor{74, 80, 88},    // secondaryText
            QColor{201, 205, 211}, // border
            kAccentColor,          // accent
            QColor{255, 255, 255}, // onAccent
            QColor{191, 221, 245}, // selection
            QColor{58, 63, 69},    // icon
    };

    /// 深色主题的语义色：主色与浅色共用，只把底色、文字与选区翻过来
    inline constexpr ThemePalette kDarkPalette{
            QColor{27, 29, 34},    // windowBackground
            QColor{36, 38, 44},    // surface
            QColor{233, 235, 238}, // text
            QColor{180, 185, 192}, // secondaryText
            QColor{60, 64, 72},    // border
            kAccentColor,          // accent
            QColor{255, 255, 255}, // onAccent
            QColor{30, 74, 104},   // selection
            QColor{212, 215, 219}, // icon
    };

    /**
     * @brief 按已定档的主题取语义色
     * @param themeKind 主题档，System 不该传进来（调用方应先解算成 Light 或 Dark）
     * @return 对应主题的调色板引用
     * @note System 按浅色返回：这是「解算失败」的兜底档，正常路径拿不到它，兜底分支由用例钉住
     */
    [[nodiscard]] constexpr const ThemePalette &paletteFor(ThemeKind themeKind)
    {
        return themeKind == ThemeKind::Dark ? kDarkPalette : kLightPalette;
    }

} // namespace GyanisScribe::Ui
