// 指针位平面的合成规则：三种指针（带 alpha、老式彩色、单色）各自的透明与颜色判据。
// 这一层刻意不碰 GDI——容器与本机都编得过，掩码位序解反这种错在两边都跑得出来。

#include "Core/Capture/CursorPixels.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QColor>
#include <QImage>
#include <QRgb>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <vector>

using GyanisScribe::Core::composeCursorImage;
using GyanisScribe::Core::cursorMonoRowBytes;

namespace
{
    // 3 像素宽、2 像素高的形状：行距按 32 位对齐就是 4 字节，正好把「补零」这一格也走一遍
    constexpr int kWidth    = 3;
    constexpr int kHeight   = 2;
    constexpr int kRowBytes = 4;

    /// 把一行像素写成 1bpp 掩码：参数里的每个值占一位，高位在左
    [[nodiscard]] std::vector<unsigned char> maskPlane(const std::vector<std::vector<int>> &rows)
    {
        std::vector<unsigned char> bits(rows.size() * kRowBytes, 0U);
        for (std::size_t row = 0; row < rows.size(); ++row)
        {
            for (std::size_t column = 0; column < rows[row].size(); ++column)
            {
                if (rows[row][column] != 0)
                {
                    bits[row * kRowBytes + column / 8] = static_cast<unsigned char>(bits[row * kRowBytes + column / 8] | (1U << (7 - column % 8)));
                }
            }
        }
        return bits;
    }
} // namespace

/**
 * @brief 钉住 1bpp 行距的四字节对齐
 * @details 掩码按行距寻址，这一格算错整张掩码就会逐行错位：指针会画成斜的、还带一条不该有的尾巴。
 */
TEST(CursorPixels, MonoRowStrideIsPaddedToThirtyTwoBits)
{
    EXPECT_EQ(cursorMonoRowBytes(1), 4);
    EXPECT_EQ(cursorMonoRowBytes(3), 4);
    EXPECT_EQ(cursorMonoRowBytes(32), 4);
    EXPECT_EQ(cursorMonoRowBytes(33), 8) << "超过 32 位要进第二格，行距不能还是 4";
    EXPECT_EQ(cursorMonoRowBytes(64), 8);
}

/**
 * @brief 钉住单色指针的三态：透明、黑、白
 * @details 单色指针只有 AND 与 XOR 两张平面：AND 置 1 才透出画面，其余按 XOR 分黑白。
 *          把两张平面当成一张读，箭头会变成一坨实心黑块。
 */
TEST(CursorPixels, MonoCursorMapsAndXorPlanesToThreeStates)
{
    // AND 面第一行：第 0 格透明，第 1、2 格不透明；第二行全透明
    // XOR 面第一行：第 1 格白、第 2 格黑
    auto mask = maskPlane({{1, 0, 0}, {1, 1, 1}, {0, 1, 0}, {0, 0, 0}});

    const QImage image = composeCursorImage({}, mask, kWidth, kHeight, true);
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.size(), QSize(kWidth, kHeight));
    EXPECT_EQ(image.pixelColor(0, 0), QColor(0, 0, 0, 0)) << "AND 置 1 的那一格该透出画面";
    EXPECT_EQ(image.pixelColor(1, 0), QColor(255, 255, 255)) << "XOR 置 1 该是白";
    EXPECT_EQ(image.pixelColor(2, 0), QColor(0, 0, 0, 255)) << "XOR 置 0 该是黑";
    EXPECT_EQ(image.pixelColor(0, 1), QColor(0, 0, 0, 0));
}

/**
 * @brief 钉住老式彩色指针按掩码判透明、并把 alpha 补成不透明
 * @details 那一路的颜色面 alpha 整片是 0：照原样画就是一枚看不见的指针，
 *          按掩码补成 255 才画得出来；掩码置 1 的格子仍然要留空。
 */
TEST(CursorPixels, LegacyColorCursorTakesOpacityFromMask)
{
    const std::vector<QRgb> color{0x00FF0000U, 0x0000FF00U, 0x000000FFU, 0x00112233U, 0x00445566U, 0x00778899U};
    const auto              mask = maskPlane({{1, 0, 0}, {0, 0, 0}});

    const QImage image = composeCursorImage(color, mask, kWidth, kHeight, false);
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.pixelColor(0, 0), QColor(0, 0, 0, 0)) << "掩码说透明的格子不该画出来";
    EXPECT_EQ(image.pixelColor(1, 0), QColor(0, 255, 0, 255)) << "alpha 没补成不透明，指针会是隐形的";
    EXPECT_EQ(image.pixelColor(2, 0), QColor(0, 0, 255, 255));
    EXPECT_EQ(image.pixelColor(0, 1), QColor(0x11, 0x22, 0x33, 255)) << "行序错了：第二行第一格读成了别的像素";
}

/**
 * @brief 钉住带 alpha 的现代指针完全按颜色面走
 * @details 现代 .cur 的抗锯齿边把半透明写在 alpha 里，而 AND 掩码在那些格子上也是 1：
 *          再按掩码硬切就会把软边切成锯齿，还会在浅色背景上留一圈黑边。
 */
TEST(CursorPixels, AlphaCursorKeepsItsSoftEdgesDespiteMask)
{
    const std::vector<QRgb> color{0x8000FF00U, 0xFFFF0000U, 0x00000000U, 0x01010101U, 0x02020202U, 0x03030303U};
    // 掩码整片置 1：按掩码判透明就会把这一枚指针全擦掉
    const auto mask = maskPlane({{1, 1, 1}, {1, 1, 1}});

    const QImage image = composeCursorImage(color, mask, kWidth, kHeight, false);
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.pixelColor(0, 0).alpha(), 128) << "带 alpha 的指针被掩码硬切了";
    EXPECT_EQ(image.pixelColor(1, 0), QColor(255, 0, 0, 255));
    EXPECT_EQ(image.pixelColor(2, 0).alpha(), 0) << "颜色面自己说透明的格子必须留空";
}

