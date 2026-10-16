#include "Core/Pick/PickGeometry.h"

#include "Base/Text.h"

#include <QString>

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace
{
    using GyanisScribe::Core::MagnifierView;

    // 四档一列到底：标记、界面名与取样边长按下标互指，加一档只改这三份表
    constexpr std::array<std::string_view, 4> kMagnifierTokens = {"off", "narrow", "medium", "wide"};
    constexpr std::array<std::string_view, 4> kMagnifierLabels = {"不显示", "窄（9×9）", "中（15×15）", "宽（21×21）"};
    constexpr std::array<int, 4>              kMagnifierEdges  = {0, 9, 15, 21};
} // namespace

namespace GyanisScribe::Core
{
    int magnifierSampleEdgeFor(const MagnifierView view)
    {
        const auto index = static_cast<std::size_t>(view);
        return index < kMagnifierEdges.size() ? kMagnifierEdges[index] : kMagnifierEdges[1];
    }

    std::string_view magnifierViewToken(const MagnifierView view)
    {
        const auto index = static_cast<std::size_t>(view);
        return index < kMagnifierTokens.size() ? kMagnifierTokens[index] : kMagnifierTokens[1];
    }

    QString magnifierViewLabel(const MagnifierView view)
    {
        const auto index = static_cast<std::size_t>(view);
        return GyanisScribe::Base::uiText(index < kMagnifierLabels.size() ? kMagnifierLabels[index].data() : kMagnifierLabels[1].data());
    }

    std::vector<MagnifierView> magnifierViews()
    {
        std::vector<MagnifierView> views;
        views.reserve(kMagnifierTokens.size());
        for (std::size_t index = 0; index < kMagnifierTokens.size(); ++index)
        {
            views.push_back(static_cast<MagnifierView>(index));
        }
        return views;
    }

    MagnifierView magnifierViewOrDefault(const std::string_view token, std::string &complaint)
    {
        complaint.clear();
        for (std::size_t index = 0; index < kMagnifierTokens.size(); ++index)
        {
            if (kMagnifierTokens[index] == token)
            {
                return static_cast<MagnifierView>(index);
            }
        }

        // 坏值回落「窄」而不是夹到最近一档：夹会把手写的 wide2 变成 wide，而这两件事差得远
        complaint = std::format("放大镜视野「{}」不是可用取值。可用的是 off、narrow、medium、wide；这一轮按 narrow（9×9，本仓此前的形状）走", std::string(token));
        return MagnifierView::Narrow;
    }

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
            rects.emplace_back(left + index * stride, top, kRecentSwatchEdge, kRecentSwatchEdge);
        }
        return rects;
    }

} // namespace GyanisScribe::Core
