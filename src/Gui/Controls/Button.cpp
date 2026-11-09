#include "Gui/Controls/Button.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Theme/ControlPalette.h"
#include "Gui/Theme/DesignTokens.h"

#include <QBrush>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionToolButton>

namespace GyanisScribe::Gui
{
    namespace
    {
        /**
         * @brief 把无效填充色翻成不铺底
         * @param color ControlPalette 给出的填充色
         * @return 有效色按实心刷；无效色给 NoBrush
         * @note QColor 直接隐式转 QBrush 时总是 SolidPattern，无效色会被画成黑块
         */
        QBrush fillBrush(const QColor &color)
        {
            return color.isValid() ? QBrush{color} : QBrush{Qt::NoBrush};
        }
    } // namespace

    Button::Button(QWidget *parent) : QPushButton(parent)
    {
        setProperty("squircle", true);
    }

    Button::Button(const QString &text, QWidget *parent) : QPushButton(text, parent)
    {
        setProperty("squircle", true);
    }

    void Button::paintEvent(QPaintEvent *)
    {
        QPainter painter{this};
        const auto colors = buttonColors(activePalette(), activeThemeKind(), isDefault(), isEnabled(), underMouse(), isDown(), hasFocus(),
                                         usesInlineFocusBorder(*this));
        const QPen pen    = colors.borderWidth > 0 ? QPen{colors.border, static_cast<qreal>(colors.borderWidth)} : QPen{Qt::NoPen};
        Core::paintContinuousCorner(painter, QRectF{rect()}, Core::cornerShapeFor(Core::CornerRole::Control), fillBrush(colors.fill), pen);

        QStyleOptionButton option;
        initStyleOption(&option);
        style()->drawControl(QStyle::CE_PushButtonLabel, &option, &painter, this);
    }

    ToolButton::ToolButton(QWidget *parent) : QToolButton(parent)
    {
        setProperty("squircle", true);
    }

    void ToolButton::paintEvent(QPaintEvent *)
    {
        QPainter painter{this};
        const auto colors = toolButtonColors(activePalette(), activeThemeKind(), isChecked(), isEnabled(), underMouse(), isDown(), hasFocus(),
                                             usesInlineFocusBorder(*this));
        const QPen pen    = colors.borderWidth > 0 ? QPen{colors.border, static_cast<qreal>(colors.borderWidth)} : QPen{Qt::NoPen};
        Core::paintContinuousCorner(painter, QRectF{rect()}, Core::cornerShapeFor(Core::CornerRole::Compact), fillBrush(colors.fill), pen);

        QStyleOptionToolButton option;
        initStyleOption(&option);
        style()->drawControl(QStyle::CE_ToolButtonLabel, &option, &painter, this);

        if (!option.features.testFlag(QStyleOptionToolButton::HasMenu))
        {
            return;
        }
        // 菜单指示器只有 CC_ToolButton 会画：把面板、焦点边、文字与图标都关掉，只留右下角那枚箭头；
        // 走样式引擎而不是自己画 PE_IndicatorArrowDown，样式表的 ::menu-indicator（image: none）才管得住它
        option.subControls = QStyle::SC_None;
        option.state &= ~QStyle::State_HasFocus;
        option.text.clear();
        option.icon = QIcon{};
        style()->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, this);
    }
} // namespace GyanisScribe::Gui
