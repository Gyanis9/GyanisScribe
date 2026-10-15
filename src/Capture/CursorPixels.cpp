#include "Capture/CursorPixels.h"

#include "Base/Logger.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <format>
#include <vector>

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

#ifdef Q_OS_WIN
    namespace
    {
        /// 指针边的可信上限：Windows 的指针最大约 128 物理像素，读到更大的值说明尺寸面读歪了
        constexpr int kMaximumCursorEdge = 256;

        /**
         * @brief GetIconInfo 交出来的两枚位图的收口处
         * @details 文档写明这两枚归调用方销毁；写成析构而不是在每条出口手动 DeleteObject，
         *          是因为这一层有好几条提前返回，漏一条就是每次截图漏一枚 GDI 句柄。
         */
        struct IconInfoOwner
        {
            ICONINFO value{};

            IconInfoOwner()                                 = default;
            IconInfoOwner(const IconInfoOwner &)            = delete;
            IconInfoOwner &operator=(const IconInfoOwner &) = delete;

            ~IconInfoOwner()
            {
                if (value.hbmColor != nullptr)
                {
                    DeleteObject(value.hbmColor);
                }
                if (value.hbmMask != nullptr)
                {
                    DeleteObject(value.hbmMask);
                }
            }
        };
    } // namespace

    std::optional<CursorSprite> cursorSpriteFromHandle(const quintptr cursorHandle)
    {
        if (cursorHandle == 0)
        {
            return std::nullopt;
        }

        IconInfoOwner owner;
        if (!GetIconInfo(reinterpret_cast<HCURSOR>(cursorHandle), &owner.value))
        {
            return std::nullopt;
        }

        // 单色指针没有 hbmColor，掩码那张是「AND 面 + XOR 面」上下叠起来的，高度要折半
        const bool isMono     = owner.value.hbmColor == nullptr;
        HBITMAP    sizeSource = isMono ? owner.value.hbmMask : owner.value.hbmColor;
        BITMAP     sizeBitmap{};
        if (!GetObjectW(sizeSource, static_cast<int>(sizeof(sizeBitmap)), &sizeBitmap) || sizeBitmap.bmWidth <= 0 || sizeBitmap.bmHeight <= 0)
        {
            return std::nullopt;
        }
        const int width  = sizeBitmap.bmWidth;
        const int height = isMono ? sizeBitmap.bmHeight / 2 : sizeBitmap.bmHeight;
        if (width > kMaximumCursorEdge || height > kMaximumCursorEdge)
        {
            Base::Logger::warning(std::format("鼠标指针的尺寸读出来是 {}×{}，超出可信范围，这次截图不含指针", width, height));
            return std::nullopt;
        }

        HDC screenDc = GetDC(nullptr);
        if (screenDc == nullptr)
        {
            return std::nullopt;
        }

        /**
         * @brief 带调色板余量的位图信息块
         * @details BITMAPINFO 只声明了 bmiColors[1] 一格，而 GDI 对 ≤8 位的位图按 2^位数 读写表项：
         *          1bpp 要两格。多出来的那一格落在栈上的局部变量之外——Debug 档的 /RTC1 会当场报
         *          「Stack around the variable was corrupted」并弹模态框（实测：整条用例挂在那儿不动），
         *          Release 档不报，但同样在踩邻居的栈。
         */
        struct BitmapInfoWithPalette
        {
            BITMAPINFOHEADER header;
            RGBQUAD          colors[2];
        };

        /// 指针的颜色面：一帧 width×height 的 32 位像素
        std::vector<QRgb> colorBits(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

        /// 指针的位平面按 32 位取：GDI 会把 1/4/8/24 位的指针都换算过来，省去自己解调色板
        BitmapInfoWithPalette colorInfo{};
        colorInfo.header.biSize        = static_cast<DWORD>(sizeof(BITMAPINFOHEADER));
        colorInfo.header.biWidth       = width;
        colorInfo.header.biHeight      = -height; // 负数即自上而下，与 QImage 的行序一致
        colorInfo.header.biPlanes      = 1;
        colorInfo.header.biBitCount    = 32;
        colorInfo.header.biCompression = BI_RGB;

        const auto            maskRowBytes = static_cast<std::size_t>(cursorMonoRowBytes(width));
        const int             maskPlanes   = isMono ? 2 : 1;
        const int             maskRows     = height * maskPlanes;
        BitmapInfoWithPalette maskInfo{};
        maskInfo.header.biSize        = static_cast<DWORD>(sizeof(BITMAPINFOHEADER));
        maskInfo.header.biWidth       = width;
        maskInfo.header.biHeight      = maskRows; // 正数即 GDI 的自下而上：一次读全，行起点才落在平面边界上
        maskInfo.header.biPlanes      = 1;
        maskInfo.header.biBitCount    = 1;
        maskInfo.header.biCompression = BI_RGB;

        // 单色指针的掩码是一张 2×height 高的位图。GetIconInfo 交回来的那张里，自下而上数是
        // 「先 XOR 面、后 AND 面」（实测：把前一半当 AND 读，一枚自造指针的外圈会画成实心白块），
        // 而 composeCursorImage 约定的是「AND 在前」。这里按平面各自翻一次行序，顺手把这个次序摆正。
        std::vector<unsigned char> maskRaw(maskRowBytes * static_cast<std::size_t>(maskRows), 0U);
        const bool hasMaskPlane = GetDIBits(screenDc, owner.value.hbmMask, 0, maskRows, maskRaw.data(), reinterpret_cast<BITMAPINFO *>(&maskInfo), DIB_RGB_COLORS) == maskRows;
        const bool hasColorPlane =
                !isMono && GetDIBits(screenDc, owner.value.hbmColor, 0, height, colorBits.data(), reinterpret_cast<BITMAPINFO *>(&colorInfo), DIB_RGB_COLORS) == height;
        ReleaseDC(nullptr, screenDc);

        std::vector<unsigned char> maskBits(maskRaw.size(), 0U);
        for (int targetPlane = 0; targetPlane < maskPlanes && hasMaskPlane; ++targetPlane)
        {
            // 单色指针的两个平面在源里位置相反；只有一个平面时它就是 AND
            const int         sourcePlane = isMono ? maskPlanes - 1 - targetPlane : 0;
            const auto *const planeStart  = maskRaw.data() + static_cast<std::size_t>(sourcePlane) * static_cast<std::size_t>(height) * maskRowBytes;
            auto *const       target      = maskBits.data() + static_cast<std::size_t>(targetPlane) * static_cast<std::size_t>(height) * maskRowBytes;
            for (int row = 0; row < height; ++row)
            {
                std::memcpy(target + static_cast<std::size_t>(row) * maskRowBytes, planeStart + static_cast<std::size_t>(height - 1 - row) * maskRowBytes, maskRowBytes);
            }
        }

        if (!hasColorPlane && !isMono)
        {
            // 颜色面读不出来时不能只靠掩码画个空壳：整枚指针放弃，让上层照常出图
            Base::Logger::warning(std::format("鼠标指针的颜色面读不出来（{}×{}），这次截图不含指针", width, height));
            return std::nullopt;
        }
        if (!hasMaskPlane)
        {
            Base::Logger::warning(std::format("鼠标指针的掩码面读不出来（{}×{}），这次截图不含指针", width, height));
            return std::nullopt;
        }

        CursorSprite sprite;
        sprite.image   = composeCursorImage(colorBits, maskBits, width, height, isMono);
        sprite.hotspot = QPoint{static_cast<int>(owner.value.xHotspot), static_cast<int>(owner.value.yHotspot)};
        if (sprite.image.isNull())
        {
            return std::nullopt;
        }
        return sprite;
    }
#else
    std::optional<CursorSprite> cursorSpriteFromHandle(const quintptr cursorHandle)
    {
        // 别的平台没有「向系统要一枚指针的两张位平面」这一格：交回空值，
        // 由编排层在日志里说明这次不含指针
        Q_UNUSED(cursorHandle);
        return std::nullopt;
    }
#endif

} // namespace GyanisScribe::Capture
