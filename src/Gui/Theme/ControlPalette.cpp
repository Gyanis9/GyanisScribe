#include "Gui/Theme/ControlPalette.h"

#include "Gui/Theme/CornerStyle.h"

namespace GyanisScribe::Gui
{
    namespace
    {
        /**
         * @brief 取聚焦时的描边；不该有可见边时返回空
         * @param palette 当前主题的语义色
         * @param kind 当前主题档
         * @param focused 是否持有焦点
         * @param inlineFocus 是否由控件自绘焦点边
         * @return 空表示不画边；高对比度档任何时候有焦点都画主色边
         */
        ControlColors focusedBorder(const ThemePalette &palette, const ThemeKind kind, const bool focused, const bool inlineFocus)
        {
            if (!focused || (kind != ThemeKind::HighContrast && !inlineFocus))
            {
                return {};
            }
            return {QColor{}, palette.accent, themeBorderWidth(kind)};
        }

        /**
         * @brief 补上高对比度档的静息底边
         * @param colors 已按状态算好的颜色，就地补边
         * @param palette 当前主题的语义色
         * @param kind 当前主题档
         * @note 高对比度档的控件一律 2px 纯黑边，不靠底色分层次；焦点边已在调用前算好，
         *       这里只补 borderWidth 仍为 0 的那一档，主色焦点边因此不会被覆盖
         */
        void applyHighContrastBaseBorder(ControlColors &colors, const ThemePalette &palette, const ThemeKind kind)
        {
            if (colors.borderWidth == 0 && kind == ThemeKind::HighContrast)
            {
                colors.border      = palette.borderStrong;
                colors.borderWidth = themeBorderWidth(kind);
            }
        }
    } // namespace

    ControlColors buttonColors(const ThemePalette &palette, const ThemeKind kind, const bool isDefault, const bool enabled, const bool hover,
                               const bool down, const bool focused, const bool inlineFocus)
    {
        ControlColors colors;
        if (!enabled)
        {
            colors.fill = palette.mutedFill;
        } else if (isDefault)
        {
            colors.fill = (hover || down) ? palette.accentHover : palette.accent;
        } else
        {
            colors.fill = down ? palette.pressed : (hover ? palette.hover : palette.controlFill);
        }
        const auto border  = focusedBorder(palette, kind, focused, inlineFocus);
        colors.border      = border.border;
        colors.borderWidth = border.borderWidth;
        applyHighContrastBaseBorder(colors, palette, kind);
        return colors;
    }

    ControlColors toolButtonColors(const ThemePalette &palette, const ThemeKind kind, const bool checked, const bool enabled, const bool hover,
                                   const bool down, const bool focused, const bool inlineFocus)
    {
        ControlColors colors;
        if (enabled)
        {
            if (checked)
            {
                colors.fill = palette.accentSoft;
            } else if (down)
            {
                colors.fill = palette.pressed;
            } else if (hover)
            {
                colors.fill = palette.hover;
            }
        }
        const auto border  = focusedBorder(palette, kind, focused, inlineFocus);
        colors.border      = border.border;
        colors.borderWidth = border.borderWidth;
        return colors;
    }

    ControlColors inputColors(const ThemePalette &palette, const ThemeKind kind, const bool enabled, const bool readOnly, const bool focused,
                              const bool inlineFocus)
    {
        ControlColors colors;
        colors.fill        = (!enabled || readOnly) ? palette.mutedFill : palette.controlFill;
        const auto border  = focusedBorder(palette, kind, focused, inlineFocus);
        colors.border      = border.border;
        colors.borderWidth = border.borderWidth;
        applyHighContrastBaseBorder(colors, palette, kind);
        return colors;
    }

    bool usesInlineFocusBorder(const QWidget &widget)
    {
        for (const auto *candidate = widget.parentWidget(); candidate != nullptr; candidate = candidate->parentWidget())
        {
            if (candidate->objectName() == QStringLiteral("AnnotationToolbar"))
            {
                return true;
            }
        }
        return false;
    }
} // namespace GyanisScribe::Gui
