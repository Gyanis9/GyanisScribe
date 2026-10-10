#include "Pick/ColorPickerOverlay.h"

#include "Core/Text.h"
#include "Ui/DesignTokens.h"
#include "Ui/ThemeManager.h"

#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>

#include <algorithm>
#include <cmath>

namespace
{
    using GyanisScribe::Pick::ColorPickerOverlay;

    /// 信息条的高度，逻辑像素：一行颜色文本加一点上下留白
    constexpr int kInfoStripHeight = 28;

    /// 信息条里色块的边长，逻辑像素
    constexpr int kColorChipEdge = 18;

    /// 放大镜边框线宽，逻辑像素
    constexpr int kMagnifierPenWidth = 2;

    /// 网格线宽，逻辑像素
    constexpr int kGridPenWidth = 1;
} // namespace

namespace GyanisScribe::Pick
{
    ColorPickerOverlay::ColorPickerOverlay(const Core::CapturedImage &frame, const QRect &screenGeometry, const ColorFormat format, const Ui::ThemeManager &theme,
                                           QWidget *parent) :
        QWidget(parent), m_frame(frame), m_screenGeometry(screenGeometry), m_format(format), m_theme(theme), m_cursor(QCursor::pos())
    {
        // 遮罩盖住整块屏幕，坐标一律用屏幕坐标：逻辑坐标与冻帧像素之间靠 framePixelFor 换算
        setGeometry(m_screenGeometry);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        setWindowTitle(GyanisScribe::Core::uiText("取色器"));
        setAccessibleName(GyanisScribe::Core::uiText("取色遮罩"));
    }

    QColor ColorPickerOverlay::colorAt(const QPoint &globalPos) const
    {
        const auto pixel = framePixelFor(globalPos, m_screenGeometry, m_frame.devicePixelRatio, m_frame.image.size());
        if (m_frame.image.isNull())
        {
            return {};
        }
        return m_frame.image.pixelColor(pixel);
    }

    QPoint ColorPickerOverlay::cursor() const
    {
        return m_cursor;
    }

    QString ColorPickerOverlay::currentText() const
    {
        return formatColor(colorAt(m_cursor), m_format);
    }

    void ColorPickerOverlay::paintEvent(QPaintEvent *)
    {
        QPainter painter{this};
        if (m_frame.image.isNull())
        {
            return;
        }

        // 冻帧按逻辑尺寸铺满遮罩：抓帧在高 DPI 下比逻辑尺寸大，不缩就会画出屏幕外
        painter.drawImage(rect(), m_frame.image);
        painter.fillRect(rect(), QColor{0, 0, 0, 24});

        const auto local  = m_cursor - m_screenGeometry.topLeft();
        const auto accent = m_theme.palette().accent;

        // 十字线只画在当前像素上，让用户能确认到底取的是哪一格
        QPen crossPen{accent};
        crossPen.setWidth(kMagnifierPenWidth);
        painter.setPen(crossPen);
        painter.drawPoint(local);

        const auto layout   = magnifierLayoutFor(m_cursor, m_screenGeometry, m_screenGeometry, kMagnifierSampleEdge, kMagnifierViewEdge);
        const auto viewRect = layout.viewRect.translated(-m_screenGeometry.topLeft());
        if (viewRect.isValid())
        {
            // 取样框是屏幕逻辑坐标，抓帧是设备像素：先换算左上角，再按缩放比放大尺寸，
            // 只减不乘会在高 DPI 屏上把整片放大镜偏到左上角
            const auto cropOrigin = framePixelFor(layout.sourceRect.topLeft(), m_screenGeometry, m_frame.devicePixelRatio, m_frame.image.size());
            const auto cropSpan   = static_cast<int>(std::lround(kMagnifierSampleEdge * m_frame.devicePixelRatio));
            const auto cropSize   = QSize{std::clamp(cropSpan, 1, m_frame.image.width() - cropOrigin.x()), std::clamp(cropSpan, 1, m_frame.image.height() - cropOrigin.y())};
            // 像素格用最近邻放大，Smooth 会把格子糊成一片，放大镜就白摆了
            painter.drawImage(viewRect, m_frame.image.copy(QRect{cropOrigin, cropSize}));
            QPen borderPen{accent};
            borderPen.setWidth(kMagnifierPenWidth);
            painter.setPen(borderPen);
            painter.drawRect(viewRect);

            painter.setPen(QPen{QColor{128, 128, 128, 140}, kGridPenWidth});
            for (auto step = 1; step < kMagnifierSampleEdge; ++step)
            {
                const auto offset = step * layout.cellSize;
                painter.drawLine(viewRect.left() + offset, viewRect.top(), viewRect.left() + offset, viewRect.bottom());
                painter.drawLine(viewRect.left(), viewRect.top() + offset, viewRect.right(), viewRect.top() + offset);
            }
        }

        // 信息条贴在放大镜下面：色块 + 当前写法，用户不必点下去就能确认取到了什么
        const auto stripRect = QRect{viewRect.left(), viewRect.bottom() + kMagnifierPenWidth, viewRect.width(), kInfoStripHeight};
        painter.fillRect(stripRect, m_theme.palette().surface);
        painter.fillRect(stripRect.adjusted(4, 4, -(stripRect.width() - kColorChipEdge - 8), -4), colorAt(m_cursor));
        painter.setPen(QPen{m_theme.palette().text});
        painter.drawText(stripRect.adjusted(kColorChipEdge + 12, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, currentText());
    }

    void ColorPickerOverlay::mouseMoveEvent(QMouseEvent *event)
    {
        m_cursor = event->globalPosition().toPoint();
        update();
    }

    void ColorPickerOverlay::mousePressEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton)
        {
            m_cursor = event->globalPosition().toPoint();
            pickAndClose();
            return;
        }
        // 右键等同 Esc：用户在取色中途最想做的就是把这一层关掉
        emit cancelled();
        close();
    }

    void ColorPickerOverlay::keyPressEvent(QKeyEvent *event)
    {
        if (event->key() == Qt::Key_Escape)
        {
            emit cancelled();
            close();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void ColorPickerOverlay::pickAndClose()
    {
        // 文本与颜色同刻取：先取文本再取色会让两者有可能落在两次移动之间
        const auto color = colorAt(m_cursor);
        emit       colorPicked(color, formatColor(color, m_format));
        close();
    }

} // namespace GyanisScribe::Pick
