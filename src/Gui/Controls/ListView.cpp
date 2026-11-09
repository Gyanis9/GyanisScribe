#include "Gui/Controls/ListView.h"

#include "Base/Text.h"
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
    ListView::ListView(QWidget *parent) : QListView(parent)
    {
        // 弹层视图在组合框构造时就会建立，主窗的可操作控件扫描会走到它；
        // 具体是哪个字段由组合框自己的无障碍名说明，这里只给列表一条通用名
        setAccessibleName(Base::uiText("下拉列表"));
        setItemDelegate(new ItemDelegate{this});
        // 与 Qt 默认弹层一致取中间省略：右侧省略会把长族名的差异截掉
        setTextElideMode(Qt::ElideMiddle);
        // 容器底与描边都由本类画：原生面板框会在圆角外补一圈方角线
        setFrameShape(QFrame::NoFrame);
        // 控件与视口都不许再铺系统底色，否则圆角外的四个角会露成方块；
        // WA_TranslucentBackground 让本类自己决定哪一块上色，弹层容器的底色从圆角外透出来
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        viewport()->setAutoFillBackground(false);
        // 项悬停由视口事件驱动：样式表里 QComboBox QAbstractItemView::item:hover 撤掉之后，
        // 这层属性与鼠标跟踪不会再被 QStyleSheetStyle 打开，委托的 State_MouseOver 分支也就永远不跑
        viewport()->setAttribute(Qt::WA_Hover);
        viewport()->setMouseTracking(true);
        // 样式表不再给 QScrollBar 尺寸与颜色：两条滚动条在构造时就换成自绘件
        setVerticalScrollBar(new ScrollBar{Qt::Vertical, this});
        setHorizontalScrollBar(new ScrollBar{Qt::Horizontal, this});
    }

    bool ListView::event(QEvent *event)
    {
        if (event->type() == QEvent::Paint)
        {
            paintSurface();
        }
        return QListView::event(event);
    }

    void ListView::paintSurface()
    {
        QPainter   painter{this};
        const QPen border =
                activeThemeKind() == ThemeKind::HighContrast ? QPen{activePalette().borderStrong, static_cast<qreal>(themeBorderWidth(ThemeKind::HighContrast))} : QPen{Qt::NoPen};
        Core::paintContinuousCorner(painter, QRectF{rect()}, Core::cornerShapeFor(Core::CornerRole::Control), activePalette().surface, border);
    }
} // namespace GyanisScribe::Gui
