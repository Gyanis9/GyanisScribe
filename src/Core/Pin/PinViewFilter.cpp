#include "Core/Pin/PinViewFilter.h"

namespace GyanisScribe::Core
{
    QImage pinViewFiltered(const QImage &source, const bool grayscale, const bool inverted)
    {
        if ((!grayscale && !inverted) || source.isNull())
        {
            return source;
        }

        // 先转成非预乘的 ARGB32：预乘格式上直接读通道值拿到的是乘过 alpha 的数，
        // 灰度的 luma 会在半透明像素上算小一截
        QImage result = source.convertToFormat(QImage::Format_ARGB32);

        if (grayscale)
        {
            for (int y = 0; y < result.height(); ++y)
            {
                auto *line = reinterpret_cast<QRgb *>(result.scanLine(y));
                for (int x = 0; x < result.width(); ++x)
                {
                    const QRgb pixel = line[x];
                    const int  luma  = qGray(pixel); // (r×11 + g×16 + b×5)/32，alpha 不参与
                    line[x]          = qRgba(luma, luma, luma, qAlpha(pixel));
                }
            }
        }

        if (inverted)
        {
            // InvertRgb 只翻 RGB：透明区不会因为反色变成实心
            result.invertPixels(QImage::InvertRgb);
        }

        return result;
    }

} // namespace GyanisScribe::Core
