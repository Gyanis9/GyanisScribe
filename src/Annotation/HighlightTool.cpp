#include "Annotation/HighlightTool.h"

#include "Annotation/AnnotationGeometry.h"
#include "Core/Text.h"

#include <QPainter>
#include <QPainterPath>

namespace GyanisScribe::Annotation
{
    namespace
    {
        // 高亮相对线宽的加宽倍数：一支笔要能盖住一行字
        constexpr int kHighlightWidthFactor = 4;

        // 高亮颜色的透明度（0-255 的不透明度）：压住底色又要让字透出来
        constexpr int kHighlightAlpha = 110;
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
        color.setAlpha(kHighlightAlpha);

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
