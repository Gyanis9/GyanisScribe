#include "Annotation/PenTool.h"
#include "Annotation/StrokePen.h"

#include "Annotation/AnnotationGeometry.h"
#include "Base/Text.h"

#include <QPainter>
#include <QPainterPath>

namespace GyanisScribe::Annotation
{
    namespace
    {
        // 采样点之间的最小间距：小于这个距离的抖动不记录，笔迹与内存都干净
        constexpr double kMinimumSampleSpacing = 2.0;
    } // namespace

    PenTool::PenTool() : AnnotationTool(ToolKind::Pen)
    {
    }

    PenTool::PenTool(ToolKind kind) : AnnotationTool(kind)
    {
    }

    QString PenTool::displayName() const
    {
        return GyanisScribe::Base::uiText("画笔");
    }

    void PenTool::begin(const QPoint &point)
    {
        AnnotationTool::begin(point);
        // 基类起的是首尾重合的两点，轨迹只要一个起笔点
        m_pending.points = {point};
    }

    void PenTool::update(const QPoint &point)
    {
        if (m_pending.points.isEmpty())
        {
            m_pending.points = {point};
            return;
        }

        const auto &last = m_pending.points.back();
        const int   dx   = point.x() - last.x();
        const int   dy   = point.y() - last.y();
        if (static_cast<double>(dx) * dx + static_cast<double>(dy) * dy < kMinimumSampleSpacing * kMinimumSampleSpacing)
        {
            return;
        }
        m_pending.points.append(point);
        m_pending.rect = m_pending.rect.united(QRect(point, QSize(1, 1)));
    }

    void PenTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        Q_UNUSED(context);
        paintPolyline(painter, shape);
    }

    void PenTool::paintPolyline(QPainter &painter, const AnnotationShape &shape) const
    {
        if (shape.points.size() < 2)
        {
            // 单点落笔也画一个小圆点，否则用户点了却什么都看不到
            if (shape.points.size() == 1)
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(shape.color);
                painter.drawEllipse(QPointF(shape.points.front()), shape.lineWidth / 2.0, shape.lineWidth / 2.0);
            }
            return;
        }

        auto pen = strokePenFor(shape, Qt::RoundCap);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);

        QPainterPath path;
        path.moveTo(shape.points.front());
        for (int index = 1; index < shape.points.size(); ++index)
        {
            path.lineTo(shape.points[index]);
        }
        painter.drawPath(path);
    }

    bool PenTool::hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const
    {
        return squaredDistanceToPolyline(point, shape.points) <= static_cast<double>(tolerance) * tolerance;
    }

    bool PenTool::shapeIsUsable(const AnnotationShape &shape) const
    {
        return !shape.points.isEmpty();
    }

} // namespace GyanisScribe::Annotation
