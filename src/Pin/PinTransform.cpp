#include "Pin/PinTransform.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace GyanisScribe::Pin
{

    double clampScale(double scale) noexcept
    {
        return std::clamp(scale, kMinimumScale, kMaximumScale);
    }

    double clampOpacity(double opacity) noexcept
    {
        return std::clamp(opacity, kMinimumOpacity, 1.0);
    }

    QSizeF windowSizeFor(const QSizeF &logicalSize, double scale) noexcept
    {
        // 至少留 1×1：零尺寸的顶层窗口既画不出东西也接不到鼠标事件
        return QSizeF(std::max(1.0, logicalSize.width() * clampScale(scale)), std::max(1.0, logicalSize.height() * clampScale(scale)));
    }

    QRectF rotatedBoundingBox(const QPointF &center, const QSizeF &size, double degrees)
    {
        const double radians = degrees * std::numbers::pi_v<double> / 180.0;
        const double cosine  = std::abs(std::cos(radians));
        const double sine    = std::abs(std::sin(radians));

        // 外接框 = 宽高各自投影相加；取绝对值让四个象限的旋转都得到同一个框
        const double width  = size.width() * cosine + size.height() * sine;
        const double height = size.width() * sine + size.height() * cosine;
        return QRectF(center.x() - width / 2.0, center.y() - height / 2.0, width, height);
    }

    std::vector<QRectF> handleRects(const QRectF &target, double handleSize)
    {
        const double  half = handleSize / 2.0;
        const QPointF centerX(target.center().x(), 0.0);
        const QPointF centerY(0.0, target.center().y());

        const auto at = [half](const QPointF &anchor) { return QRectF(anchor.x() - half, anchor.y() - half, half * 2.0, half * 2.0); };

        // 顺序必须与 HandleKind 一致：布局与命中都按这个下标取
        return {
                at(target.topLeft()),     at(QPointF(centerX.x(), target.top())),    at(target.topRight()),   at(QPointF(target.right(), centerY.y())),
                at(target.bottomRight()), at(QPointF(centerX.x(), target.bottom())), at(target.bottomLeft()), at(QPointF(target.left(), centerY.y())),
        };
    }

    double scaleFromDrag(double initialScale, double initialDistance, double currentDistance) noexcept
    {
        if (initialDistance <= 0.0)
        {
            // 在中心点上按下：没有比值可算，保持原样而不是把贴图瞬间缩成看不见
            return clampScale(initialScale);
        }
        return clampScale(initialScale * (currentDistance / initialDistance));
    }

    double normalizeDegrees(double degrees) noexcept
    {
        double folded = std::fmod(degrees, 360.0);
        if (folded < 0.0)
        {
            folded += 360.0;
        }
        return folded;
    }

} // namespace GyanisScribe::Pin
