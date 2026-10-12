#include "Annotation/ArrowTool.h"

#include "Annotation/AnnotationGeometry.h"
#include "Core/Text.h"

#include <QLineF>
#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace GyanisScribe::Annotation
{
    namespace
    {
        // 箭头两端的最近距离：再短就没有方向可言
        constexpr double kMinimumArrowLength = 4.0;

        // 箭头张角，弧度制。两翼各与箭杆成 30°，合计 60° 的常见标注箭头
        constexpr double kArrowHeadAngle = 0.52;

        // 箭头长度相对线宽的下限，短箭头的箭头不能比杆还长
        constexpr double kArrowLengthPerLineWidth = 3.5;

        /// 头部三角沿杆方向的深度：两翼顶点到尖端的轴向距离，箭杆只该画到这里
        constexpr double headDepthOf(const double headLength)
        {
            return headLength * std::cos(kArrowHeadAngle);
        }

        /**
         * @brief 画一个实心箭头三角
         * @param painter 目标画家，笔刷已设为箭头色
         * @param tip 尖端
         * @param angle 箭杆指向（从起点端看向尖端端）
         * @param headLength 两翼到尖端的距离
         * @details 两翼落在「从尖端沿杆往回转 ±kArrowHeadAngle 走 headLength」处：头长 14 时
         *          三角纵深 12、最宽 ±7，是常见的实心三角。曾经写成从「尖端往回 headLength
         *          的底边点」再朝外转 ±角度 走 headLength——那只把两翼顶到尖端旁 0.13×headLength 处，
         *          画出来的是一小片贴着尖端的扁平扇面，实际观感就是一根光杆（渲染探针核对过）。
         */
        void drawArrowHead(QPainter &painter, const QPointF &tip, const double angle, const double headLength)
        {
            const QPointF left  = tip - QPointF(std::cos(angle + kArrowHeadAngle), std::sin(angle + kArrowHeadAngle)) * headLength;
            const QPointF right = tip - QPointF(std::cos(angle - kArrowHeadAngle), std::sin(angle - kArrowHeadAngle)) * headLength;
            QPolygonF     head;
            head << tip << left << right;
            painter.drawPolygon(head);
        }
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

        const double  headLength = std::min(kArrowLengthPerLineWidth * shape.lineWidth, shaft.length() * 0.5);
        const double  angle      = std::atan2(shaft.dy(), shaft.dx());
        const QPointF unit(std::cos(angle), std::sin(angle));
        const QPointF headSink = unit * headDepthOf(headLength);

        // 箭杆只画到箭头底边那一处，否则杆头会从三角中间冒出来；双头箭杆两头都收
        painter.drawLine(shape.doubleHeaded ? start + headSink : start, end - headSink);
        painter.setPen(Qt::NoPen);
        painter.setBrush(shape.color);

        drawArrowHead(painter, end, angle, headLength);
        if (shape.doubleHeaded)
        {
            // 起点端的头指向相反：从起点端看出去，仍是「从杆尾指向这一头」
            drawArrowHead(painter, start, angle + std::numbers::pi, headLength);
        }
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
