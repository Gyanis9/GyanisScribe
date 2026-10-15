#include "Core/Annotation/HighlightTool.h"

#include "Core/Annotation/AnnotationGeometry.h"
#include "Base/Text.h"

#include <QPainter>
#include <QPainterPath>

namespace GyanisScribe::Core
{
    HighlightTool::HighlightTool() : PenTool(ToolKind::Highlight)
    {
    }

    QString HighlightTool::displayName() const
    {
        return GyanisScribe::Base::uiText("高亮");
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
        // 加宽倍数记在每条图形自己身上（窄/中/宽三档）：工具上那一份只是后续笔画的默认值
        pen.setWidth(shape.lineWidth * shape.highlightWidthFactor);
        // 椭圆档换圆头圆角：方档是平头直角，两头的切法一眼可辨
        pen.setCapStyle(shape.elliptical ? Qt::RoundCap : Qt::FlatCap);
        pen.setJoinStyle(shape.elliptical ? Qt::RoundJoin : Qt::MiterJoin);
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
        const double widened = static_cast<double>(tolerance) * shape.highlightWidthFactor;
        return squaredDistanceToPolyline(point, shape.points) <= widened * widened;
    }

} // namespace GyanisScribe::Core
