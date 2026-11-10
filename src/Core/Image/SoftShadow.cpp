#include "Core/Image/SoftShadow.h"

#include <algorithm>
#include <cmath>

namespace
{
    /// 像素索引到圆角矩形边界的有符号距离：内部为负，外部为正
    [[nodiscard]] double distanceToRoundedRect(const int x, const int y, const QRect &rect, const int radius)
    {
        // 像素索引按采样点算，矩形两端落在首末像素上：半宽取两端之差的一半。
        // 取 width() 的一半会偏半格，右、下两边各少算一格，紧贴卡片那一圈最深投影只剩左、上两条
        const auto halfWidth  = static_cast<double>(rect.right() - rect.left()) / 2.0;
        const auto halfHeight = static_cast<double>(rect.bottom() - rect.top()) / 2.0;
        const auto centerX    = (static_cast<double>(rect.left()) + static_cast<double>(rect.right())) / 2.0;
        const auto centerY    = (static_cast<double>(rect.top()) + static_cast<double>(rect.bottom())) / 2.0;
        const auto dx         = std::abs(static_cast<double>(x) - centerX) - halfWidth + static_cast<double>(radius);
        const auto dy         = std::abs(static_cast<double>(y) - centerY) - halfHeight + static_cast<double>(radius);
        const auto outside    = std::hypot(std::max(dx, 0.0), std::max(dy, 0.0));
        const auto inside     = std::min(std::max(dx, dy), 0.0);
        return outside + inside - static_cast<double>(radius);
    }
} // namespace

namespace GyanisScribe::Core
{
    QImage makeSoftShadowLayer(const QSize &canvasSize, const QRect &cardRect, const int cornerRadius, const int shadowPixels, int maximumAlpha)
    {
        if (canvasSize.isEmpty() || cardRect.isEmpty() || shadowPixels <= 0)
        {
            return {};
        }
        const auto canvas = QRect{QPoint{0, 0}, canvasSize};
        const auto card   = cardRect.intersected(canvas);
        if (card.isEmpty())
        {
            return {};
        }
        const auto radius = std::clamp(cornerRadius, 0, std::min(card.width(), card.height()) / 2);
        maximumAlpha      = std::clamp(maximumAlpha, 0, 255);
        if (maximumAlpha == 0)
        {
            return {};
        }

        QImage layer{canvasSize, QImage::Format_ARGB32_Premultiplied};
        if (layer.isNull())
        {
            return {};
        }
        layer.fill(Qt::transparent);

        // 投影只存在于卡片外一圈：上、下两条按带子的整幅宽扫，左右两条只走卡片那几行，
        // 逐像素算距离的成本因此只随周长走，不随卡片面积走。外扩量按画布边长之和截住：
        // 画布上任何一点离卡片都不超过这个数，原值直接拿来调 adjusted 会在超大宽度上溢出
        const auto reach     = std::min(shadowPixels, canvasSize.width() + canvasSize.height());
        const auto band      = card.adjusted(-reach, -reach, reach, reach).intersected(canvas);
        const auto span      = static_cast<double>(shadowPixels);
        const auto paintArea = [&](const int left, const int top, const int right, const int bottom)
        {
            const auto area = QRect{QPoint{left, top}, QPoint{right, bottom}}.intersected(band);
            for (auto y = area.top(); y <= area.bottom(); ++y)
            {
                auto *const line = reinterpret_cast<QRgb *>(layer.scanLine(y));
                for (auto x = area.left(); x <= area.right(); ++x)
                {
                    const auto distance = distanceToRoundedRect(x, y, card, radius);
                    if (distance <= 0.0 || distance >= span)
                    {
                        continue;
                    }
                    // 距离平方衰减：紧贴那一圈最深，到边界正好为零，不会留下一条看得见的硬边
                    const auto falloff = 1.0 - distance / span;
                    const auto alpha   = static_cast<int>(std::lround(static_cast<double>(maximumAlpha) * falloff * falloff));
                    if (alpha > 0)
                    {
                        line[x] = qRgba(0, 0, 0, alpha);
                    }
                }
            }
        };

        // 上、下两条整幅宽，卡片左右的四个角也归这里；中间那几行只补竖条
        paintArea(band.left(), band.top(), band.right(), card.top() - 1);
        paintArea(band.left(), card.bottom() + 1, band.right(), band.bottom());
        paintArea(band.left(), card.top(), card.left() - 1, card.bottom());
        paintArea(card.right() + 1, card.top(), band.right(), card.bottom());
        if (radius > 0)
        {
            // 圆角切掉的四块落在卡片外接矩形里，只有半径见方的四个角块会算出正距离
            const auto corner = radius - 1;
            paintArea(card.left(), card.top(), card.left() + corner, card.top() + corner);
            paintArea(card.right() - corner, card.top(), card.right(), card.top() + corner);
            paintArea(card.left(), card.bottom() - corner, card.left() + corner, card.bottom());
            paintArea(card.right() - corner, card.bottom() - corner, card.right(), card.bottom());
        }
        return layer;
    }
} // namespace GyanisScribe::Core
