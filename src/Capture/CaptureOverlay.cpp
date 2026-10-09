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

        painter.drawImage(0, 0, m_background);

        // 压暗层用四块矩形围出选区，而不是建偶奇路径：四块矩形是这里最省事也最不容易画错形状的做法
        painter.setPen(Qt::NoPen);
        painter.setBrush(GyanisScribe::Ui::kCaptureMaskColor);
        if (region.isNull())
        {
            painter.drawRect(widgetRect);
        } else
        {
            painter.setClipRegion(QRegion(widgetRect) - QRegion(region));
            painter.drawRect(widgetRect);
            painter.setClipping(false);
        }

        if (!region.isNull())
        {
            auto borderPen = QPen(GyanisScribe::Ui::kAccentColor);
            borderPen.setWidth(GyanisScribe::Ui::kSelectionBorderWidth);
            painter.setPen(borderPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(region);

            if (m_isSelecting)
            {
                // 拖动中给十字虚线定位，松手后不留在画面上干扰读数
                auto crossPen = QPen(GyanisScribe::Ui::kCrosshairColor);
                crossPen.setStyle(Qt::DashLine);
                painter.setPen(crossPen);
                painter.drawLine(QPoint(m_currentPoint.x(), 0), QPoint(m_currentPoint.x(), widgetRect.height()));
                painter.drawLine(QPoint(0, m_currentPoint.y()), QPoint(widgetRect.width(), m_currentPoint.y()));
            }

            const auto hint     = formatSelectionHint(region);
            QFont      hintFont = font();
            hintFont.setPointSize(GyanisScribe::Ui::kHintFontSize);
            painter.setFont(hintFont);
            const QFontMetrics metrics(hintFont);
            const auto         textSize    = metrics.size(Qt::TextSingleLine, hint);
            const auto         hintTopLeft = hintPositionFor(region);
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
            update();
        }
    }

    void CaptureOverlay::mouseMoveEvent(QMouseEvent *event)
    {
        m_currentPoint = event->position().toPoint();
        // 没在拖也要重画：十字辅助线跟着光标走
        update();
    }

    void CaptureOverlay::mouseReleaseEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton && m_isSelecting)
        {
            m_isSelecting  = false;
            m_currentPoint = event->position().toPoint();
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
