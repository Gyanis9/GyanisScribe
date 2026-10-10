#include "Annotation/ArrowTool.h"

#include "Annotation/AnnotationGeometry.h"
#include "Core/Text.h"

#include <QLineF>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace GyanisScribe::Annotation
{
    namespace
    {
        // 箭头两端的最近距离：再短就没有方向可言
        constexpr double kMinimumArrowLength = 4.0;

        // 箭头张角，弧度制。取 30° 一侧，视觉上与常见标注箭头一致
        constexpr double kArrowHeadAngle = 0.52;

        // 箭头长度相对线宽的下限，短箭头的箭头不能比杆还长
        constexpr double kArrowLengthPerLineWidth = 3.5;
    } // namespace

    ArrowTool::ArrowTool() : AnnotationTool(ToolKind::Arrow)
    {
    }

    QString ArrowTool::displayName() const
    {
        return GyanisScribe::Core::uiText("箭头");
    }

    void ArrowTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        Q_UNUSED(context);

        if (shape.points.size() < 2)
        {
            return;
        }

        const QPointF start(shape.points.front());
        const QPointF end(shape.points.back());
        const QLineF  shaft(start, end);
        if (shaft.length() < kMinimumArrowLength)
        {
            return;
        }

        auto pen = QPen(shape.color);
        pen.setWidth(shape.lineWidth);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(shape.color);

        // 箭杆只画到箭头底边那一处，否则杆头会从三角中间冒出来
        const double  headLength = std::min(kArrowLengthPerLineWidth * shape.lineWidth, shaft.length() * 0.5);
        const double  angle      = std::atan2(shaft.dy(), shaft.dx());
        const QPointF base       = end - QPointF(std::cos(angle) * headLength, std::sin(angle) * headLength);
        painter.drawLine(start, end);
        painter.setPen(Qt::NoPen);

        const QPointF left  = base + QPointF(std::cos(angle + kArrowHeadAngle), std::sin(angle + kArrowHeadAngle)) * headLength;
        const QPointF right = base + QPointF(std::cos(angle - kArrowHeadAngle), std::sin(angle - kArrowHeadAngle)) * headLength;
        QPolygonF     head;
        head << end << left << right;
        painter.drawPolygon(head);
    }

    bool ArrowTool::hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const
    {
        if (shape.points.size() < 2)
        {
            return false;
        }
        const auto distance = squaredDistanceToSegment(point, shape.points.front(), shape.points.back());
        return distance <= static_cast<double>(tolerance) * tolerance;
    }

    bool ArrowTool::shapeIsUsable(const AnnotationShape &shape) const
    {
        if (shape.points.size() < 2)
        {
            return false;
        }
        const QLineF shaft(shape.points.front(), shape.points.back());
        return shaft.length() >= kMinimumArrowLength;
    }

} // namespace GyanisScribe::Annotation
