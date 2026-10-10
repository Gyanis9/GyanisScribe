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

#include <array>
#include <cstddef>
#include <optional>

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

    /// 工具栏的内边距与控件间距，同样取 4 这一档
    inline constexpr int kToolbarPadding = 4;

    /// 线宽滑块的固定宽度：滑块跟着布局拉伸会让「四档阶梯」的刻度间距变得忽宽忽窄
    inline constexpr int kToolbarSliderWidth = 64;

    /// 选区边框宽度：1-2 像素之间取主色描边那一条
    inline constexpr int kSelectionBorderWidth = 2;

    /**
     * @brief 一套主题的全部语义色
     * @details 绘制代码只认识这里的角色名，不认识具体十六进制；界面样式（Light.qss / Dark.qss）
     *          用同一批色值写规则，用例逐条比对两边同解，色值因此只可能有一处真源。
     *          每个字段都必须有真实消费方：icon 由 IconProvider 取用，success 与 danger 由 Toast 取用，
     *          其余都进 QSS（这三者都不在 QSS 里，用例按这张名单核对）。
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
        QColor success;          ///< 成功语义色，Toast 与状态文字用
        QColor danger;           ///< 危险语义色，出错提示与删除类动作的强调色
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
            QColor{21, 122, 66},   // success
            QColor{179, 38, 30},   // danger
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
            QColor{87, 199, 130},  // success
            QColor{242, 85, 90},   // danger
    };

    /**
     * @brief 高对比度主题的语义色
     * @details 刻意不用灰阶层级：次要文本与正文同为纯黑、边框为纯黑（浅色与深色主题那条
     *          「边框不用纯黑」在这里反过来是要求），靠 2px 边框与主色区分层级。
     *          选区用纯黄而不是主色系，是为了让纯黑正文压在选区上仍是最强对比的那一档。
     */
    inline constexpr ThemePalette kHighContrastPalette{
            QColor{255, 255, 255}, // windowBackground
            QColor{255, 255, 255}, // surface
            QColor{0, 0, 0},       // text
            QColor{0, 0, 0},       // secondaryText
            QColor{0, 0, 0},       // border
            QColor{11, 87, 208},   // accent
            QColor{255, 255, 255}, // onAccent
            QColor{255, 229, 0},   // selection
            QColor{0, 0, 0},       // icon
            QColor{0, 100, 0},     // success
            QColor{176, 0, 0},     // danger
    };

    /**
     * @brief 按已定档的主题取语义色
     * @param themeKind 主题档，System 不该传进来（调用方应先解算成 Light、Dark 或 HighContrast）
     * @return 对应主题的调色板引用
     * @note System 按浅色返回：这是「解算失败」的兜底档，正常路径拿不到它，兜底分支由用例钉住
     */
    [[nodiscard]] constexpr const ThemePalette &paletteFor(ThemeKind themeKind)
    {
        switch (themeKind)
        {
            case ThemeKind::Dark:
                return kDarkPalette;
            case ThemeKind::HighContrast:
                return kHighContrastPalette;
            case ThemeKind::Light:
            case ThemeKind::System:
                break;
        }
        return kLightPalette;
    }

    /**
     * @brief 标注默认色板
     * @details 需求第五节点名的八色（红橙黄绿蓝紫黑白），自定义色由工具栏的取色对话框追加，
     *          不在这里占位；消费方是标注工具栏的色块按钮，序号即按钮顺序。
     */
    inline constexpr std::array<QColor, 8> kAnnotationPalette{
            QColor{232, 62, 62},  // 红
            QColor{242, 141, 50}, // 橙
            QColor{240, 195, 60}, // 黄
            QColor{70, 180, 90},  // 绿
            QColor{0, 122, 204},  // 蓝，与主色同值
            QColor{150, 90, 200}, // 紫
            QColor{24, 24, 24},   // 黑
            QColor{252, 252, 252} // 白
    };

    /// 标注线宽阶梯，工具栏的滑块按这几档走，不允许连续取值
    inline constexpr std::array<int, 4> kAnnotationWidths{2, 4, 6, 8};
    /// 字号阶梯（需求第五节：12 / 13 / 14 / 16 / 20 / 24），工具栏的字号下拉只列这几档
    inline constexpr std::array<int, 6> kFontSizes{12, 13, 14, 16, 20, 24};

    /**
     * @brief 标注起点档位在阶梯表里的下标：线宽 4、字号 14
     * @details 工具栏的兜底档与设置页的显示档读同一个常量；两处各写一个下标，
     *          「设置页显示 2、新开的标注窗却按 4 起步」就没人看得出来。
     */
    inline constexpr std::size_t kDefaultAnnotationWidthStep = 1;
    /// 默认字号在字号阶梯里的下标，与上面同解
    inline constexpr std::size_t kDefaultAnnotationFontSizeIndex = 2;

    /**
     * @brief 在阶梯表里找回某个取值对应的下标
     * @tparam Size 阶梯档数
     * @param steps 阶梯表（值不重复）
     * @param value 要找回的取值，例如配置文件里存着的默认线宽
     * @return 命中的下标；取值不在阶梯里时返回空
     * @details 刻意不做「就近取一档」：配置里写着 5 而阶梯只有 2/4/6/8 时，悄悄按 4 或 6 用
     *          会让用户以为存的那一档生效了。返回空由调用方记一条日志再回落默认档。
     */
    template<std::size_t Size>
    [[nodiscard]] constexpr std::optional<int> stepIndexOf(const std::array<int, Size> &steps, const int value)
    {
        for (std::size_t index = 0; index < steps.size(); ++index)
        {
            if (steps[index] == value)
            {
                return static_cast<int>(index);
            }
        }
        return {};
    }

    /**
     * @brief 按序号取标注色板里的颜色
     * @param index 色板序号
     * @return 对应的颜色；序号越界时返回调用方可见的空色，由调用方决定是否报「取不到」
     * @note 越界返回空色而不是夹到最后一个：夹住会让工具栏少画一个色块也没人发现
     */
    [[nodiscard]] inline constexpr QColor annotationColorAt(std::size_t index)
    {
        return index < kAnnotationPalette.size() ? kAnnotationPalette[index] : QColor{};
    }

} // namespace GyanisScribe::Ui
