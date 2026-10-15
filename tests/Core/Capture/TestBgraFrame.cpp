// BGRA 行拷的直测：DXGI 交回的缓冲行距按 256 字节对齐，判据必须能把「按 width*4 当行距」
// 这种斜切抓出来——在真机上那只会表现为「录出来的画面是歪的」，很难回溯到这一行。

#include "Core/Capture/BgraFrame.h"

#include <QColor>
#include <QImage>
#include <QSize>

#include <gtest/gtest.h>

#include <array>

using GyanisScribe::Core::imageFromBgraRows;

/**
 * @brief 钉住行距大于宽度时逐行按自己的行距拷，且像素序就是 BGRA
 * @details 每行尾巴上填 0xFF 当填充字节：一旦按 width×4 前进，这些字节会被读成下一行的头几个像素，
 *          判据里的逐像素比对当场红。
 */
TEST(BgraFrame, CopiesEachRowWithItsOwnPitch)
{
    constexpr int width  = 3;
    constexpr int height = 2;
    constexpr int pitch  = 16; // 比 width*4=12 大，模拟 DXGI 的对齐

    const std::array<std::array<QColor, width>, height> pixels = {
            std::array<QColor, width>{QColor(10, 20, 30), QColor(40, 50, 60), QColor(70, 80, 90)},
            std::array<QColor, width>{QColor(100, 110, 120), QColor(130, 140, 150), QColor(160, 170, 180)},
    };

    std::array<uchar, pitch * height> buffer{};
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            auto *pixel = buffer.data() + y * pitch + x * 4;
            pixel[0]    = static_cast<uchar>(pixels[y][x].blue());
            pixel[1]    = static_cast<uchar>(pixels[y][x].green());
            pixel[2]    = static_cast<uchar>(pixels[y][x].red());
            pixel[3]    = 0; // 桌面重复这一路 alpha 不参与显示，给 0 也必须是正常颜色
        }
        for (int padding = width * 4; padding < pitch; ++padding)
        {
            buffer[static_cast<std::size_t>(y * pitch + padding)] = 0xFF;
        }
    }

    const auto image = imageFromBgraRows(buffer.data(), pitch, QSize(width, height));
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.size(), QSize(width, height));

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            EXPECT_EQ(image.pixelColor(x, y), pixels[y][x]) << "第 " << y << " 行第 " << x << " 列的颜色不对：行距或通道序解错了";
        }
    }
}

/**
 * @brief 钉住退化入参交回空图，而不是编出一张尺寸错乱的图
 */
TEST(BgraFrame, DegenerateInputsYieldNoImage)
{
    std::array<uchar, 16> buffer{};

    EXPECT_TRUE(imageFromBgraRows(nullptr, 16, QSize(2, 2)).isNull()) << "空指针不该被当成地址读";
    EXPECT_TRUE(imageFromBgraRows(buffer.data(), 16, QSize(0, 2)).isNull());
    EXPECT_TRUE(imageFromBgraRows(buffer.data(), 16, QSize(2, 0)).isNull());
    EXPECT_TRUE(imageFromBgraRows(buffer.data(), 4, QSize(2, 2)).isNull()) << "行距比一行还短时会读到行外";
}
