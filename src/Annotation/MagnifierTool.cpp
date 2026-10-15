#include "Annotation/MagnifierTool.h"

#include "Annotation/MagnifierLayout.h"
#include "Annotation/StrokePen.h"
#include "Base/Text.h"

#include <QImage>
#include <QPainter>

namespace GyanisScribe::Annotation
{
    namespace
    {
        /// 源框虚线的线宽，逻辑像素：细一圈，够看出「放大的是这里」又不盖住内容
        constexpr double kSourceOutlineWidth = 1.5;
    } // namespace

    MagnifierTool::MagnifierTool() : AnnotationTool(ToolKind::Magnifier)
    {
    }

    QString MagnifierTool::displayName() const
    {
        return GyanisScribe::Base::uiText("放大镜");
    }

    void MagnifierTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        const auto &source = context.source;
        if (source.isNull() || context.pixelScale <= 0.0 || shape.rect.width() <= 1 || shape.rect.height() <= 1)
        {
            return;
        }

        // 逻辑尺寸 = 物理尺寸 / 倍比：源图是按物理像素存的，画布逻辑坐标要用它换
        const QSize canvasLogicalSize{qMax(1, static_cast<int>(std::lround(source.width() / context.pixelScale))),
                                      qMax(1, static_cast<int>(std::lround(source.height() / context.pixelScale)))};
        const QRect insetRect = magnifierInsetRectFor(shape.rect, canvasLogicalSize);

        // 逻辑框换成物理框：源图是物理像素，取错了地方放大出来就是隔壁那块内容
        QRect physical(qRound(shape.rect.x() * context.pixelScale), qRound(shape.rect.y() * context.pixelScale), qMax(1, qRound(shape.rect.width() * context.pixelScale)),
                       qMax(1, qRound(shape.rect.height() * context.pixelScale)));
        physical = physical.intersected(source.rect());
        if (!physical.isEmpty())
        {
            // 放大用平滑插值：镜子的用处是让人看清，最近邻会把斜边与文字掰成阶梯
            const QImage enlarged =
                    source.copy(physical).scaled(QSize{qMax(1, qRound(insetRect.width() * context.pixelScale)), qMax(1, qRound(insetRect.height() * context.pixelScale))},
                                                 Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            painter.drawImage(insetRect, enlarged);
        }

        // 插图边框：用图形自己的那支笔（颜色、线宽、线型都跟着用户调的走）
        auto insetPen = strokePenFor(shape);
        painter.setPen(insetPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(insetRect);

        // 源框虚线：标出「放大的是这里」，细一圈、虚一股，不与内容抢视线
        painter.setPen(QPen(shape.color, kSourceOutlineWidth, Qt::DashLine));
        painter.drawRect(shape.rect);
    }

} // namespace GyanisScribe::Annotation
