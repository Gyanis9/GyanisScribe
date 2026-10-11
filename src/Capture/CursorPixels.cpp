#include "Capture/CursorPixels.h"

#include <algorithm>
#include <cstddef>

namespace GyanisScribe::Capture
{

    int cursorMonoRowBytes(const int width)
    {
        // 向上取整到 32 位：GDI 的 1bpp 行尾补零，行距不是宽度除八
        return (width + 31) / 32 * 4;
    }

    namespace
    {
        /// 取掩码第 row 行第 column 个像素的位（1 表示这一格透出底下的画面）
        [[nodiscard]] bool maskBit(const std::vector<unsigned char> &bits, int rowBytes, int row, int column)
        {
            const auto *const begin = bits.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(rowBytes);
            return ((begin[column / 8] >> (7 - column % 8)) & 1U) != 0U;
        }
    } // namespace

    QImage composeCursorImage(const std::vector<QRgb> &colorPlane, const std::vector<unsigned char> &maskPlane, const int width, const int height, const bool isMono)
    {
        if (width <= 0 || height <= 0)
        {
            return {};
        }

        const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        // 带 alpha 的现代指针把透明写在颜色面里：整片扫一遍比逐格猜可靠，
        // 因为同一枚指针不会一半有一半没有
        const bool carriesAlpha = std::any_of(colorPlane.begin(), colorPlane.end(), [](const QRgb pixel) { return qAlpha(pixel) != 0; });

        const auto        rowBytes = cursorMonoRowBytes(width);
        std::vector<QRgb> pixels(pixelCount, 0U);
        for (int row = 0; row < height; ++row)
        {
            for (int column = 0; column < width; ++column)
            {
                const auto index       = static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column);
                const bool transparent = maskBit(maskPlane, rowBytes, row, column);
                if (isMono)
                {
                    // 单色指针三态：AND 置 1 透出画面，否则 XOR 置 1 是白、置 0 是黑
                    const bool xorPainted = maskBit(maskPlane, rowBytes, row + height, column);
                    pixels[index]         = transparent ? QRgb{0U} : (xorPainted ? 0xFFFFFFFFU : 0xFF000000U);
                    continue;
                }
                if (!carriesAlpha && transparent)
                {
                    continue;
                }
                pixels[index] = colorPlane[index];
                if (!carriesAlpha)
                {
                    // 老式彩色指针的颜色面 alpha 那一格是 0，直接画等于画一枚全透明的图
                    pixels[index] = qRgb(qRed(pixels[index]), qGreen(pixels[index]), qBlue(pixels[index]));
                }
            }
        }

        return QImage(reinterpret_cast<const uchar *>(pixels.data()), width, height, width * 4, QImage::Format_ARGB32).copy();
    }

} // namespace GyanisScribe::Capture
