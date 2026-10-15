#include "Core/History/HistoryThumbnail.h"

#include <QPainter>
#include <QPainterPath>

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

        QImage canvas{target, QImage::Format_ARGB32};
        canvas.fill(Qt::transparent);
        {
            QPainter     painter{&canvas};
            QPainterPath rounded;
            rounded.addRoundedRect(QRectF{QPointF{0.0, 0.0}, QSizeF{target}}, cornerRadius, cornerRadius);
            painter.setClipPath(rounded);
            painter.drawImage(-crop, scaled);
        }
        return canvas;
    }

} // namespace GyanisScribe::Core
