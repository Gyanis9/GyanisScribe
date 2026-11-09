#include "Core/Privacy/RedactionGeometry.h"

#include <QPoint>

#include <algorithm>
#include <cmath>

namespace GyanisScribe::Core::Privacy
{
    QRect expandBarcodeScanline(const QRect &scanline, const QSize imageSize, const QSize logicalSize)
    {
        if (imageSize.isEmpty() || logicalSize.isEmpty() || scanline.isEmpty())
        {
            return {};
        }

        // 一维码的定位框往往只覆盖解码行附近：先按纵向倍比把逻辑像素折成物理像素，
        // 目标高度取「原框高度 + 额外高度」与最小高度中的较大者，再以定位框中心为轴上下均分
        const qreal verticalScale        = logicalSize.height() > 0 ? static_cast<qreal>(imageSize.height()) / static_cast<qreal>(logicalSize.height()) : 1.0;
        const QRect line                 = scanline.normalized();
        const qreal currentLogicalHeight = static_cast<qreal>(line.height()) / verticalScale;
        const qreal targetLogicalHeight  = std::max(static_cast<qreal>(kBarcodeMinLogicalHeight), currentLogicalHeight + static_cast<qreal>(kBarcodeVerticalExpansionPixels));
        const int   targetHeight         = std::max(1, static_cast<int>(std::lround(targetLogicalHeight * verticalScale)));
        const int   centerY              = line.top() + line.height() / 2;
        const QRect expanded{line.left(), centerY - targetHeight / 2, line.width(), targetHeight};
        return expanded.intersected(QRect{QPoint{0, 0}, imageSize});
    }

    std::optional<QRect> logicalRedactionRect(const QRect &physicalBox, const QSize imageSize, const QSize logicalSize)
    {
        if (imageSize.isEmpty() || logicalSize.isEmpty() || physicalBox.isEmpty())
        {
            return std::nullopt;
        }

        // 物理框换到画布逻辑坐标：按两个方向的倍比各算一次，四边再留 kRedactionPaddingPixels 逻辑像素
        const qreal  scaleX = static_cast<qreal>(logicalSize.width()) / static_cast<qreal>(imageSize.width());
        const qreal  scaleY = static_cast<qreal>(logicalSize.height()) / static_cast<qreal>(imageSize.height());
        const QRectF physical{physicalBox.normalized()};
        QRectF       logical{physical.left() * scaleX, physical.top() * scaleY, physical.width() * scaleX, physical.height() * scaleY};
        logical.adjust(-kRedactionPaddingPixels, -kRedactionPaddingPixels, kRedactionPaddingPixels, kRedactionPaddingPixels);
        const QRect clamped = logical.toAlignedRect().intersected(QRect{QPoint{0, 0}, logicalSize});
        if (clamped.isEmpty())
        {
            return std::nullopt;
        }
        return clamped;
    }

} // namespace GyanisScribe::Core::Privacy
