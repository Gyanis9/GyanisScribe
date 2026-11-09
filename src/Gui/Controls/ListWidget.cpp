#include "Gui/Controls/ListWidget.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Controls/ItemDelegate.h"
#include "Gui/Controls/ScrollBar.h"
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
        // 项悬停由视口事件驱动：样式表里 QListWidget::item:hover 撤掉之后，这层属性与鼠标跟踪
        // 不会再被 QStyleSheetStyle 打开，委托的 State_MouseOver 分支也就永远不跑，必须在这里打开
        viewport()->setAttribute(Qt::WA_Hover);
        viewport()->setMouseTracking(true);
        // 样式表不再给 QScrollBar 尺寸与颜色：两条滚动条在构造时就换成自绘件
        setVerticalScrollBar(new ScrollBar{Qt::Vertical, this});
        setHorizontalScrollBar(new ScrollBar{Qt::Horizontal, this});
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
