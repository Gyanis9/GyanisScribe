/**
 * @file DesignTokens.h
 * @brief 设计令牌：颜色、字号、间距、圆角与动效时长的唯一出处
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Record/CursorHighlight.h"
#include "Gui/Theme/ThemeKind.h"

#include <QColor>
#include <QPalette>
#include <QWidget>

#include <array>
#include <cstddef>
#include <optional>

namespace GyanisScribe::Gui
{

    /// 截图遮罩用的半透明黑：压得住背景，又不让用户看不清自己在框什么
    inline constexpr QColor kCaptureMaskColor{0, 0, 0, 120};

    /// 选区边框与焦点圈的主色：遮罩压在任意画面上，这一支不随主题走，只看「深浅两种背景上都够重」；
    /// 与浅色档 accent 同值（珍珠玻璃的蓝）
    inline constexpr QColor kAccentColor{35, 109, 233};

    /// 遮罩上的读数文本色：借珍珠玻璃的深色铭牌文字档（--plate-ink）
    inline constexpr QColor kCaptureHintTextColor{242, 245, 244};

    /// 选区读数文本的底衬色，保证读数压在任何画面上都读得出：借设计的深色铭牌底（--plate-bg 的 .9 档）
    inline constexpr QColor kCaptureHintBackgroundColor{21, 24, 26, 230};

    /// 十字辅助线色（虚线）
    inline constexpr QColor kCrosshairColor{160, 160, 160, 180};

    /// 遮罩上读数的字号，取自 12/13/14/16/20/24 的阶梯
    inline constexpr int kHintFontSize = 12;

    /// 读数文本四周的内边距，取自 4/8/12/16/24/32 的阶梯
    inline constexpr int kHintPadding = 4;

    /// 线宽滑块的固定宽度：滑块跟着布局拉伸会让「四档阶梯」的刻度间距变得忽宽忽窄
    inline constexpr int kToolbarSliderWidth = 64;

    /// 字体下拉的宽度上限：系统字体名可以很长，放任 sizeHint 会把整条工具栏撑出去
    inline constexpr int kToolbarFontComboWidth = 150;

    /// 选区边框宽度：1-2 像素之间取主色描边那一条
    inline constexpr int kSelectionBorderWidth = 2;

    /// 选区抓点小方块的边长，逻辑像素：要盖住那条一像素的边界，又不能大到挡住要看的内容
    inline constexpr int kSelectionGripEdge = 8;

    // 光标高亮的形状与配色住在录屏那边（Core/Record/CursorHighlight.h）；
    // 「主圈色必须跟着主色走」这条关系在这里钉死，两处各写一份就会分家
    // （QColor 不是 constexpr 可比，这条关系由 Gui 的用例钉住：TestDesignTokens.CursorHighlightColorFollowsTheAccent）

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

    /// 浅色主题的语义色：珍珠玻璃（scaffold-vue）浅色档的移植；
    /// 逐条对比度现算与设计原值的差异见换肤计划附录的冻结值表（仓外 GyanisScribe-Reports/UiRestylePlan.md）
    inline constexpr ThemePalette kLightPalette{
            QColor{246, 248, 247}, // windowBackground
            QColor{251, 252, 252}, // surface
            QColor{16, 20, 24},    // text
            QColor{73, 83, 88},    // secondaryText：设计原值 #596368 对底色只有 5.8:1，压深保色相到 7.4:1
            QColor{228, 231, 231}, // border
            kAccentColor,          // accent
            QColor{255, 255, 255}, // onAccent
            QColor{216, 229, 249}, // selection
            QColor{126, 136, 140}, // icon
            QColor{59, 125, 33},   // success：设计原值 #58A021 当文字只有 3.3:1，压深保色相
            QColor{202, 57, 50},   // danger：设计原值 #E0443B 当文字只有 4.2:1，压深保色相
            QColor{238, 242, 240}, // mutedFill
            QColor{242, 243, 243}, // hover
            QColor{236, 237, 238}, // pressed
            QColor{29, 97, 213},   // accentHover
            QColor{229, 238, 250}, // accentSoft
            QColor{214, 217, 218}, // borderStrong
    };

    /// 深色主题的语义色：同一套语义换材质（设计深色档的移植）
    inline constexpr ThemePalette kDarkPalette{
            QColor{18, 21, 23},    // windowBackground
            QColor{27, 31, 33},    // surface
            QColor{238, 242, 241}, // text
            QColor{176, 186, 189}, // secondaryText
            QColor{35, 37, 39},    // border
            QColor{76, 141, 255},  // accent
            QColor{11, 18, 32},    // onAccent：这一档主色偏亮，压白字只有 3.2:1，压深字才够 AA
            QColor{39, 57, 86},    // selection
            QColor{141, 151, 154}, // icon
            QColor{155, 227, 90},  // success
            QColor{255, 122, 113}, // danger
            QColor{21, 24, 26},    // mutedFill
            QColor{38, 42, 44},    // hover
            QColor{45, 49, 51},    // pressed
            QColor{61, 127, 242},  // accentHover
            QColor{35, 49, 69},    // accentSoft
            QColor{50, 53, 55},    // borderStrong
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

    /// 菜单文字的点值：托盘右键菜单与贴图右键菜单比正文小一档，行高才能收到 24px 左右
    inline constexpr int kMenuTextPointSize = 9;

    /// 一行设置项里字段的最大宽度：路径与下拉拉到整窗宽时，「这一格填什么」就看不出来了
    inline constexpr int kFieldMaximumWidth = 380;

    /// 设置页标签列的最小宽度：全族统一（取最长标签一页），翻页时字段列不再左右跳（界面审查 UI-01）
    inline constexpr int kSettingsLabelColumnWidth = 240;

    /// 页顶说明行的最大宽度：整窗宽的一行长句读起来要回头数，收到这一档就按行折
    inline constexpr int kIntroMaximumWidth = 420;

    // 标注默认色板住在 Core/Annotation/AnnotationPalette.h（kAnnotationPaletteColors）；
    // 其中「蓝」与主色同值这条关系由 Gui 的用例钉住（TestDesignTokens.AnnotationPaletteBlueFollowsTheAccent）

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


} // namespace GyanisScribe::Gui
