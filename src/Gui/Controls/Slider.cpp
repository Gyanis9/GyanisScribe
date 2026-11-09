#include "Gui/Controls/Slider.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Theme/CornerStyle.h"
#include "Gui/Theme/DesignTokens.h"

#include <QEnterEvent>
#include <QEvent>
#include <QPainter>
#include <QPen>
#include <QStyle>

#include <algorithm>

namespace
{
    /// 槽的高度，逻辑像素
    constexpr int kGrooveHeight = 6;

    /// 手柄的边长，逻辑像素：正方形，按正圆档画成圆
    constexpr int kHandleEdge = 18;

    /// 手柄描边宽度：聚焦与静息同宽，反转只换颜色
    constexpr qreal kHandleBorderWidth = 2.0;
} // namespace

namespace GyanisScribe::Gui
{
    Slider::Slider(const Qt::Orientation orientation, QWidget *parent) : QSlider(orientation, parent)
    {
        // 绘制只按水平方向实现，竖向会把槽与手柄画成一条横杠；构造时直接拦下
        Q_ASSERT(orientation == Qt::Horizontal);
        setFixedHeight(20);
    }

    QSize Slider::sizeHint() const
    {
        return {80, 20};
    }

    void Slider::paintEvent(QPaintEvent *)
    {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing, true);
        const auto  &palette  = activePalette();
        const bool   enabled  = isEnabled();
        const qreal  centerY  = height() / 2.0;
        const int    span     = std::max(0, width() - kHandleEdge);
        const int    position = style()->sliderPositionFromValue(minimum(), maximum(), value(), span, false);
        const QRectF groove{0.0, centerY - kGrooveHeight / 2.0, static_cast<qreal>(width()), static_cast<qreal>(kGrooveHeight)};
        const QRectF handle{static_cast<qreal>(position), centerY - kHandleEdge / 2.0, static_cast<qreal>(kHandleEdge), static_cast<qreal>(kHandleEdge)};

        const QColor grooveFill        = enabled ? palette.mutedFill : palette.border;
        const QColor grooveBorder      = enabled ? palette.border : palette.borderStrong;
        const qreal  grooveBorderWidth = static_cast<qreal>(themeBorderWidth(activeThemeKind()));
        Core::paintContinuousCorner(painter, groove, Core::cornerShapeFor(Core::CornerRole::Hairline), grooveFill, QPen{grooveBorder, grooveBorderWidth});

        if (handle.center().x() > 0.5)
        {
            const QRectF subPage{groove.left(), groove.top(), std::max<qreal>(0.0, handle.center().x()), groove.height()};
            Core::paintContinuousCorner(painter, subPage, Core::cornerShapeFor(Core::CornerRole::Hairline), enabled ? palette.accent : palette.borderStrong,
                                        QPen{enabled ? palette.accent : palette.borderStrong, grooveBorderWidth});
        }

        QColor handleFill   = enabled ? palette.accent : palette.borderStrong;
        QColor handleBorder = palette.surface;
        if (enabled && underMouse())
        {
            handleFill = palette.accentHover;
        }
        // 焦点反转要压过悬停：键盘操作时手柄永远是底色芯加正文色圈
        if (hasFocus())
        {
            handleFill   = palette.surface;
            handleBorder = palette.text;
        }
        Core::paintContinuousCorner(painter, handle, Core::cornerShapeFor(Core::CornerRole::Handle, handle.size()), handleFill, QPen{handleBorder, kHandleBorderWidth});
    }

    void Slider::enterEvent(QEnterEvent *event)
    {
        QSlider::enterEvent(event);
        update();
    }

    void Slider::leaveEvent(QEvent *event)
    {
        QSlider::leaveEvent(event);
        update();
    }
} // namespace GyanisScribe::Gui
