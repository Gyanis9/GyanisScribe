#include "Core/Image/ImageFit.h"

#include <algorithm>
#include <cmath>

namespace GyanisScribe::Core
{
    QRect displayRectFor(const QSize &widgetSize, const QSize &imageSize)
    {
        if (widgetSize.width() <= 0 || widgetSize.height() <= 0 || imageSize.width() <= 0 || imageSize.height() <= 0)
        {
            return {};
        }

        // 取两个方向里更小的那个比例：保比例就是「谁先到边谁说话」，另一头留白
        const auto  scale = std::min(static_cast<qreal>(widgetSize.width()) / imageSize.width(), static_cast<qreal>(widgetSize.height()) / imageSize.height());
        const QSize shown{std::max(1, static_cast<int>(std::lround(imageSize.width() * scale))), std::max(1, static_cast<int>(std::lround(imageSize.height() * scale)))};
        return {(widgetSize.width() - shown.width()) / 2, (widgetSize.height() - shown.height()) / 2, shown.width(), shown.height()};
    }

    QRect imageRectToDisplay(const QRect &imageRect, const QSize &imageSize, const QRect &displayRect)
    {
        if (imageSize.width() <= 0 || imageSize.height() <= 0 || displayRect.isEmpty())
        {
            return {};
        }

        // 两个方向各算一次比例：调用方给的 displayRect 未必由 displayRectFor 产出，
        // 只信宽度的话，非等比的那一份会整片画歪
        const auto scaleX = static_cast<qreal>(displayRect.width()) / imageSize.width();
        const auto scaleY = static_cast<qreal>(displayRect.height()) / imageSize.height();

        // 位置与宽高各按同一比例舍入：整图的框因此恰好铺满 displayRect，
        // 若改用「右下角相减」，QRect::right() 的 -1 会在缩放后多出一像素
        const auto left  = displayRect.left() + static_cast<int>(std::lround(imageRect.left() * scaleX));
        const auto top   = displayRect.top() + static_cast<int>(std::lround(imageRect.top() * scaleY));
        const auto width = std::max(1, static_cast<int>(std::lround(imageRect.width() * scaleX)));
        return {left, top, width, std::max(1, static_cast<int>(std::lround(imageRect.height() * scaleY)))};
    }

} // namespace GyanisScribe::Core
