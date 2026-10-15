#include "Core/Annotation/MosaicTool.h"

#include "Base/Text.h"

#include <QImage>
#include <QPainter>

namespace GyanisScribe::Core
{
    MosaicTool::MosaicTool() : AnnotationTool(ToolKind::Mosaic)
    {
    }

    QString MosaicTool::displayName() const
    {
        return GyanisScribe::Base::uiText("马赛克");
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
            painter.drawImage(shape.rect, upscaled);
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
        painter.drawImage(shape.rect, masked);
    }

} // namespace GyanisScribe::Core
