#include "Capture/CaptureOverlay.h"

#include "Capture/ScreenUtils.h"
#include "Ui/DesignTokens.h"

#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace GyanisScribe::Capture
{

    CaptureOverlay::CaptureOverlay(const QImage &background, const QRect &screenGeometry, qreal devicePixelRatio, QWidget *parent) :
        QWidget(parent), m_screenGeometry(screenGeometry), m_devicePixelRatio(devicePixelRatio)
    {
        // 物理像素图按该屏缩放因子标记一次，之后的绘制一律用逻辑坐标，读数与选区才对得上同一个点
        m_background = background;
        m_background.setDevicePixelRatio(devicePixelRatio);

        // 无边框 + 置顶 + Tool：Tool 让它不进任务栏，截图时不该多出一个窗口条目
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setGeometry(m_screenGeometry);
        setMouseTracking(true); // 十字辅助线要在不按鼠标时也跟着走
        setCursor(Qt::CrossCursor);
        setFocusPolicy(Qt::StrongFocus);
    }

    CaptureOverlay::~CaptureOverlay() = default;

    QRect CaptureOverlay::selectedRegion() const
    {
        if (!m_isSelecting && m_anchorPoint == m_currentPoint)
        {
            return {};
        }
        return normalizeRect(m_anchorPoint, m_currentPoint);
    }

    void CaptureOverlay::confirmSelection()
    {
        const auto region = selectedRegion();
        if (region.width() <= 0 || region.height() <= 0)
        {
            // 没框到东西就不产出：0×0 的图一路传下去会变成一个打不开的产物
            return;
        }
        emit regionSelected(region);
    }

    void CaptureOverlay::setWindowCandidates(const std::vector<QRect> &globalRects)
    {
        // 换成局部坐标：遮罩上的鼠标读数与选区都是这块屏内的局部值，两套单位混在一起就会错一格
        const auto origin = m_screenGeometry.topLeft();
        m_windowCandidates.clear();
        m_windowCandidates.reserve(globalRects.size());
        for (const auto &rect: globalRects)
        {
            m_windowCandidates.push_back(rect.translated(-origin.x(), -origin.y()));
        }
        refreshHoveredRegion();
    }

    std::optional<QRect> CaptureOverlay::hoveredRegion() const
    {
        return m_hoveredRegion;
    }

    void CaptureOverlay::refreshHoveredRegion()
    {
        if (m_isSelecting || !selectedRegion().isNull())
        {
            m_hoveredRegion.reset();
            return;
        }
        m_hoveredRegion = regionAtPoint(m_cursorPoint, m_windowCandidates);
    }

    QRect CaptureOverlay::normalizeRect(const QPoint &anchor, const QPoint &current)
    {
        return QRect(anchor, current).normalized();
    }

    QPoint CaptureOverlay::hintPositionFor(const QRect &region) const
    {
        // 读数默认画在选区上方一点；贴到屏顶时改画到选区下方，否则读数会被屏边切掉
        const auto hintOffset = region.height() + GyanisScribe::Ui::kSelectionBorderWidth + GyanisScribe::Ui::kHintPadding;
        if (region.top() - hintOffset - 4 >= 0)
        {
            return QPoint(region.left(), region.top() - hintOffset);
        }
        return QPoint(region.left(), region.bottom() + 4);
    }

    void CaptureOverlay::paintEvent(QPaintEvent *event)
    {
        Q_UNUSED(event);

        QPainter   painter(this);
        const auto widgetRect = QRect(QPoint(0, 0), size());
        const auto region     = selectedRegion();
        // 还没框的时候把悬停认出的那一块当预览画出来：不亮这块，用户就只能靠猜知道点一下会不会中
        const auto shown = region.isNull() ? m_hoveredRegion.value_or(QRect{}) : region;

        painter.drawImage(0, 0, m_background);

        // 压暗层用四块矩形围出选区，而不是建偶奇路径：四块矩形是这里最省事也最不容易画错形状的做法
        painter.setPen(Qt::NoPen);
        painter.setBrush(GyanisScribe::Ui::kCaptureMaskColor);
        if (shown.isNull())
        {
            painter.drawRect(widgetRect);
        } else
        {
            painter.setClipRegion(QRegion(widgetRect) - QRegion(shown));
            painter.drawRect(widgetRect);
            painter.setClipping(false);
        }

        if (!shown.isNull())
        {
            auto borderPen = QPen(GyanisScribe::Ui::kAccentColor);
            borderPen.setWidth(GyanisScribe::Ui::kSelectionBorderWidth);
            painter.setPen(borderPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(shown);

            if (m_isSelecting)
            {
                // 拖动中给十字虚线定位，松手后不留在画面上干扰读数
                auto crossPen = QPen(GyanisScribe::Ui::kCrosshairColor);
                crossPen.setStyle(Qt::DashLine);
                painter.setPen(crossPen);
                painter.drawLine(QPoint(m_currentPoint.x(), 0), QPoint(m_currentPoint.x(), widgetRect.height()));
                painter.drawLine(QPoint(0, m_currentPoint.y()), QPoint(widgetRect.width(), m_currentPoint.y()));
            }

            const auto hint     = formatSelectionHint(shown);
            QFont      hintFont = font();
            hintFont.setPointSize(GyanisScribe::Ui::kHintFontSize);
            painter.setFont(hintFont);
            const QFontMetrics metrics(hintFont);
            const auto         textSize    = metrics.size(Qt::TextSingleLine, hint);
            const auto         hintTopLeft = hintPositionFor(shown);
            const QRect        hintRect(hintTopLeft, textSize + QSize(GyanisScribe::Ui::kHintPadding * 2, GyanisScribe::Ui::kHintPadding));

            painter.setPen(Qt::NoPen);
            painter.setBrush(GyanisScribe::Ui::kCaptureHintBackgroundColor);
            painter.drawRect(hintRect);
            painter.setPen(GyanisScribe::Ui::kCaptureHintTextColor);
            painter.drawText(hintRect, Qt::AlignCenter, hint);
        }
    }

    void CaptureOverlay::mousePressEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton)
        {
            m_isSelecting  = true;
            m_anchorPoint  = event->position().toPoint();
            m_currentPoint = m_anchorPoint;
            m_cursorPoint  = m_anchorPoint;
            // 预览留着不熄：按下还没拖的那一瞬间，「点一下要哪扇」的答案就是它。
            // 真拖起来了再由 refreshHoveredRegion 关掉，同一时刻不会出现两个矩形
            update();
        }
    }

    void CaptureOverlay::mouseMoveEvent(QMouseEvent *event)
    {
        // 光标位置与选区端点分开记：没按下时也要重画（辅助线与悬停跟着光标走），
        // 但把它写进 m_currentPoint 会让 selectedRegion() 从原点编出一块根本没框过的选区
        m_cursorPoint = event->position().toPoint();
        if (m_isSelecting)
        {
            m_currentPoint = m_cursorPoint;
        }
        refreshHoveredRegion();
        update();
    }

    void CaptureOverlay::mouseReleaseEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton && m_isSelecting)
        {
            m_isSelecting  = false;
            m_currentPoint = event->position().toPoint();

            // 按下又原地放开（没拖出任何一块）时，认此刻悬停亮着的那个候选：
            // 「点一下就要这一扇窗口」是框选最常用的走法，而原地放开过去什么也不产出
            if (m_hoveredRegion && selectedRegion().isNull())
            {
                m_anchorPoint  = m_hoveredRegion->topLeft();
                m_currentPoint = m_hoveredRegion->bottomRight();
                m_hoveredRegion.reset();
            }
            update();
        }
    }

    void CaptureOverlay::mouseDoubleClickEvent(QMouseEvent *event)
    {
        // 双击即确认当前选区，不必再按 Enter
        Q_UNUSED(event);
        confirmSelection();
    }

    void CaptureOverlay::keyPressEvent(QKeyEvent *event)
    {
        switch (event->key())
        {
            case Qt::Key_Escape:
                emit dismissed();
                return;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                confirmSelection();
                return;
            default:
                QWidget::keyPressEvent(event);
                return;
        }
    }

} // namespace GyanisScribe::Capture
