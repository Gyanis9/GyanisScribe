#include "Annotation/RectangleTool.h"

#include <QPainter>

namespace GyanisScribe::Annotation
{

    RectangleTool::RectangleTool() : AnnotationTool(ToolKind::Rectangle)
    {
    }

    QString RectangleTool::displayName() const
    {
        return QStringLiteral("矩形");
    }

    void RectangleTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        Q_UNUSED(context);

        auto pen = QPen(shape.color);
        pen.setWidth(shape.lineWidth);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(shape.rect);
    }

} // namespace GyanisScribe::Annotation
