#include "Core/Geometry/ContinuousCorner.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr qreal kPi = 3.14159265358979323846;

    qreal toRadians(const qreal degrees) noexcept
    {
        return degrees * kPi / 180.0;
    }

    struct LocalCornerParameters
    {
        qreal a                = 0.0;
        qreal b                = 0.0;
        qreal c                = 0.0;
        qreal d                = 0.0;
        qreal p                = 0.0;
        qreal radius           = 0.0;
        qreal arcSectionLength = 0.0;
        qreal arcMeasure       = 0.0;
    };

    LocalCornerParameters makeLocalParameters(const QSizeF &size, const GyanisScribe::Core::CornerShape &shape)
    {
        // 夹取上界先收到非负：尺寸为负时 std::clamp 的下界会大于上界，属于未定义行为
        const qreal budget = std::max(0.0, std::min(size.width(), size.height()) / 2.0);
        const qreal radius = std::clamp(shape.radius, 0.0, budget);
        if (radius <= 0.0)
        {
            return {};
        }

        qreal smoothing              = std::clamp(shape.smoothing, 0.0, 1.0);
        smoothing                    = std::min(smoothing, budget / radius - 1.0);
        const qreal p                = std::min((1.0 + smoothing) * radius, budget);
        const qreal arcMeasure       = 90.0 * (1.0 - smoothing);
        const qreal arcSectionLength = std::sin(toRadians(arcMeasure / 2.0)) * radius * std::sqrt(2.0);
        const qreal angleAlpha       = (90.0 - arcMeasure) / 2.0;
        const qreal p3ToP4Distance   = radius * std::tan(toRadians(angleAlpha / 2.0));
        const qreal angleBeta        = 45.0 * smoothing;
        const qreal c                = p3ToP4Distance * std::cos(toRadians(angleBeta));
        const qreal d                = c * std::tan(toRadians(angleBeta));
        const qreal b                = (p - arcSectionLength - c - d) / 3.0;
        const qreal a                = 2.0 * b;
        return {a, b, c, d, p, radius, arcSectionLength, arcMeasure};
    }

    void appendCorner(QPainterPath &path, const QPointF &corner, const QPointF &outgoing, const QPointF &incoming, const LocalCornerParameters &params)
    {
        const QPointF start = corner - params.p * outgoing;
        path.lineTo(start);

        const QPointF firstEnd = start + (params.a + params.b + params.c) * outgoing + params.d * incoming;
        path.cubicTo(start + params.a * outgoing, start + (params.a + params.b) * outgoing, firstEnd);

        const QPointF secondStart = firstEnd + params.arcSectionLength * (outgoing + incoming);
        if (params.arcMeasure > 1e-9)
        {
            QPointF     tangent       = params.c * outgoing + params.d * incoming;
            const qreal tangentLength = std::hypot(tangent.x(), tangent.y());
            tangent                   = tangentLength > 1e-9 ? tangent / tangentLength : outgoing;

            // 屏幕坐标 y 向下，顺时针转 90° 的切法是把切向量换成 (-y, x)
            const QPointF normal{-tangent.y(), tangent.x()};
            const QPointF center     = firstEnd + params.radius * normal;
            const qreal   startAngle = std::atan2(-(firstEnd.y() - center.y()), firstEnd.x() - center.x()) * 180.0 / kPi;
            path.arcTo(QRectF{center.x() - params.radius, center.y() - params.radius, params.radius * 2.0, params.radius * 2.0}, startAngle, -params.arcMeasure);
        }

        const QPointF secondEnd = secondStart + params.d * outgoing + (params.a + params.b + params.c) * incoming;
        path.cubicTo(secondStart + params.d * outgoing + params.c * incoming, secondStart + params.d * outgoing + (params.b + params.c) * incoming, secondEnd);
    }
} // namespace

