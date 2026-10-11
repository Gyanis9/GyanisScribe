#include "Annotation/BlurTool.h"

#include "Annotation/ImageBlur.h"
#include "Core/Text.h"

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
        return GyanisScribe::Core::uiText("高斯模糊");
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
        const auto radius = qMax(1, static_cast<int>(std::lround(kBlurRadiusLogical * context.pixelScale)));
        painter.drawImage(shape.rect, boxBlurred(source.copy(physical), radius));
    }
} // namespace GyanisScribe::Annotation
