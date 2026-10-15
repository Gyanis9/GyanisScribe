#include "Core/Annotation/EllipseTool.h"
#include "Core/Annotation/StrokePen.h"

#include "Core/Annotation/FillBrush.h"
#include "Base/Text.h"

#include <QPainter>

#include <cmath>

namespace GyanisScribe::Core
{

    EllipseTool::EllipseTool() : AnnotationTool(ToolKind::Ellipse)
    {
    }

    QString EllipseTool::displayName() const
    {
        return GyanisScribe::Base::uiText("椭圆");
    }

    void EllipseTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        Q_UNUSED(context);

        auto pen = strokePenFor(shape);
        painter.setPen(pen);
        painter.setBrush(fillBrushFor(shape));
        painter.drawEllipse(shape.rect);
    }

    bool EllipseTool::hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const
    {
        const auto box = shape.rect;
        if (box.width() <= 0 || box.height() <= 0)
        {
            return false;
        }

        // 归一化到单位圆后再比半径：环带厚度按容差换算，细线椭圆与粗线椭圆点起来手感一致
        const double centerX  = box.center().x();
        const double centerY  = box.center().y();
        const double radiusX  = box.width() / 2.0;
        const double radiusY  = box.height() / 2.0;
        const double dx       = (point.x() - centerX) / radiusX;
        const double dy       = (point.y() - centerY) / radiusY;
        const double distance = std::sqrt(dx * dx + dy * dy);

        const double bandWidth = static_cast<double>(tolerance) / qMin(radiusX, radiusY);
        return std::abs(distance - 1.0) <= bandWidth;
    }

} // namespace GyanisScribe::Core
