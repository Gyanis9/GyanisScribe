#include "Gui/Controls/TextEdit.h"

#include "Gui/Theme/ControlPalette.h"
#include "Gui/Theme/DesignTokens.h"

#include <QEvent>
#include <QPainter>
#include <QPen>
#include <QWidget>

namespace GyanisScribe::Gui
{
    TextEdit::TextEdit(QWidget *parent) : TextEdit(Core::CornerRole::Panel, parent)
    {
    }

    TextEdit::TextEdit(const Core::CornerRole role, QWidget *parent) : QTextEdit(parent), m_role(role)
    {
        setProperty("squircle", true);
        viewport()->setAutoFillBackground(false);
    }

    bool TextEdit::event(QEvent *event)
    {
        // 视口绘制走 QTextEdit::paintEvent（只画正文），控件自身的 Paint 才由这里铺底；
        // 在视口事件里往控件上落笔会被 Qt 丢掉，底必须在控件自己的绘制事件里画
        if (event->type() == QEvent::Paint)
        {
            QPainter   painter{this};
            const auto colors = inputColors(activePalette(), activeThemeKind(), isEnabled(), isReadOnly(), hasFocus(), usesInlineFocusBorder(*this));
            const QPen pen    = colors.borderWidth > 0 ? QPen{colors.border, static_cast<qreal>(colors.borderWidth)} : QPen{Qt::NoPen};
            Core::paintContinuousCorner(painter, QRectF{rect()}, Core::cornerShapeFor(m_role), colors.fill, pen);
        }
        return QTextEdit::event(event);
    }
} // namespace GyanisScribe::Gui
