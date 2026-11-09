#include "Core/Privacy/RedactionGeometry.h"

#include <QPoint>

#include <cmath>

namespace GyanisScribe::Core::Privacy
{
    QRect expandBarcodeScanline(const QRect &scanline, const QSize imageSize, const QSize logicalSize)
    {
        if (imageSize.isEmpty())
        {
            return {};
        }

        // 一维码的定位框是一条接近零高度的扫描线：额外纵向像素上下各扩一半，
        // 再按两维比例把逻辑像素折成物理高度
        const qreal verticalScale = logicalSize.height() > 0 ? static_cast<qreal>(imageSize.height()) / static_cast<qreal>(logicalSize.height()) : 1.0;
        const int   expansion     = static_cast<int>(std::lround(static_cast<qreal>(kBarcodeVerticalExpansionPixels) / 2.0 * verticalScale));
        QRect       box           = scanline.normalized();
        box.adjust(0, -expansion, 0, expansion);
        return box.intersected(QRect{QPoint{0, 0}, imageSize});
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
