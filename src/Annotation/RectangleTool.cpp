#include "Annotation/RectangleTool.h"
#include "Annotation/StrokePen.h"

#include "Annotation/FillBrush.h"
#include "Core/Text.h"

#include <QPainter>

namespace GyanisScribe::Annotation
{

    RectangleTool::RectangleTool() : AnnotationTool(ToolKind::Rectangle)
    {
    }

    QString RectangleTool::displayName() const
    {
        return GyanisScribe::Core::uiText("矩形");
    }

    void RectangleTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        Q_UNUSED(context);

        auto pen = strokePenFor(shape);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(fillBrushFor(shape));
        painter.drawRect(shape.rect);
    }

} // namespace GyanisScribe::Annotation
