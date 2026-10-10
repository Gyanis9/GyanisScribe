#include "Annotation/MosaicTool.h"

#include "Core/Text.h"

#include <QImage>
#include <QPainter>

namespace GyanisScribe::Annotation
{
    namespace
    {
        // 一个马赛克方块的边长，逻辑像素
        constexpr int kMosaicBlockSizeLogical = 12;
    } // namespace

    MosaicTool::MosaicTool() : AnnotationTool(ToolKind::Mosaic)
    {
    }

    QString MosaicTool::displayName() const
    {
        return GyanisScribe::Core::uiText("马赛克");
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

        const int    blocksWide = qMax(1, physical.width() / (qRound(kMosaicBlockSizeLogical * context.pixelScale)));
        const int    blocksTall = qMax(1, physical.height() / (qRound(kMosaicBlockSizeLogical * context.pixelScale)));
        const QImage region     = source.copy(physical);
        // 两段都用 FastTransformation：任何一段带平滑都会把方块糊回可读的细节
        const QImage downscaled = region.scaled(blocksWide, blocksTall, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        const QImage upscaled   = downscaled.scaled(physical.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);

        painter.drawImage(shape.rect, upscaled);
    }

} // namespace GyanisScribe::Annotation
