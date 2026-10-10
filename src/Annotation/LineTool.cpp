#include "Annotation/LineTool.h"

#include "Annotation/AnnotationGeometry.h"
#include "Core/Text.h"

#include <QLineF>
#include <QPainter>
#include <QPen>

namespace GyanisScribe::Annotation
{
    namespace
    {
        // 直线两端最近距离：再短就只是一个点，交给画笔那一档更合适
        constexpr double kMinimumLineLength = 4.0;
    } // namespace

    LineTool::LineTool() : AnnotationTool(ToolKind::Line)
    {
    }

    QString LineTool::displayName() const
    {
        return GyanisScribe::Core::uiText("直线");
    }

    void LineTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        Q_UNUSED(context);

        if (shape.points.size() < 2)
        {
            return;
        }

        const QPointF start(shape.points.front());
        const QPointF end(shape.points.back());
        if (QLineF(start, end).length() < kMinimumLineLength)
        {
            return;
        }

        auto pen = QPen(shape.color);
        pen.setWidth(shape.lineWidth);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(start, end);
    }

    bool LineTool::hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const
    {
        if (shape.points.size() < 2)
        {
            return false;
        }
        const auto distance = squaredDistanceToSegment(point, shape.points.front(), shape.points.back());
        return distance <= static_cast<double>(tolerance) * tolerance;
    }

    bool LineTool::shapeIsUsable(const AnnotationShape &shape) const
    {
        if (shape.points.size() < 2)
        {
            return false;
        }
        return QLineF(shape.points.front(), shape.points.back()).length() >= kMinimumLineLength;
    }

} // namespace GyanisScribe::Annotation
