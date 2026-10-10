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
#include <QPalette>
#include <QWidget>

#include <array>
#include <cstddef>
#include <optional>

namespace GyanisScribe::Ui
{

    /// 截图遮罩用的半透明黑：压得住背景，又不让用户看不清自己在框什么
    inline constexpr QColor kCaptureMaskColor{0, 0, 0, 120};

    /// 选区边框与焦点圈的主色：遮罩压在任意画面上，这一支不随主题走，只看「深浅两种背景上都够重」
    inline constexpr QColor kAccentColor{43, 103, 216};

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

    /// 线宽滑块的固定宽度：滑块跟着布局拉伸会让「四档阶梯」的刻度间距变得忽宽忽窄
    inline constexpr int kToolbarSliderWidth = 64;

    /// 选区边框宽度：1-2 像素之间取主色描边那一条
    inline constexpr int kSelectionBorderWidth = 2;

    /**
     * @brief 录屏光标高亮圈的半径，逻辑像素
     * @details 取 4/8/12/16/24 阶梯里的 16 这一档：再小会被任务栏一类密集像素吃掉，再大就挡住被指的内容。
     *          半径按逻辑像素给，物理像素由乘缩放因子得到，这样 200% 缩放的屏上圈的大小与视觉一致。
     */
    inline constexpr int kCursorHighlightRadius = 16;

    /// 录屏光标高亮的主圈色，用主色（浅色与深色画面上都满足非文本对比）
    inline constexpr QColor kCursorHighlightColor = kAccentColor;

    /// 录屏光标高亮的外圈衬色：半透明白压在深色画面上才看得出圈的存在
    inline constexpr QColor kCursorHighlightHaloColor{255, 255, 255, 210};

    /// 主圈的描边宽度，逻辑像素
    inline constexpr int kCursorHighlightPenWidth = 2;

    /// 衬圈的描边宽度，逻辑像素：比主圈宽才会在两侧都露出白边
    inline constexpr int kCursorHighlightHaloPenWidth = 5;

    /**
     * @brief 一套主题的全部语义色
     * @details 绘制代码只认识这里的角色名，不认识具体十六进制；界面样式（Light.qss / Dark.qss）
     *          用同一批色值写规则，用例逐条比对两边同解，色值因此只可能有一处真源。
     *          每个字段都必须有真实消费方：icon 由 IconProvider 取用，success 与 danger 由 Toast 取用，
     *          其余都进 QSS（这三者都不在 QSS 里，用例按这张名单核对）。
     */
    struct ThemePalette
    {
        QColor windowBackground; ///< 窗口底色，QSS 里顶层窗口的背景
        QColor surface;          ///< 面板底色：卡片、工具栏、列表项、弹层
        QColor text;             ///< 正文色，对 windowBackground 与 surface 都要满足 AA
        QColor secondaryText;    ///< 次要文本色（说明行、读数），同样要满足 AA
        QColor border;           ///< 1px 分隔与卡片描边，用中性语义色，不用纯黑或纯白
        QColor accent;           ///< 主色：焦点圈、主按钮填充、选中项描边
        QColor onAccent;         ///< 压在主色上的文字色
        QColor selection;        ///< 文本选区底色，正文色压在其上仍要读得出
        QColor icon;             ///< 图标描边色，替换 SVG 里的 currentColor
        QColor success;          ///< 成功语义色，Toast 与状态文字用
        QColor danger;           ///< 危险语义色，出错提示与删除类动作的强调色
        QColor mutedFill;        ///< 静默底：进度槽、输入类控件的禁用底、次级面板
        QColor hover;            ///< 中性悬停底（按钮、列表项、菜单项）
        QColor pressed;          ///< 中性按下底，比 hover 再重一档
        QColor accentHover;      ///< 主按钮悬停底：填充色的加深档，不换成描边
        QColor accentSoft;       ///< 选中底：导航当前项、勾选的工具按钮，配 text 与 accent 同时可读
        QColor borderStrong;     ///< 输入类控件的默认描边，比 border 重一档，好让「能填的」一眼分得出
    };

