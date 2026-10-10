#include "Annotation/AnnotationTool.h"

#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>

namespace GyanisScribe::Annotation
{
    namespace
    {
        // 命中测试的最小容差：1-2 像素的线很细，不给余量就点不中
        constexpr int kMinimumHitTolerance = 8;
    } // namespace

    AnnotationTool::AnnotationTool(ToolKind kind) : m_kind(kind), m_color(255, 0, 0), m_lineWidth(3), m_fontSize(16)
    {
    }

    ToolKind AnnotationTool::kind() const noexcept
    {
        return m_kind;
    }

    bool AnnotationTool::isActive() const noexcept
    {
        return m_isActive;
    }

    void AnnotationTool::setActive(bool active) noexcept
    {
        // 半途的拖拽在切走时丢掉：留下一条谁都没看见的图形，撤销栈里就会出现「没画过却能撤」的一条
        if (!active)
        {
            cancel();
        }
        m_isActive = active;
    }

    void AnnotationTool::begin(const QPoint &point)
    {
        m_startPoint        = point;
        m_pending           = AnnotationShape{};
        m_pending.color     = m_color;
        m_pending.lineWidth = m_lineWidth;
        m_pending.fontSize  = m_fontSize;
        m_pending.position  = point;
        m_pending.rect      = QRect(point, point);
        m_pending.points    = {point, point};
    }

    void AnnotationTool::update(const QPoint &point)
    {
        m_pending.rect   = QRect(m_startPoint, point).normalized();
        m_pending.points = {m_startPoint, point};
    }

    std::optional<AnnotationShape> AnnotationTool::end()
    {
        if (!shapeIsUsable(m_pending))
        {
            cancel();
            return std::nullopt;
        }

        // 交出去之后再清在途：引擎要把这条图形放进撤销栈
        const AnnotationShape committed = m_pending;
        cancel();
        return committed;
    }

    void AnnotationTool::cancel() noexcept
    {
        m_pending = AnnotationShape{};
    }

    int AnnotationTool::shapeCount() const noexcept
    {
        return static_cast<int>(m_shapes.size());
    }

    AnnotationShape AnnotationTool::shapeAt(int index) const
    {
        if (index < 0 || index >= m_shapes.size())
        {
            return {};
        }
        return m_shapes[index];
    }

    void AnnotationTool::insertShape(int index, const AnnotationShape &shape)
    {
        // 索引钳到末尾：撤销「删除」时传的正是取走时那个位置，越界只会来自调用方算错
        const int clamped = index < 0 ? 0 : (index > m_shapes.size() ? m_shapes.size() : index);
        m_shapes.insert(clamped, shape);
    }

    AnnotationShape AnnotationTool::takeShapeAt(int index)
    {
        if (index < 0 || index >= m_shapes.size())
        {
            return {};
        }
        const AnnotationShape taken = m_shapes[index];
        m_shapes.remove(index);
        return taken;
    }

    bool AnnotationTool::translateShapeBy(int index, const QPoint &delta)
    {
        if (index < 0 || index >= m_shapes.size())
        {
            return false;
        }

        auto &shape = m_shapes[index];
        shape.rect.translate(delta);
        shape.position += delta;
        for (auto &point: shape.points)
        {
            point += delta;
        }
        return true;
    }

    int AnnotationTool::hitTest(const QPoint &point) const
    {
        // 倒序遍历：后画的图形压在上面，用户点的就是看得见的那一条
        for (int index = static_cast<int>(m_shapes.size()) - 1; index >= 0; --index)
        {
            const auto &shape     = m_shapes[index];
            const int   tolerance = shape.lineWidth > kMinimumHitTolerance ? shape.lineWidth : kMinimumHitTolerance;
            if (hitTestShape(shape, point, tolerance))
            {
                return index;
            }
        }
        return -1;
    }

    bool AnnotationTool::hasPending() const noexcept
    {
        return !m_pending.rect.isNull() || !m_pending.text.isEmpty() || !m_pending.points.isEmpty();
    }

    bool AnnotationTool::hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const
    {
        const auto box = boundingBoxOf(shape).adjusted(-tolerance, -tolerance, tolerance, tolerance);
        return box.contains(point);
    }

    bool AnnotationTool::shapeIsUsable(const AnnotationShape &shape) const
    {
        return shape.rect.width() > 1 && shape.rect.height() > 1;
    }

    QRect AnnotationTool::boundingBoxOf(const AnnotationShape &shape) const
    {
        QRect box;

        if (!shape.rect.isNull())
        {
            box = box.united(shape.rect);
        }
        for (const auto &point: shape.points)
        {
            box = box.united(QRect(point, QSize(1, 1)));
        }

        if (!shape.text.isEmpty())
        {
            // 文字的外接框按同一字号量出来：估算宽度会让选中框比实际画的字宽一截或窄一截
            QFont font;
            font.setPointSize(shape.fontSize);
            const QFontMetricsF metrics(font);
            const auto          width  = static_cast<int>(metrics.horizontalAdvance(shape.text));
            const auto          height = metrics.height();
            box                        = box.united(QRect(shape.position, QSize(width, height)));
        }

        return box;
    }

    void AnnotationTool::paint(QPainter &painter, const PaintContext &context) const
    {
        for (const auto &shape: m_shapes)
        {
            paintShape(painter, shape, context);
        }

        // 在途图形也画：用户要立刻看到自己正在框的东西，但它不进最终导出
        if (isActive() && hasPending())
        {
            paintShape(painter, m_pending, context);
        }
    }

    void AnnotationTool::applyToImage(QImage &image, const PaintContext &context) const
    {
        QPainter painter(&image);
        // 图形记的是逻辑坐标，底图是物理像素：缩放画家让两处用同一套数字，导出与屏显才不会分家
        painter.scale(context.pixelScale, context.pixelScale);
        painter.setRenderHint(QPainter::Antialiasing, true);
        for (const auto &shape: m_shapes)
        {
            paintShape(painter, shape, context);
        }
    }

    void AnnotationTool::setPenColor(const QColor &color) noexcept
    {
        m_color = color;
    }

    void AnnotationTool::setLineWidth(int width) noexcept
    {
        m_lineWidth = width;
    }

    void AnnotationTool::setFontSize(int size) noexcept
    {
        m_fontSize = size;
    }


    int AnnotationTool::lineWidth() const noexcept
    {
        return m_lineWidth;
    }

    int AnnotationTool::fontSize() const noexcept
    {
        return m_fontSize;
    }

} // namespace GyanisScribe::Annotation
