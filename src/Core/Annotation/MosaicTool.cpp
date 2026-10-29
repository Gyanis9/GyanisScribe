#include "Core/Annotation/MosaicTool.h"

#include "Base/Text.h"

#include <QImage>
#include <QPainter>

#include <bit>
#include <cstdint>

namespace GyanisScribe::Core
{
    namespace
    {
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
         * @brief 算上一笔马赛克的缓存键
         * @param shape 图形
         * @param context 绘制现场
         * @return 源图身份、倍比与影响成品的几何一起混出的键
         * @details 倍比取位模式而不是容差比较：同一形状的重复重绘走的是同一条算式，位模式必然一致；
         *          源图身份就是 cacheKey，换底图后旧成品不会再被贴上。
         */
        [[nodiscard]] std::uint64_t mosaicCacheKey(const AnnotationShape &shape, const PaintContext &context) noexcept
        {
            std::uint64_t key = kFnvOffsetBasis;
            key               = mixCacheKey(key, static_cast<std::uint64_t>(context.source.cacheKey()));
            key               = mixCacheKey(key, std::bit_cast<std::uint64_t>(static_cast<double>(context.pixelScale)));
            key               = mixCacheKey(key, static_cast<std::uint64_t>(shape.rect.x()));
            key               = mixCacheKey(key, static_cast<std::uint64_t>(shape.rect.y()));
            key               = mixCacheKey(key, static_cast<std::uint64_t>(shape.rect.width()));
            key               = mixCacheKey(key, static_cast<std::uint64_t>(shape.rect.height()));
            key               = mixCacheKey(key, static_cast<std::uint64_t>(shape.mosaicBlockSize));
            key               = mixCacheKey(key, static_cast<std::uint64_t>(shape.lineWidth));
            key               = mixCacheKey(key, shape.elliptical ? 1ULL : 0ULL);
            return key;
        }
    } // namespace

    MosaicTool::MosaicTool() : AnnotationTool(ToolKind::Mosaic)
    {
    }

    QString MosaicTool::displayName() const
    {
        return GyanisScribe::Base::uiText("马赛克");
    }

    int MosaicTool::cacheBuildCountForTesting() const noexcept
    {
        return m_cacheBuilds;
    }

    void MosaicTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        const auto &source = context.source;
        if (source.isNull() || context.pixelScale <= 0.0 || shape.rect.width() <= 0 || shape.rect.height() <= 0)
        {
            return;
        }

        // 逻辑框换成物理框：源图是物理像素，取错了地方就打到了隔壁那块内容
        QRect physical(qRound(shape.rect.x() * context.pixelScale), qRound(shape.rect.y() * context.pixelScale), qMax(1, qRound(shape.rect.width() * context.pixelScale)),
                       qMax(1, qRound(shape.rect.height() * context.pixelScale)));
        physical = physical.intersected(source.rect());
        if (physical.isEmpty())
        {
            return;
        }

        // 静止重绘几何没变：上一笔成品原样贴回去，不必再跑一遍两段缩放
        const std::uint64_t cacheKey = mosaicCacheKey(shape, context);
        if (!m_lastImage.isNull() && cacheKey == m_lastKey)
        {
            painter.drawImage(m_lastTarget, m_lastImage);
            return;
        }

        // 方块边长按图形自己的粒度档换算成物理像素；夹住分母，缩放比例极小或档位取到边界时不做除零
        const int    blockPixels = qMax(1, qRound(shape.mosaicBlockSize * context.pixelScale));
        const int    blocksWide  = qMax(1, physical.width() / blockPixels);
        const int    blocksTall  = qMax(1, physical.height() / blockPixels);
        const QImage region      = source.copy(physical);
        // 两段都用 FastTransformation：任何一段带平滑都会把方块糊回可读的细节
        const QImage downscaled = region.scaled(blocksWide, blocksTall, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        const QImage upscaled   = downscaled.scaled(physical.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);

        if (!shape.elliptical)
        {
            m_lastImage  = upscaled;
            m_lastTarget = shape.rect;
            m_lastKey    = cacheKey;
            ++m_cacheBuilds;
            painter.drawImage(m_lastTarget, m_lastImage);
            return;
        }

        // 椭圆档：把方块图裁进内切椭圆再贴，椭圆之外一个像素都不碰。走掩膜而不是 setClipPath——
        // 裁剪边不带抗锯齿，椭圆弧上会看见一圈台阶
        QImage masked = upscaled.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QImage mask(masked.size(), QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        {
            QPainter maskPainter(&mask);
            maskPainter.setRenderHint(QPainter::Antialiasing, true);
            maskPainter.setPen(Qt::NoPen);
            maskPainter.setBrush(Qt::black);
            maskPainter.drawEllipse(QRectF(mask.rect()));
        }
        {
            QPainter maskedPainter(&masked);
            maskedPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            maskedPainter.drawImage(0, 0, mask);
        }

        m_lastImage  = masked;
        m_lastTarget = shape.rect;
        m_lastKey    = cacheKey;
        ++m_cacheBuilds;
        painter.drawImage(m_lastTarget, m_lastImage);
    }

} // namespace GyanisScribe::Core