namespace GyanisScribe::Core
{
    CornerShape cornerShapeFor(const CornerRole role)
    {
        switch (role)
        {
            case CornerRole::Card:
                return {20.0, 0.80};
            case CornerRole::Panel:
                return {16.0, 0.70};
            case CornerRole::Control:
                return {12.0, 0.50};
            case CornerRole::Compact:
                return {8.0, 0.35};
            case CornerRole::Thumbnail:
                return {6.0, 0.30};
            case CornerRole::Indicator:
                return {5.0, 0.25};
            case CornerRole::Hairline:
                return {4.0, 0.20};
            case CornerRole::Handle:
                return {0.0, 0.0};
            case CornerRole::ContentTiny:
                return {3.0, 0.20};
        }
        return {0.0, 0.0};
    }

    CornerShape cornerShapeFor(const CornerRole role, const QSizeF &size)
    {
        if (role == CornerRole::Handle)
        {
            return {std::min(size.width(), size.height()) / 2.0, 0.0};
        }
        return cornerShapeFor(role);
    }

    CornerShape cornerShapeForRadius(const qreal radius)
    {
        if (radius <= 6.0)
        {
            return {radius, 0.30};
        }
        if (radius <= 12.0)
        {
            return {radius, 0.50};
        }
        if (radius <= 20.0)
        {
            return {radius, 0.65};
        }
        if (radius <= 32.0)
        {
            return {radius, 0.75};
        }
        return {radius, 0.85};
    }

    CornerPathParameters cornerPathParameters(const QSizeF &size, const CornerShape &shape)
    {
        const auto local = makeLocalParameters(size, shape);
        return {local.a, local.b, local.c, local.d, local.p, local.radius, local.arcSectionLength, local.arcMeasure};
    }

    QPainterPath continuousCornerPath(const QRectF &rect, const CornerShape &shape)
    {
        QPainterPath path;
        if (rect.isEmpty())
        {
            return path;
        }

        const auto params = makeLocalParameters(rect.size(), shape);
        if (params.radius <= 0.0)
        {
            path.addRect(rect);
            return path;
        }

        const QPointF topRight{rect.right(), rect.top()};
        const QPointF bottomRight{rect.right(), rect.bottom()};
        const QPointF bottomLeft{rect.left(), rect.bottom()};
        const QPointF topLeft{rect.left(), rect.top()};

        path.moveTo(topRight - params.p * QPointF{1.0, 0.0});
        appendCorner(path, topRight, {1.0, 0.0}, {0.0, 1.0}, params);
        appendCorner(path, bottomRight, {0.0, 1.0}, {-1.0, 0.0}, params);
        appendCorner(path, bottomLeft, {-1.0, 0.0}, {0.0, -1.0}, params);
        appendCorner(path, topLeft, {0.0, -1.0}, {1.0, 0.0}, params);
        path.closeSubpath();
        return path;
    }

    void paintContinuousCorner(QPainter &painter, const QRectF &rect, const CornerShape &shape, const QBrush &fill, const QPen &pen)
    {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (fill.style() != Qt::NoBrush)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawPath(continuousCornerPath(rect, shape));
        }

        const qreal penWidth = pen.style() == Qt::NoPen ? 0.0 : std::max(pen.widthF(), 1.0);
        if (penWidth > 0.0)
        {
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(continuousCornerPath(rect.adjusted(penWidth / 2.0, penWidth / 2.0, -penWidth / 2.0, -penWidth / 2.0), shape));
        }
        painter.restore();
    }

    void applyCornerMask(QImage &image, const CornerShape &shape)
    {
        if (image.isNull())
        {
            return;
        }

        QImage mask{image.size(), QImage::Format_ARGB32_Premultiplied};
        mask.fill(Qt::transparent);
        {
            QPainter maskPainter{&mask};
            maskPainter.setRenderHint(QPainter::Antialiasing, true);
            maskPainter.setPen(Qt::NoPen);
            maskPainter.setBrush(Qt::black);
            maskPainter.drawPath(continuousCornerPath(QRectF{mask.rect()}, shape));
        }
        QPainter painter{&image};
        painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        painter.drawImage(0, 0, mask);
    }
} // namespace GyanisScribe::Core
