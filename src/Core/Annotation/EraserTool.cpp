#include "Core/Annotation/EraserTool.h"

#include "Core/Annotation/AnnotationGeometry.h"
#include "Base/Text.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>

#include <bit>
#include <cmath>
#include <cstdint>

namespace GyanisScribe::Core
{
    namespace
    {
        // 橡皮相对线宽的加宽倍数：与高亮同口径，滑块调线宽就是调橡皮的大小
        constexpr int kEraserWidthFactor = 6;

        /// FNV-1a 的 64 位基值与质数：把源图身份与几何混成一个缓存键
        constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
        constexpr std::uint64_t kFnvPrime       = 1099511628211ULL;

        /**
         * @brief 把一个 64 位量逐字节混进 FNV-1a 哈希
         * @param hash 当前哈希值
         * @param value 待混入的量
         * @return 混入后的哈希值
         */
        [[nodiscard]] std::uint64_t mixCacheKey(std::uint64_t hash, const std::uint64_t value) noexcept
        {
            for (int shift = 0; shift < 64; shift += 8)
            {
                hash ^= (value >> shift) & 0xFFULL;
                hash *= kFnvPrime;
            }
            return hash;
        }

        /**
         * @brief 算上一笔擦除成品的缓存键
         * @param shape 图形
         * @param context 绘制现场
         * @return 源图身份、倍比、线宽、椭圆档与整条轨迹一起混出的键
         * @details 轨迹逐点混入：采样点一变就是另一笔，绝不把旧笔迹的成品贴到新笔迹上。
         */
        [[nodiscard]] std::uint64_t eraserCacheKey(const AnnotationShape &shape, const PaintContext &context) noexcept
        {
            std::uint64_t key = kFnvOffsetBasis;
            key               = mixCacheKey(key, static_cast<std::uint64_t>(context.source.cacheKey()));
            key               = mixCacheKey(key, std::bit_cast<std::uint64_t>(static_cast<double>(context.pixelScale)));
            key               = mixCacheKey(key, static_cast<std::uint64_t>(shape.lineWidth));
            key               = mixCacheKey(key, static_cast<std::uint64_t>(shape.rect.x()));
            key               = mixCacheKey(key, static_cast<std::uint64_t>(shape.rect.y()));
            key               = mixCacheKey(key, static_cast<std::uint64_t>(shape.rect.width()));
            key               = mixCacheKey(key, static_cast<std::uint64_t>(shape.rect.height()));
            key               = mixCacheKey(key, shape.elliptical ? 1ULL : 0ULL);
            key               = mixCacheKey(key, static_cast<std::uint64_t>(shape.points.size()));
            for (const auto &point: shape.points)
            {
                key = mixCacheKey(key, static_cast<std::uint64_t>(point.x()));
                key = mixCacheKey(key, static_cast<std::uint64_t>(point.y()));
            }
            return key;
        }
    } // namespace

    EraserTool::EraserTool() : PenTool(ToolKind::Eraser)
    {
    }

    QString EraserTool::displayName() const
    {
        return GyanisScribe::Base::uiText("橡皮擦");
    }

    int EraserTool::cacheBuildCountForTesting() const noexcept
    {
        return m_cacheBuilds;
    }

    void EraserTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        const auto &source = context.source;
        if (source.isNull() || context.pixelScale <= 0.0 || shape.points.isEmpty())
        {
            return;
        }

        // 笔迹粗细按同一倍比换成物理像素：下面画掩膜用的是物理坐标
        const qreal eraseWidth = static_cast<qreal>(shape.lineWidth) * kEraserWidthFactor * context.pixelScale;

        // 采样点的逻辑外接框换成物理框，四周再各放一个笔迹半径：不放的话圆头两端会被切掉
        QRect logical(shape.points.front(), QSize(1, 1));
        for (const auto &point: shape.points)
        {
            logical = logical.united(QRect(point, QSize(1, 1)));
        }
        const int margin = static_cast<int>(std::ceil(eraseWidth / 2.0)) + 1;
        QRect     physical(static_cast<int>(std::floor(logical.left() * context.pixelScale)), static_cast<int>(std::floor(logical.top() * context.pixelScale)),
                           qMax(1, static_cast<int>(std::ceil(logical.width() * context.pixelScale))), qMax(1, static_cast<int>(std::ceil(logical.height() * context.pixelScale))));
        physical = physical.adjusted(-margin, -margin, margin, margin).intersected(source.rect());
        if (physical.isEmpty())
        {
            return;
        }

