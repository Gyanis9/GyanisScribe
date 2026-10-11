// 指针位平面的合成规则：三种指针（带 alpha、老式彩色、单色）各自的透明与颜色判据。
// 这一层刻意不碰 GDI——容器与本机都编得过，掩码位序解反这种错在两边都跑得出来。

#include "Capture/CursorPixels.h"

#include <QColor>
#include <QImage>
#include <QRgb>

#include <gtest/gtest.h>

#include <vector>

using GyanisScribe::Capture::composeCursorImage;
using GyanisScribe::Capture::cursorMonoRowBytes;

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
