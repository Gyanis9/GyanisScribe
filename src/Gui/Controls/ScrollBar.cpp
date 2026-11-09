#include "Gui/Controls/ScrollBar.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Theme/DesignTokens.h"

#include <QEnterEvent>
#include <QEvent>
#include <QPainter>
#include <QPen>

#include <algorithm>

namespace
{
    /// 滚动条的粗细，逻辑像素
    constexpr int kThickness = 10;

    /// 轨道四周留白，手柄不贴边
    constexpr int kMargin = 2;

    /// 手柄的最短长度，逻辑像素
    constexpr int kMinimumHandle = 24;

    /**
     * @brief 手柄颜色
     * @param kind 已解算的主题档
     * @param hover 是否悬停
     * @return 高对比度档为不透明纯黑；浅深档为带透明度的中性色，悬停时加深一档
     * @details 浅深档的透明度沿用原样式表里唯一的 rgba 例外：深色档 14%/28%、浅色档 16%/32%
     */
    QColor handleColor(const GyanisScribe::Gui::ThemeKind kind, const bool hover)
    {
        if (kind == GyanisScribe::Gui::ThemeKind::HighContrast)
        {
            return QColor{0, 0, 0};
        }
        const int alpha = hover ? (kind == GyanisScribe::Gui::ThemeKind::Dark ? 71 : 82) : (kind == GyanisScribe::Gui::ThemeKind::Dark ? 36 : 41);
        return kind == GyanisScribe::Gui::ThemeKind::Dark ? QColor{232, 232, 227, alpha} : QColor{20, 20, 18, alpha};
    }
} // namespace

namespace GyanisScribe::Gui
{
    ScrollBar::ScrollBar(const Qt::Orientation orientation, QWidget *parent) : QScrollBar(orientation, parent)
    {
        // 轨道要真的透明：QScrollBar 默认带着不透明绘制标记，宿主窗口底也会铺到轨道上，
        // 两处都关掉，轨道才露出背后的容器
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        setAutoFillBackground(false);
        if (orientation == Qt::Vertical)
        {
            setFixedWidth(kThickness);
        } else
        {
            setFixedHeight(kThickness);
        }
    }

    QSize ScrollBar::sizeHint() const
    {
        return orientation() == Qt::Vertical ? QSize{kThickness, 120} : QSize{120, kThickness};
    }

    void ScrollBar::paintEvent(QPaintEvent *)
    {
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing, true);
        const int trackLength = (orientation() == Qt::Vertical ? height() : width()) - 2 * kMargin;
        if (trackLength <= 0)
        {
            return;
        }
        const int    range         = maximum() - minimum();
        const int    page          = std::max(1, pageStep());
        const int    minimumHandle = std::min(kMinimumHandle, trackLength);
        const int    handleLength  = std::clamp(qRound(static_cast<qreal>(trackLength) * page / (range + page)), minimumHandle, trackLength);
        const int    position      = range > 0 ? qRound(static_cast<qreal>(trackLength - handleLength) * (value() - minimum()) / range) : 0;
        const QRectF handle = orientation() == Qt::Vertical
                                      ? QRectF{kMargin, static_cast<qreal>(kMargin + position), static_cast<qreal>(width() - 2 * kMargin), static_cast<qreal>(handleLength)}
                                      : QRectF{static_cast<qreal>(kMargin + position), kMargin, static_cast<qreal>(handleLength), static_cast<qreal>(height() - 2 * kMargin)};
        Core::paintContinuousCorner(painter, handle, Core::cornerShapeFor(Core::CornerRole::Hairline), handleColor(activeThemeKind(), underMouse()), QPen{Qt::NoPen});
    }

    void ScrollBar::enterEvent(QEnterEvent *event)
    {
        QScrollBar::enterEvent(event);
        update();
    }

    void ScrollBar::leaveEvent(QEvent *event)
    {
        QScrollBar::leaveEvent(event);
        update();
    }
} // namespace GyanisScribe::Gui
