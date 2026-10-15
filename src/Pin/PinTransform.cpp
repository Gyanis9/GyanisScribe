#include "Pin/PinTransform.h"

#include "Core/Text.h"

#include <QLineF>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
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

    double clampThumbnailEdge(const double edge) noexcept
    {
        return std::clamp(edge, kMinimumThumbnailEdge, kMaximumThumbnailEdge);
    }

    double thumbnailEdgeFromDrag(const double initialEdge, const double initialDistance, const double currentDistance) noexcept
    {
        if (initialDistance <= 0.0)
        {
            // 与 scaleFromDrag 同一条口径：在中心点上按下没有比值可算，保持原边长而不是瞬间缩没
            return clampThumbnailEdge(initialEdge);
        }
        return clampThumbnailEdge(initialEdge * (currentDistance / initialDistance));
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

    std::optional<QPoint> imagePixelAt(const QPointF &windowPoint, const QRectF &content, const QSize &imagePixelSize, const double degrees, const bool mirroredHorizontally,
                                       const bool mirroredVertically)
    {
        if (content.width() <= 0.0 || content.height() <= 0.0 || imagePixelSize.isEmpty())
        {
            return std::nullopt;
        }

        // 绘制那一步是「绕画布中心转 +degrees」，这里按 -degrees 转回去。
        // 坐标轴是屏幕那套（y 向下），所以反变换里的 sin 号与教科书写法相反
        const QPointF center  = content.center();
        const QPointF vector  = windowPoint - center;
        const double  radians = -degrees * std::numbers::pi_v<double> / 180.0;
        const double  cosine  = std::cos(radians);
        const double  sine    = std::sin(radians);
        const QPointF unrotated(center.x() + vector.x() * cosine - vector.y() * sine, center.y() + vector.x() * sine + vector.y() * cosine);

        // 绘制在旋转之前先按中心翻转（转的是翻好的那一份），这里按同一中心翻回来
        const QPointF unmirrored(mirroredHorizontally ? 2.0 * center.x() - unrotated.x() : unrotated.x(), mirroredVertically ? 2.0 * center.y() - unrotated.y() : unrotated.y());

        const double u = (unmirrored.x() - content.left()) / content.width();
        const double v = (unmirrored.y() - content.top()) / content.height();
        if (u < 0.0 || u >= 1.0 || v < 0.0 || v >= 1.0)
        {
            return std::nullopt;
        }

        // 画布矩形里是整张物理图被缩放着画进去的：按比例折进像素格，右/下边界取不到（u、v 已排除 1）
        return QPoint(static_cast<int>(u * imagePixelSize.width()), static_cast<int>(v * imagePixelSize.height()));
    }

    std::optional<QRect> imagePixelRect(const QRectF &windowRect, const QRectF &content, const QSize &imagePixelSize)
    {
        if (content.width() <= 0.0 || content.height() <= 0.0 || imagePixelSize.isEmpty())
        {
            return std::nullopt;
        }

        const QRectF clamped = windowRect.normalized().intersected(content);
        if (clamped.width() <= 0.0 || clamped.height() <= 0.0)
        {
            return std::nullopt;
        }

        // 左/上边界下取整（框压到哪一格就算哪一格）、右/下边界上取整再退一格（边界线本身不算新的一列）
        const auto fold = [](const double position, const double origin, const double span, const int pixels) { return (position - origin) / span * pixels; };
        const int  x0   = std::clamp(static_cast<int>(std::floor(fold(clamped.left(), content.left(), content.width(), imagePixelSize.width()))), 0, imagePixelSize.width() - 1);
        const int  y0   = std::clamp(static_cast<int>(std::floor(fold(clamped.top(), content.top(), content.height(), imagePixelSize.height()))), 0, imagePixelSize.height() - 1);
        const int  x1 = std::clamp(static_cast<int>(std::ceil(fold(clamped.right(), content.left(), content.width(), imagePixelSize.width()))) - 1, 0, imagePixelSize.width() - 1);
        const int  y1 =
                std::clamp(static_cast<int>(std::ceil(fold(clamped.bottom(), content.top(), content.height(), imagePixelSize.height()))) - 1, 0, imagePixelSize.height() - 1);

        return QRect(QPoint(x0, y0), QPoint(x1, y1));
    }

    QSize sizeScaledBy(const QSize &current, const double ratio)
    {
        if (ratio <= 0.0)
        {
            return current;
        }
        return QSize{std::max(1, static_cast<int>(std::lround(current.width() * ratio))), std::max(1, static_cast<int>(std::lround(current.height() * ratio)))};
    }

    Core::Expected<int> parseEdgeSnapDistance(const int distance)
    {
        if (distance >= kEdgeSnapDistanceMinimum && distance <= kEdgeSnapDistanceMaximum)
        {
            return distance;
        }
        return Core::Expected<int>{std::unexpected{Core::Error{
                .code    = Core::ErrorCode::InvalidArgument,
                .message = GyanisScribe::Core::uiFormat(
                        GyanisScribe::Core::uiTextUtf8("吸附距离 \"{}\" 不在 {} 到 {} 之间：请在设置的贴图页重填这一格，或删掉 Pin/EdgeSnapDistance 这一项回到默认的 {} 格"),
                        distance, kEdgeSnapDistanceMinimum, kEdgeSnapDistanceMaximum, kEdgeSnapTolerance),
        }}};
    }

    EdgeSnap snapToScreenEdges(const QRect &draggedRect, const QRect &available, const int tolerance)
    {
        if (available.isEmpty() || draggedRect.isEmpty())
        {
            return EdgeSnap{draggedRect, ScreenEdge::None};
        }

        // 先把整块夹进可用区：夹取之前能把贴图整个拖出屏幕，从此用户再也点不到它
        const auto maxX = std::max(available.left(), available.right() - draggedRect.width() + 1);
        const auto maxY = std::max(available.top(), available.bottom() - draggedRect.height() + 1);
        auto rect = QRect(std::clamp(draggedRect.left(), available.left(), maxX), std::clamp(draggedRect.top(), available.top(), maxY), draggedRect.width(), draggedRect.height());

        // 挨着哪条边就贴哪条；两条边同时够近（小窗口拖到角上）时先认左右再认上下
        EdgeSnap snap;
        snap.rect = rect;
        if (std::abs(rect.left() - available.left()) <= tolerance)
        {
            rect.moveLeft(available.left());
            snap.edge = ScreenEdge::Left;
        } else if (std::abs(available.right() - rect.right()) <= tolerance)
        {
            rect.moveLeft(available.right() - rect.width() + 1);
            snap.edge = ScreenEdge::Right;
        }
        if (std::abs(rect.top() - available.top()) <= tolerance)
        {
            rect.moveTop(available.top());
            snap.edge = snap.edge == ScreenEdge::None ? ScreenEdge::Top : snap.edge;
        } else if (std::abs(available.bottom() - rect.bottom()) <= tolerance)
        {
            rect.moveTop(available.bottom() - rect.height() + 1);
            snap.edge = snap.edge == ScreenEdge::None ? ScreenEdge::Bottom : snap.edge;
        }
        snap.rect = rect;
        return snap;
    }

    QRect snapToCandidateEdges(const QRect &draggedRect, const std::vector<QRect> &candidates, const int tolerance)
    {
        if (candidates.empty() || tolerance < 0)
        {
            return draggedRect;
        }

        // 候选给出的线与本矩形的一一对齐都算一次位移，取绝对值最小的那一条；
        // 中心线也进去，是因为「两张同宽的图并排」靠的是左右边，而「一窄一宽居中摆齐」靠的是中心
        std::vector<int> xTargets;
        std::vector<int> yTargets;
        xTargets.reserve(candidates.size() * 3);
        yTargets.reserve(candidates.size() * 3);
        for (const auto &candidate: candidates)
        {
            const auto center = candidate.center();
            xTargets.push_back(candidate.left());
            xTargets.push_back(center.x());
            xTargets.push_back(candidate.right());
            yTargets.push_back(candidate.top());
            yTargets.push_back(center.y());
            yTargets.push_back(candidate.bottom());
        }

        const auto       draggedCenter = draggedRect.center();
        const std::array xLines{draggedRect.left(), draggedCenter.x(), draggedRect.right()};
        const std::array yLines{draggedRect.top(), draggedCenter.y(), draggedRect.bottom()};

        // 每轴一个最佳位移：容差之内取最小的那一档；都没有就这一轴不动
        const auto bestDelta = [&tolerance](const std::array<int, 3> &sources, const std::vector<int> &targets)
        {
            std::optional<int> best;
            for (const auto target: targets)
            {
                for (const auto source: sources)
                {
                    const auto delta = target - source;
                    if (std::abs(delta) > tolerance)
                    {
                        continue;
                    }
                    if (!best || std::abs(delta) < std::abs(*best))
                    {
                        best = delta;
                    }
                }
            }
            return best;
        };

        auto shifted = draggedRect;
        shifted.translate(bestDelta(xLines, xTargets).value_or(0), bestDelta(yLines, yTargets).value_or(0));
        return shifted;
    }

    QRect dockedRect(const QRect &revealed, const QRect &available, const ScreenEdge edge, const bool collapsed, const int stripWidth)
    {
        if (edge == ScreenEdge::None || available.isEmpty())
        {
            return revealed;
        }
        if (!collapsed)
        {
            return revealed;
        }

        auto rect = revealed;
        switch (edge)
        {
            case ScreenEdge::Left:
                rect.moveLeft(available.left() - revealed.width() + stripWidth);
                break;
            case ScreenEdge::Right:
                rect.moveLeft(available.right() - stripWidth + 1);
                break;
            case ScreenEdge::Top:
                rect.moveTop(available.top() - revealed.height() + stripWidth);
                break;
            case ScreenEdge::Bottom:
                rect.moveTop(available.bottom() - stripWidth + 1);
                break;
            case ScreenEdge::None:
                break;
        }
        return rect;
    }

    std::optional<CheckerColors> checkerColorsFor(const TransparentBackdrop mode)
    {
        switch (mode)
        {
            case TransparentBackdrop::DarkChecker:
                return CheckerColors{.first = QColor{56, 56, 56}, .second = QColor{88, 88, 88}};
            case TransparentBackdrop::LightChecker:
                return CheckerColors{.first = QColor{255, 255, 255}, .second = QColor{204, 204, 204}};
            case TransparentBackdrop::ThemeBase:
            case TransparentBackdrop::SeeThrough:
                // 这两档不画棋盘：一档露窗口底色，一档让桌面透出来
                return std::nullopt;
        }
        return std::nullopt;
    }

    QImage checkerboardTile(const QColor &first, const QColor &second, const int cell)
    {
        if (cell <= 0)
        {
            return {};
        }

        // 贴片是 2×2 格：平铺之后相邻的格子交替两色，就是棋盘
        QImage tile{QSize{cell * 2, cell * 2}, QImage::Format_ARGB32_Premultiplied};
        tile.fill(first);
        {
            QPainter painter{&tile};
            painter.setPen(Qt::NoPen);
            painter.setBrush(second);
            painter.drawRect(QRect{0, cell, cell, cell});
            painter.drawRect(QRect{cell, 0, cell, cell});
        }
        return tile;
    }

} // namespace GyanisScribe::Pin
