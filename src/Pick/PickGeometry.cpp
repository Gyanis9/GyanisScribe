#include "Pick/PickGeometry.h"

#include <algorithm>
#include <cmath>

namespace GyanisScribe::Pick
{
    MagnifierLayout magnifierLayoutFor(const QPoint &cursor, const QRect &screen, const QRect &available, const int sampleEdge, const int viewEdge)
    {
        if (sampleEdge <= 0 || viewEdge <= 0 || screen.isEmpty())
        {
            return {};
        }

        // 可用区没给的时候退回整屏：翻边判据不能拿一个空矩形去比
        const QRect bounds = available.isEmpty() ? screen : available;

        // 以光标为中心取样，越界整体夹进屏幕。回绕会把屏幕另一头的像素摆到中间，
        // 用户照抄的颜色就错了，而放大镜本身看起来毫无异常
        const auto half = sampleEdge / 2;
        const auto left = std::clamp(cursor.x() - half, screen.left(), screen.right() - sampleEdge + 1);
        const auto top  = std::clamp(cursor.y() - half, screen.top(), screen.bottom() - sampleEdge + 1);

        // 一格多大由显示边长反推：除不尽时按整数格算，宁可少几像素也不要出现半格
        const auto cellSize = std::max(1, viewEdge / sampleEdge);
        const auto shown    = cellSize * sampleEdge;

        auto viewLeft = cursor.x() + kMagnifierCursorOffset;
        auto viewTop  = cursor.y() + kMagnifierCursorOffset;
        if (viewLeft + shown > bounds.right())
        {
            viewLeft = cursor.x() - kMagnifierCursorOffset - shown;
        }
        if (viewTop + shown > bounds.bottom())
        {
            viewTop = cursor.y() - kMagnifierCursorOffset - shown;
        }

        return MagnifierLayout{QRect{left, top, sampleEdge, sampleEdge}, QRect{viewLeft, viewTop, shown, shown}, cellSize};
    }

    QPoint framePixelFor(const QPoint &globalPos, const QRect &screenGeometry, const qreal devicePixelRatio, const QSize &imageSize)
    {
        if (imageSize.isEmpty() || devicePixelRatio <= 0.0)
        {
            return {};
        }

        // 先减掉屏幕原点（多屏时副屏不是从 0 开始），再乘缩放比换到像素
        const auto local = globalPos - screenGeometry.topLeft();
        const auto x     = std::clamp(static_cast<int>(std::lround(local.x() * devicePixelRatio)), 0, imageSize.width() - 1);
        const auto y     = std::clamp(static_cast<int>(std::lround(local.y() * devicePixelRatio)), 0, imageSize.height() - 1);
        return {x, y};
    }


    std::vector<QRect> recentSwatchRects(const QRect &screen, const int count)
    {
        std::vector<QRect> rects;
        if (count <= 0 || screen.isEmpty())
        {
            return rects;
        }

        // 一行摆得下几个：留白按色块边长同宽，两侧各留一份，块间再夹间距
        const auto stride   = kRecentSwatchEdge + kRecentSwatchGap;
        const auto usable   = screen.width() - 2 * kRecentSwatchEdge;
        const auto capacity = usable > 0 ? (usable + kRecentSwatchGap) / stride : 0;
        const auto shown    = std::min(count, capacity);
        if (shown <= 0)
        {
            return rects;
        }

        const auto rowWidth = shown * kRecentSwatchEdge + (shown - 1) * kRecentSwatchGap;
        const auto left     = screen.left() + (screen.width() - rowWidth) / 2;
        const auto top      = screen.bottom() - kRecentSwatchEdge - kRecentSwatchEdge;
        rects.reserve(static_cast<std::size_t>(shown));
        for (int index = 0; index < shown; ++index)
        {
            rects.push_back(QRect{left + index * stride, top, kRecentSwatchEdge, kRecentSwatchEdge});
        }
        return rects;
    }

} // namespace GyanisScribe::Pick
