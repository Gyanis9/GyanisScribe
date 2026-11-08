#include "Gui/Controls/LineEdit.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Theme/ControlPalette.h"
#include "Gui/Theme/DesignTokens.h"

#include <QPainter>
#include <QPen>

namespace GyanisScribe::Gui
{
    LineEdit::LineEdit(QWidget *parent) : QLineEdit(parent)
    {
        setProperty("squircle", true);
    }

    void LineEdit::paintEvent(QPaintEvent *event)
    {
        {
            QPainter   painter{this};
            const auto colors = inputColors(activePalette(), activeThemeKind(), isEnabled(), isReadOnly(), hasFocus(), usesInlineFocusBorder(*this));
            const QPen pen    = colors.borderWidth > 0 ? QPen{colors.border, static_cast<qreal>(colors.borderWidth)} : QPen{Qt::NoPen};
            Core::paintContinuousCorner(painter, QRectF{rect()}, Core::cornerShapeFor(Core::CornerRole::Control), colors.fill, pen);
        }
        QLineEdit::paintEvent(event);
    }
} // namespace GyanisScribe::Gui
