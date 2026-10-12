#include "Annotation/HighlightTool.h"

#include "Annotation/AnnotationGeometry.h"
#include "Core/Text.h"

#include <QPainter>
#include <QPainterPath>

namespace GyanisScribe::Annotation
{
    namespace
    {
        // 高亮相对线宽的加宽倍数：一支笔要能盖住一行字（宽度的档不随图形走，透明度才随）
        constexpr int kHighlightWidthFactor = 4;
    } // namespace

    HighlightTool::HighlightTool() : PenTool(ToolKind::Highlight)
    {
    }

    QString HighlightTool::displayName() const
    {
        return GyanisScribe::Core::uiText("高亮");
    }

    void HighlightTool::paintPolyline(QPainter &painter, const AnnotationShape &shape) const
    {
        if (shape.points.size() < 2)
        {
            return;
        }

        auto color = shape.color;
        color.setAlpha(shape.highlightAlpha);

        auto pen = QPen(color);
        pen.setWidth(shape.lineWidth * kHighlightWidthFactor);
        pen.setCapStyle(Qt::FlatCap);
        pen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(pen);

        QPainterPath path;
        path.moveTo(shape.points.front());
        for (int index = 1; index < shape.points.size(); ++index)
        {
            path.lineTo(shape.points[index]);
        }
        painter.drawPath(path);
    }

    bool HighlightTool::hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const
    {
        const double widened = static_cast<double>(tolerance) * kHighlightWidthFactor;
        return squaredDistanceToPolyline(point, shape.points) <= widened * widened;
    }

} // namespace GyanisScribe::Annotation