/**
 * @brief 钉住尺寸非正时返回空图
 * @details 尺寸读歪（比如单色指针的高度忘了折半）时宁可给空，让上层按「取不到形状」降级，
 *          也不要一张按错行距拼出来的图。
 */
TEST(CursorPixels, NonPositiveSizeYieldsEmptyImage)
{
    const std::vector<QRgb>          color;
    const std::vector<unsigned char> mask;
    EXPECT_TRUE(composeCursorImage(color, mask, 0, kHeight, true).isNull());
    EXPECT_TRUE(composeCursorImage(color, mask, kWidth, -1, false).isNull());
}

#ifdef Q_OS_WIN
namespace
{
    /// 背景色：GDI 没往上面画的那些格子会原样留着这一色
    const QColor kProbeBackground(255, 0, 255);

    /**
     * @brief 让 GDI 自己把这枚指针画到一张空图上
     * @details 判据取「GDI 画出来的样子」而不是我对 AND/XOR 两张面的理解：画出来是什么样，
     *          截图里就该是什么样，平面次序与上下序这些文档讲不清的东西都由这一次绘制定案。
     */
    [[nodiscard]] QImage renderCursorWithGdi(HCURSOR cursor, int width, int height)
    {
        BITMAPINFO info{};
        info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth       = width;
        info.bmiHeader.biHeight      = -height;
        info.bmiHeader.biPlanes      = 1;
        info.bmiHeader.biBitCount    = 32;
        info.bmiHeader.biCompression = BI_RGB;

        void   *bits   = nullptr;
        HDC     screen = GetDC(nullptr);
        HDC     memory = CreateCompatibleDC(screen);
        HBITMAP dib    = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        QImage  result;
        if (dib != nullptr && bits != nullptr && memory != nullptr)
        {
            auto *const previous = SelectObject(memory, dib);
            auto *const pixels   = static_cast<QRgb *>(bits);
            std::fill_n(pixels, static_cast<std::size_t>(width) * static_cast<std::size_t>(height), kProbeBackground.rgba());
            DrawIconEx(memory, 0, 0, cursor, width, height, 0, nullptr, DI_NORMAL);
            result = QImage(static_cast<const uchar *>(bits), width, height, width * 4, QImage::Format_ARGB32).copy();
            SelectObject(memory, previous);
        }
        if (dib != nullptr)
        {
            DeleteObject(dib);
        }
        if (memory != nullptr)
        {
            DeleteDC(memory);
        }
        if (screen != nullptr)
        {
            ReleaseDC(nullptr, screen);
        }
        return result;
    }
} // namespace

/**
 * @brief 钉住「解出来的指针」与「GDI 自己画出来的指针」逐格一致
 * @details 自造一枚 8×8 的单色指针（AND 与 XOR 两张面），一条路是我从位平面解，另一条路是让
 *          GDI 直接画一遍。两边不一致就是解错了平面——这一条只在 Windows 上成立。
 */
TEST(CursorPixelsFromHandle, DecodesWhatGdiItselfDraws)
{
    // GDI 的 DDB 行距按字对齐：8 像素宽的一行占 2 字节，第二个字节是补零
    unsigned char andPlane[16] = {0xFF, 0x00, 0x81, 0x00, 0x81, 0x00, 0x81, 0x00, 0x81, 0x00, 0x81, 0x00, 0x81, 0x00, 0xFF, 0x00};
    unsigned char xorPlane[16] = {0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x72, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00};

    const HCURSOR cursor = CreateCursor(nullptr, 2, 3, 8, 8, andPlane, xorPlane);
    ASSERT_NE(cursor, nullptr) << "自造指针都建不出来，这条判据无从谈起";

    const auto sprite = GyanisScribe::Core::cursorSpriteFromHandle(reinterpret_cast<quintptr>(cursor));
    ASSERT_TRUE(sprite.has_value()) << "GDI 那一段解不出这枚指针：真机上的指针同样解不出来";
    ASSERT_EQ(sprite->image.size(), QSize(8, 8));
    EXPECT_EQ(sprite->hotspot, (QPoint(2, 3)));

    const QImage drawn = renderCursorWithGdi(cursor, 8, 8);
    DeleteObject(cursor);
    ASSERT_EQ(drawn.size(), QSize(8, 8)) << "GDI 那一侧没画出来，判据不成立";

    int mismatched = 0;
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            const QColor mine = sprite->image.pixelColor(x, y);
            const QColor real = drawn.pixelColor(x, y);
            // 我判为透明的格子，GDI 也不该在上面落下颜色；其余格子要逐色相同
            // GDI 画进 32 位图时不写 alpha，那一格恒为 0：判透明只能看「有没有动过背景」
            const bool untouched = real.red() == kProbeBackground.red() && real.green() == kProbeBackground.green() && real.blue() == kProbeBackground.blue();
            const bool agrees    = (mine.alpha() == 0) ? untouched : (!untouched && real.red() == mine.red() && real.green() == mine.green() && real.blue() == mine.blue());
            if (!agrees)
            {
                ++mismatched;
            }
        }
    }
    EXPECT_EQ(mismatched, 0) << "解出来的指针与 GDI 画出来的不一致：平面次序或上下序读错了";
}
#endif
