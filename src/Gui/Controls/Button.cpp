#include "Gui/Controls/Button.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Theme/ControlPalette.h"
#include "Gui/Theme/DesignTokens.h"

#include <QPainter>
#include <QPen>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionToolButton>

namespace GyanisScribe::Gui
{
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
        Core::paintContinuousCorner(painter, QRectF{rect()}, Core::cornerShapeFor(Core::CornerRole::Control), colors.fill, pen);

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
        Core::paintContinuousCorner(painter, QRectF{rect()}, Core::cornerShapeFor(Core::CornerRole::Compact), colors.fill, pen);

        QStyleOptionToolButton option;
        initStyleOption(&option);
        style()->drawControl(QStyle::CE_ToolButtonLabel, &option, &painter, this);
    }
} // namespace GyanisScribe::Gui
