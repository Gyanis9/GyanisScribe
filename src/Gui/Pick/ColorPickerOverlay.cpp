#include "Gui/Pick/ColorPickerOverlay.h"

#include "Base/Text.h"
#include "Gui/Theme/DesignTokens.h"
#include "Gui/Theme/ThemeManager.h"

#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    using GyanisScribe::Gui::ColorPickerOverlay;

    /// 信息条的高度，逻辑像素：一行颜色文本加一点上下留白
    constexpr int kInfoStripHeight = 28;

    /// 信息条里色块的边长，逻辑像素
    constexpr int kColorChipEdge = 18;

    /// 放大镜边框线宽，逻辑像素
    constexpr int kMagnifierPenWidth = 2;

    /// 网格线宽，逻辑像素
    constexpr int kGridPenWidth = 1;
} // namespace

namespace GyanisScribe::Gui
{
    ColorPickerOverlay::ColorPickerOverlay(Base::CapturedImage frame, const QRect &screenGeometry, const Core::ColorFormat format, const Core::HexCasing casing,
                                           const Core::MagnifierView view, const Gui::ThemeManager &theme, QWidget *parent) :
        QWidget(parent), m_frame(std::move(frame)), m_screenGeometry(screenGeometry), m_format(format), m_hexCasing(casing), m_magnifierView(view), m_theme(theme),
        m_cursor(QCursor::pos())
    {
        // 遮罩盖住整块屏幕，坐标一律用屏幕坐标：逻辑坐标与冻帧像素之间靠 Core::framePixelFor 换算
        setGeometry(m_screenGeometry);
        setMouseTracking(true);
        setCursor(Qt::CrossCursor);
        setWindowTitle(GyanisScribe::Base::uiText("取色器"));
        setAccessibleName(GyanisScribe::Base::uiText("取色遮罩"));
        // 关掉即销毁：控制器拿的是 QPointer，下一次取色重建的是新的一份；
        // 少了这一档，每取一次色就在桌上留一个藏着不用的全屏遮罩（还带着它那张屏幕冻帧）
        setAttribute(Qt::WA_DeleteOnClose);
    }

    QColor ColorPickerOverlay::colorAt(const QPoint &globalPos) const
    {
        const auto pixel = Core::framePixelFor(globalPos, m_screenGeometry, m_frame.devicePixelRatio, m_frame.image.size());
        if (m_frame.image.isNull())
        {
            return {};
        }
        return m_frame.image.pixelColor(pixel);
    }

    QPoint ColorPickerOverlay::pointerPosition() const
    {
        return m_cursor;
    }

    QString ColorPickerOverlay::currentText() const
    {
        return Core::formatColor(colorAt(m_cursor), m_format, m_hexCasing);
    }

    bool ColorPickerOverlay::isMagnifierVisible() const noexcept
    {
        return m_magnifierView != Core::MagnifierView::Off;
    }

    void ColorPickerOverlay::setRecentColors(std::vector<QColor> colors)
    {
        m_recent        = std::move(colors);
        m_hoveredSwatch = -1;
        update();
    }

