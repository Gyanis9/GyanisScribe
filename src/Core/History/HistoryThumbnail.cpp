#include "Core/History/HistoryThumbnail.h"

#include "Core/Geometry/ContinuousCorner.h"

#include <QPainter>

#include <algorithm>

namespace GyanisScribe::Core
{
    QImage makeThumbnail(const QImage &source, const QSize &target, const int cornerRadius)
    {
        if (source.isNull() || target.width() <= 0 || target.height() <= 0)
        {
            return {};
        }

        // 先按「填满」缩放，再居中裁掉多出来的一边：留白的缩略图在网格里大小不一
        const auto scaled = source.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const auto crop   = QPoint{std::max(0, (scaled.width() - target.width()) / 2), std::max(0, (scaled.height() - target.height()) / 2)};

        // 画布用预乘格式：与掩膜同一套像素口径，DestinationIn 合成不必在绘制时临时转换
        QImage canvas{target, QImage::Format_ARGB32_Premultiplied};
        canvas.fill(Qt::transparent);
        {
            QPainter painter{&canvas};
            painter.drawImage(-crop, scaled);
        }

        // 平滑度取缩略图这一档、半径取调用方换好的物理像素：1x/2x/3x 三张图在列表里是同一个逻辑形状
        const auto shape = cornerShapeFor(CornerRole::Thumbnail);
        applyCornerMask(canvas, CornerShape{static_cast<qreal>(cornerRadius), shape.smoothing});
        return canvas;
    }

} // namespace GyanisScribe::Core
