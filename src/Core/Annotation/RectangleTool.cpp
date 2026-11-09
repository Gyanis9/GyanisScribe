#include "Core/Annotation/RectangleTool.h"
#include "Core/Annotation/StrokePen.h"

#include "Core/Annotation/FillBrush.h"
#include "Core/Geometry/ContinuousCorner.h"
#include "Base/Text.h"

#include <QPainter>

#include <algorithm>

namespace GyanisScribe::Core
{
    namespace
    {
        /// 圆角半径相对短边的比例：太大窄条会长成一粒胶囊，太小又看不出圆
        constexpr double kRoundedRectRadiusRatio = 0.18;
    } // namespace

    RectangleTool::RectangleTool() : AnnotationTool(ToolKind::Rectangle)
    {
    }

    QString RectangleTool::displayName() const
    {
        return GyanisScribe::Base::uiText("矩形");
    }

    void RectangleTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        Q_UNUSED(context);

        auto pen = strokePenFor(shape);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.setBrush(fillBrushFor(shape));

        if (!shape.roundedRect)
        {
            painter.drawRect(shape.rect);
            return;
        }

        // 圆角按短边算：长条上按长边给半径会把两端收成一粒胶囊
        const double radius = std::min(shape.rect.width(), shape.rect.height()) * kRoundedRectRadiusRatio;
        // 半径只决定形状大小，平滑度按半径分档：统一交给连续曲率路径，圆弧与过渡段一起算
        painter.drawPath(continuousCornerPath(QRectF{shape.rect}, cornerShapeForRadius(radius)));
    }

} // namespace GyanisScribe::Core