        // 物理框按同一倍比换回逻辑坐标贴回画布：屏显与导出各有一套画家变换，两处都落到同一块像素上
        const QRectF target(QPointF(physical.left() / context.pixelScale, physical.top() / context.pixelScale),
                            QSizeF(physical.width() / context.pixelScale, physical.height() / context.pixelScale));

        // 静止重绘轨迹没变：上一块成品原样贴回去，不必再画一遍掩膜、再抠一次图
        const std::uint64_t cacheKey = eraserCacheKey(shape, context);
        if (!m_lastPatch.isNull() && cacheKey == m_lastKey)
        {
            painter.drawImage(m_lastTarget, m_lastPatch);
            return;
        }

        // 掩膜：笔迹画成不透明，其它地方留空，之后按它的透明度把底图那一块抠出笔迹形状
        QImage mask(physical.size(), QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        {
            QPainter maskPainter(&mask);
            maskPainter.setRenderHint(QPainter::Antialiasing, true);
            // 掩膜里画的是相对物理框的坐标：采样点先乘倍比，再减去框左上角
            const auto toMaskPoint = [&physical, &context](const QPoint &point)
            { return QPointF(static_cast<qreal>(point.x()) * context.pixelScale - physical.left(), static_cast<qreal>(point.y()) * context.pixelScale - physical.top()); };

            maskPainter.setPen(Qt::NoPen);
            maskPainter.setBrush(Qt::black);
            if (shape.points.size() == 1)
            {
                // 单点落笔擦一个点，与画笔工具落点同一套口径；形状档决定圆点还是方点
                const QPointF center = toMaskPoint(shape.points.front());
                if (shape.elliptical)
                {
                    maskPainter.drawEllipse(center, eraseWidth / 2.0, eraseWidth / 2.0);
                } else
                {
                    maskPainter.drawRect(QRectF(center.x() - eraseWidth / 2.0, center.y() - eraseWidth / 2.0, eraseWidth, eraseWidth));
                }
            } else
            {
                auto pen = QPen(Qt::black);
                pen.setWidthF(eraseWidth);
                // 椭圆档圆头圆角、方档方头直角。方头用 SquareCap 而不是 FlatCap：
                // 与圆头同为「向外伸半个笔宽」，只换角的形状，不缩覆盖范围——平头会把拖动
                // 起点那一格削掉半像素，用起来就是「按下的地方没擦到」
                pen.setCapStyle(shape.elliptical ? Qt::RoundCap : Qt::SquareCap);
                pen.setJoinStyle(shape.elliptical ? Qt::RoundJoin : Qt::MiterJoin);
                maskPainter.setPen(pen);
                maskPainter.setBrush(Qt::NoBrush);

                QPainterPath path;
                path.moveTo(toMaskPoint(shape.points.front()));
                for (int index = 1; index < shape.points.size(); ++index)
                {
                    path.lineTo(toMaskPoint(shape.points[index]));
                }
                maskPainter.drawPath(path);
            }
        }

        // 抠图：掩膜为不透明的地方留下底图像素，其余透明度清零
        QImage patch = source.copy(physical).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        {
            QPainter patchPainter(&patch);
            patchPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            patchPainter.drawImage(0, 0, mask);
        }

        m_lastPatch  = patch;
        m_lastTarget = target;
        m_lastKey    = cacheKey;
        ++m_cacheBuilds;
        painter.drawImage(m_lastTarget, m_lastPatch);
    }

    bool EraserTool::hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const
    {
        const double widened = static_cast<double>(tolerance) * kEraserWidthFactor;
        return squaredDistanceToPolyline(point, shape.points) <= widened * widened;
    }

} // namespace GyanisScribe::Core
