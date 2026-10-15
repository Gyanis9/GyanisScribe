#include "Core/Annotation/EraserTool.h"

#include "Core/Annotation/AnnotationGeometry.h"
#include "Base/Text.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>

#include <cmath>

namespace GyanisScribe::Core
{
    namespace
    {
        // 橡皮相对线宽的加宽倍数：与高亮同口径，滑块调线宽就是调橡皮的大小
        constexpr int kEraserWidthFactor = 6;
    } // namespace

    EraserTool::EraserTool() : PenTool(ToolKind::Eraser)
    {
    }

    QString EraserTool::displayName() const
    {
        return GyanisScribe::Base::uiText("橡皮擦");
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

        // 物理框按同一倍比换回逻辑坐标贴回画布：屏显与导出各有一套画家变换，两处都落到同一块像素上
        const QRectF target(QPointF(physical.left() / context.pixelScale, physical.top() / context.pixelScale),
                            QSizeF(physical.width() / context.pixelScale, physical.height() / context.pixelScale));
        painter.drawImage(target, patch);
    }

    bool EraserTool::hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const
    {
        const double widened = static_cast<double>(tolerance) * kEraserWidthFactor;
        return squaredDistanceToPolyline(point, shape.points) <= widened * widened;
    }

} // namespace GyanisScribe::Core
