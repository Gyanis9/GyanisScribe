#include "Gui/Controls/ScrollBar.h"

#include "Core/Geometry/ContinuousCorner.h"
#include "Gui/Theme/DesignTokens.h"

#include <QEnterEvent>
#include <QEvent>
#include <QPainter>
#include <QPen>
#include <QStyleOptionSlider>

#include <algorithm>

namespace
{
    /// 轨道四周留白，手柄不贴边
    constexpr int kMargin = 2;

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
    QRectF scrollBarTrackRect(const QStyleOptionSlider &option, const QWidget &widget)
    {
        const bool vertical = option.orientation == Qt::Vertical;
        const int  length   = vertical ? widget.height() : widget.width();
        const int  extent   = vertical ? widget.width() : widget.height();
        if (length <= 2 * kMargin || extent <= 2 * kMargin)
        {
            return {};
        }
        return vertical ? QRectF{kMargin, kMargin, static_cast<qreal>(extent - 2 * kMargin), static_cast<qreal>(length - 2 * kMargin)}
                        : QRectF{kMargin, kMargin, static_cast<qreal>(length - 2 * kMargin), static_cast<qreal>(extent - 2 * kMargin)};
    }

    QRectF scrollBarSliderRect(const QStyleOptionSlider &option, const QWidget &widget)
    {
        const QRectF track = scrollBarTrackRect(option, widget);
        if (track.isEmpty())
        {
            return {};
        }
        const bool vertical      = option.orientation == Qt::Vertical;
        const int  trackLength   = qRound(vertical ? track.height() : track.width());
        const int  range         = option.maximum - option.minimum;
        const int  page          = std::max(1, option.pageStep);
        const int  minimumHandle = std::min(kScrollBarMinimumHandle, trackLength);
        const int  handleLength  = std::clamp(qRound(static_cast<qreal>(trackLength) * page / (qint64{range} + page)), minimumHandle, trackLength);
        const int  position =
                range > 0 ? qRound(static_cast<qreal>(trackLength - handleLength) * (static_cast<qreal>(option.sliderPosition) - static_cast<qreal>(option.minimum)) / range) : 0;
        return vertical ? QRectF{track.left(), track.top() + position, track.width(), static_cast<qreal>(handleLength)}
                        : QRectF{track.left() + position, track.top(), static_cast<qreal>(handleLength), track.height()};
    }

    ScrollBar::ScrollBar(const Qt::Orientation orientation, QWidget *parent) : QScrollBar(orientation, parent)
    {
        // 轨道要真的透明：QScrollBar 默认带着不透明绘制标记，宿主窗口底也会铺到轨道上，
        // 两处都关掉，轨道才露出背后的容器
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        setAutoFillBackground(false);
        if (orientation == Qt::Vertical)
        {
            setFixedWidth(kScrollBarThickness);
        } else
        {
            setFixedHeight(kScrollBarThickness);
        }
    }

    QSize ScrollBar::sizeHint() const
    {
        return orientation() == Qt::Vertical ? QSize{kScrollBarThickness, 120} : QSize{120, kScrollBarThickness};
    }

    void ScrollBar::paintEvent(QPaintEvent *)
    {
        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRectF handle = scrollBarSliderRect(option, *this);
        if (handle.isEmpty())
        {
            return;
        }
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing, true);
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
