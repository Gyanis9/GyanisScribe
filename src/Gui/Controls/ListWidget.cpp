#include "Gui/Controls/ListWidget.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Controls/ItemDelegate.h"
#include "Gui/Theme/CornerStyle.h"
#include "Gui/Theme/DesignTokens.h"

#include <QEvent>
#include <QPainter>
#include <QPen>

namespace GyanisScribe::Gui
{
    ListWidget::ListWidget(QWidget *parent) : QListWidget(parent)
    {
        setItemDelegate(new ItemDelegate{this});
        // 容器底与描边都由本类画：原生面板框会在圆角外补一圈方角线
        setFrameShape(QFrame::NoFrame);
        // 控件与视口都不许再铺系统底色，否则圆角外的四个角会露成方块；
        // WA_TranslucentBackground 让本类自己决定哪一块上色，父级面板从圆角外透出来
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        viewport()->setAutoFillBackground(false);
    }

    void ListWidget::setSurfaceVisible(const bool visible)
    {
        if (m_surfaceVisible == visible)
        {
            return;
        }
        m_surfaceVisible = visible;
        update();
    }

    bool ListWidget::event(QEvent *event)
    {
        if (event->type() == QEvent::Paint)
        {
            paintSurface();
        }
        return QListWidget::event(event);
    }

    void ListWidget::paintSurface()
    {
        if (!m_surfaceVisible)
        {
            return;
        }
        QPainter   painter{this};
        const QPen border =
                activeThemeKind() == ThemeKind::HighContrast ? QPen{activePalette().borderStrong, static_cast<qreal>(themeBorderWidth(ThemeKind::HighContrast))} : QPen{Qt::NoPen};
        Core::paintContinuousCorner(painter, QRectF{rect()}, Core::cornerShapeFor(Core::CornerRole::Panel), activePalette().surface, border);
    }
} // namespace GyanisScribe::Gui