    int ColorPickerOverlay::swatchIndexAt(const QPoint &globalPos) const
    {
        const auto rects = Core::recentSwatchRects(m_screenGeometry, static_cast<int>(m_recent.size()));
        for (std::size_t index = 0; index < rects.size(); ++index)
        {
            if (rects[index].contains(globalPos))
            {
                return static_cast<int>(index);
            }
        }
        return -1;
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

        // 视野档是「不显示」时也按窄档算一次位置：下面那条信息条要留在原位
        const auto sampleEdge = Core::magnifierSampleEdgeFor(m_magnifierView);
        const auto layout =
                Core::magnifierLayoutFor(m_cursor, m_screenGeometry, m_screenGeometry, sampleEdge > 0 ? sampleEdge : Core::kMagnifierSampleEdge, Core::kMagnifierViewEdge);
        const auto viewRect = layout.viewRect.translated(-m_screenGeometry.topLeft());
        if (viewRect.isValid() && isMagnifierVisible())
        {
            // 取样框是屏幕逻辑坐标，抓帧是设备像素：先换算左上角，再按缩放比放大尺寸，
            // 只减不乘会在高 DPI 屏上把整片放大镜偏到左上角
            const auto cropOrigin = Core::framePixelFor(layout.sourceRect.topLeft(), m_screenGeometry, m_frame.devicePixelRatio, m_frame.image.size());
            const auto cropSpan   = static_cast<int>(std::lround(sampleEdge * m_frame.devicePixelRatio));
            const auto cropSize   = QSize{std::clamp(cropSpan, 1, m_frame.image.width() - cropOrigin.x()), std::clamp(cropSpan, 1, m_frame.image.height() - cropOrigin.y())};
            // 像素格用最近邻放大，Smooth 会把格子糊成一片，放大镜就白摆了
            painter.drawImage(viewRect, m_frame.image.copy(QRect{cropOrigin, cropSize}));
            QPen borderPen{accent};
            borderPen.setWidth(kMagnifierPenWidth);
            painter.setPen(borderPen);
            painter.drawRect(viewRect);

            painter.setPen(QPen{QColor{128, 128, 128, 140}, kGridPenWidth});
            for (auto step = 1; step < sampleEdge; ++step)
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

        // 最近取色的色块行贴在屏幕底边中间：命中测试与绘制共用 Core::recentSwatchRects 那一处几何
        const auto origin   = m_screenGeometry.topLeft();
        const auto swatches = Core::recentSwatchRects(m_screenGeometry, static_cast<int>(m_recent.size()));
        for (std::size_t index = 0; index < swatches.size(); ++index)
        {
            const auto box = swatches[index].translated(-origin.x(), -origin.y());
            painter.fillRect(box, m_recent[index]);
            const auto hovered = m_hoveredSwatch == static_cast<int>(index);
            // 线宽走 qreal：花括号初始化里 int 到 qreal 是收缩转换，MSVC /WX 直接拒
            painter.setPen(QPen{hovered ? accent : m_theme.palette().text, static_cast<qreal>(hovered ? kMagnifierPenWidth : kGridPenWidth)});
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(box.adjusted(0, 0, -1, -1));
        }
    }

    void ColorPickerOverlay::mouseMoveEvent(QMouseEvent *event)
    {
        m_cursor        = event->globalPosition().toPoint();
        m_hoveredSwatch = swatchIndexAt(m_cursor);
        update();
    }

    void ColorPickerOverlay::mousePressEvent(QMouseEvent *event)
    {
        if (event->button() == Qt::LeftButton)
        {
            m_cursor = event->globalPosition().toPoint();
            // 压在色块行上就是取那一块，不是取它底下那个像素：色块行是给「再取一次最近用过的色」留的
            if (const auto swatch = swatchIndexAt(m_cursor); swatch >= 0)
            {
                deliverColor(m_recent[static_cast<std::size_t>(swatch)]);
                return;
            }
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

        // 数字键 1..8 取色块行上的第几块：与点击取那一块是同一条出口，光标动没动都不影响
        if (event->modifiers() == Qt::NoModifier && event->key() >= Qt::Key_1 && event->key() <= Qt::Key_8)
        {
            const auto index = event->key() - Qt::Key_1;
            if (index >= 0 && static_cast<std::size_t>(index) < m_recent.size())
            {
                deliverColor(m_recent[static_cast<std::size_t>(index)]);
                return;
            }
        }
        QWidget::keyPressEvent(event);
    }

    void ColorPickerOverlay::pickAndClose()
    {
        // 文本与颜色同刻取：先取文本再取色会让两者有可能落在两次移动之间
        deliverColor(colorAt(m_cursor));
    }

    void ColorPickerOverlay::deliverColor(const QColor &color)
    {
        emit colorPicked(color, Core::formatColor(color, m_format, m_hexCasing));
        close();
    }

} // namespace GyanisScribe::Gui
