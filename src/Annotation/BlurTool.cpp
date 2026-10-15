#include "Annotation/BlurTool.h"

#include "Annotation/ImageBlur.h"
#include "Base/Text.h"

#include <QImage>
#include <QPainter>

#include <cmath>

namespace GyanisScribe::Annotation
{
    BlurTool::BlurTool() : AnnotationTool(ToolKind::Blur)
    {
    }

    QString BlurTool::displayName() const
    {
        return GyanisScribe::Base::uiText("高斯模糊");
    }

    void BlurTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        const auto &source = context.source;
        if (source.isNull() || context.pixelScale <= 0.0 || shape.rect.width() <= 0 || shape.rect.height() <= 0)
        {
            return;
        }

        // 逻辑框换成物理框：源图是物理像素，取错了地方就糊到了隔壁那块内容
        QRect physical(qRound(shape.rect.x() * context.pixelScale), qRound(shape.rect.y() * context.pixelScale), qMax(1, qRound(shape.rect.width() * context.pixelScale)),
                       qMax(1, qRound(shape.rect.height() * context.pixelScale)));
        physical = physical.intersected(source.rect());
        if (physical.isEmpty())
        {
            return;
        }

        // 半径也按同一倍比换算：否则高 DPI 屏上糊得比用户框的浅一倍
        const auto radius  = qMax(1, static_cast<int>(std::lround(kBlurRadiusLogical * context.pixelScale)));
        const auto blurred = boxBlurred(source.copy(physical), radius);

        if (!shape.elliptical)
        {
            painter.drawImage(shape.rect, blurred);
            return;
        }

        // 椭圆档：把糊过的图裁进内切椭圆再贴，椭圆之外一个像素都不碰。与马赛克同一套掩膜口径——
        // 走掩膜而不是 setClipPath，裁剪边不带抗锯齿，椭圆弧上会看见一圈台阶
        QImage masked = blurred.convertToFormat(QImage::Format_ARGB32_Premultiplied);
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
} // namespace GyanisScribe::Annotation