    /// 浅色主题的语义色
    inline constexpr ThemePalette kLightPalette{
            QColor{244, 245, 247}, // windowBackground
            QColor{255, 255, 255}, // surface
            QColor{27, 31, 36},    // text
            QColor{73, 81, 98},    // secondaryText
            QColor{220, 224, 230}, // border
            kAccentColor,          // accent
            QColor{255, 255, 255}, // onAccent
            QColor{207, 225, 248}, // selection
            QColor{74, 82, 96},    // icon
            QColor{18, 105, 60},   // success
            QColor{179, 38, 30},   // danger
            QColor{237, 239, 242}, // mutedFill
            QColor{239, 241, 244}, // hover
            QColor{227, 231, 236}, // pressed
            QColor{36, 88, 189},   // accentHover
            QColor{230, 238, 251}, // accentSoft
            QColor{195, 201, 210}, // borderStrong
    };

    /// 深色主题的语义色：底色不是把浅色取反，而是各自另给一档；主色在这一档要更亮才压得住
    inline constexpr ThemePalette kDarkPalette{
            QColor{23, 25, 29},    // windowBackground
            QColor{31, 34, 40},    // surface
            QColor{232, 234, 237}, // text
            QColor{174, 183, 196}, // secondaryText
            QColor{56, 62, 72},    // border
            QColor{78, 139, 245},  // accent
            QColor{11, 18, 32},    // onAccent：这一档主色偏亮，压深字才够 AA（Fluent 深色同做法）
            QColor{44, 78, 119},   // selection
            QColor{201, 207, 216}, // icon
            QColor{92, 203, 134},  // success
            QColor{244, 121, 126}, // danger
            QColor{42, 46, 54},    // mutedFill
            QColor{44, 49, 58},    // hover
            QColor{53, 59, 70},    // pressed
            QColor{107, 160, 248}, // accentHover
            QColor{30, 51, 82},    // accentSoft
            QColor{74, 82, 94},    // borderStrong
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
            QColor{255, 255, 255}, // mutedFill：这一档不靠底色分层次，靠 2px 描边
            QColor{224, 224, 224}, // hover
            QColor{200, 200, 200}, // pressed
            QColor{8, 68, 158},    // accentHover
            QColor{214, 228, 251}, // accentSoft
            QColor{0, 0, 0},       // borderStrong
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
     * @brief 把语义色摊成 Qt 的应用调色板
     * @param palette 已经解算过档位的语义色
     * @return 可直接交给 QApplication::setPalette 的调色板
     * @details 样式表管不到由原生风格绘制的部分（勾选的勾、微调框的箭头、表头、占位文字、禁用文字），
     *          那些走 QPalette 的角色。只落样式表不落调色板时，深色档会画出一批浅色原生件——
     *          这就是这一份映射存在的理由，两套上色路径必须同源于 ThemePalette。
     */
    [[nodiscard]] inline QPalette buildApplicationPalette(const ThemePalette &palette)
    {
        QPalette result;
        result.setColor(QPalette::Window, palette.windowBackground);
        result.setColor(QPalette::Base, palette.surface);
        result.setColor(QPalette::AlternateBase, palette.mutedFill);
        result.setColor(QPalette::Button, palette.surface);
        result.setColor(QPalette::Text, palette.text);
        result.setColor(QPalette::WindowText, palette.text);
        result.setColor(QPalette::ButtonText, palette.text);
        result.setColor(QPalette::Highlight, palette.accentSoft);
        result.setColor(QPalette::HighlightedText, palette.text);
        result.setColor(QPalette::PlaceholderText, palette.secondaryText);
        result.setColor(QPalette::Link, palette.accent);
        result.setColor(QPalette::ToolTipBase, palette.surface);
        result.setColor(QPalette::ToolTipText, palette.text);
        // 禁用态留一档「读得出但不是可操作」的灰，而不是让原生风格自己按亮度算
        result.setColor(QPalette::Disabled, QPalette::Text, palette.secondaryText);
        result.setColor(QPalette::Disabled, QPalette::WindowText, palette.secondaryText);
        result.setColor(QPalette::Disabled, QPalette::ButtonText, palette.secondaryText);
        result.setColor(QPalette::Disabled, QPalette::Highlight, palette.mutedFill);
        result.setColor(QPalette::Disabled, QPalette::HighlightedText, palette.secondaryText);
        return result;
    }

    /**
     * @brief 文字角色：界面里仅有的四种排版档位
     * @details 需求 §九 要「克制、工具感」，可读性靠层级而不是字号堆叠，所以档位刻意少。
     *          取值走点（pt）而不是像素：像素在 QSS 里不随系统 DPI 缩放，200% 缩放的屏上会一起变小。
     */
    enum class TextRole
    {
        PageTitle, ///< 页标题：一类的名字，每页只有一处
        Body,      ///< 正文、控件文字与说明行——说明行只靠次要色弱化，不再另开更小的一档
        Metric,    ///< 时长、计数这类数字读数，走等宽数字以免读数跳动
    };

    /**
     * @brief 各文字角色的点值；与 TextRole 一一对应，改动只许发生在这一处
     * @details 刻意没有「比正文更小」的一档：小字号与次要色是两个独立的弱化手段，同时用就把
     *          「灰一点」变成「看不清」（浅色档实测 9pt 压 5.7:1 的灰字，屏幕远一点就读不出）。
     *          层级靠颜色与字重表达，字号只留正文与标题两级。
     */
    inline constexpr int kTextRolePointSizePageTitle = 15;
    inline constexpr int kTextRolePointSizeBody      = 10;
    inline constexpr int kTextRolePointSizeMetric    = 13;

    /**
     * @brief 按角色给控件设字号与字重
     * @param widget 目标控件，字体随它继承给子控件
     * @param role 文字角色
     * @details 标题走半粗（DemiBold）而不是加粗：中文在 Bold 下会糊成一团黑块。
     *          Metric 另开等宽数字，时长从 9:59 跳到 10:00 时后面那行不该左右抖。
     */
    inline void applyTextRole(QWidget &widget, const TextRole role)
    {
        auto font = widget.font();
        switch (role)
        {
            case TextRole::PageTitle:
                font.setPointSize(kTextRolePointSizePageTitle);
                font.setWeight(QFont::DemiBold);
                break;
            case TextRole::Body:
                font.setPointSize(kTextRolePointSizeBody);
                break;
            case TextRole::Metric:
                font.setPointSize(kTextRolePointSizeMetric);
                font.setWeight(QFont::DemiBold);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
                // OpenType 的 tnum 特性：Segoe UI 与 Noto Sans 都带；缺这个特性的字体只是拿不到等宽，不影响可读。
                // setFeature 是 Qt 6.7 才有的接口（容器那份 6.4 实测报 no member named），按版本分岔：
                // 拿不到等宽数字时读数只是会抖，不值得为此放弃整档
                font.setFeature(QFont::Tag("tnum"), 1);
#endif
                break;
        }
        widget.setFont(font);
    }

    /// 间距阶梯：4 的倍数，界面里出现的每一个间距都取自这几档
    inline constexpr int kSpacingTiny   = 4;
    inline constexpr int kSpacingSmall  = 8;
    inline constexpr int kSpacingMedium = 12;
    inline constexpr int kSpacingLarge  = 16;

    /// 一行设置项里字段的最大宽度：路径与下拉拉到整窗宽时，「这一格填什么」就看不出来了
    inline constexpr int kFieldMaximumWidth = 380;

    /// 页顶说明行的最大宽度：整窗宽的一行长句读起来要回头数，收到这一档就按行折
    inline constexpr int kIntroMaximumWidth = 420;

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
            QColor{43, 103, 216}, // 蓝，与主色同值
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


} // namespace GyanisScribe::Ui
