#include "Pin/PinViewFilter.h"

#include <QColor>
#include <QImage>

#include <gtest/gtest.h>

using GyanisScribe::Pin::pinViewFiltered;

namespace
{
    /// 2×1 的底样：不透明红 + 半透明青，一条像素同时管住「三通道」与「alpha 要留着」
    QImage makeSwatch()
    {
        QImage image{QSize{2, 1}, QImage::Format_ARGB32};
        image.setPixelColor(0, 0, QColor(255, 0, 0, 255));
        image.setPixelColor(1, 0, QColor(0, 128, 255, 64));
        return image;
    }
} // namespace

/**
 * @brief 钉住两档全关时原样交回：视图滤镜不该悄悄换掉一张图
 */
TEST(PinViewFilter, BothOffReturnsTheSourceUntouched)
{
    const QImage source = makeSwatch();
    const QImage result = pinViewFiltered(source, false, false);
    EXPECT_EQ(result, source) << "两档全关还动了像素";
}

/**
 * @brief 钉住灰度：按 luma 折成三通道同值，alpha 原样
 * @details luma 是 qGray 的口径 (r×11 + g×16 + b×5)/32：
 *          纯红 = 2805/32 = 87（整除截断）、(0,128,255) = (2048+1275)/32 = 103。
 *          半透明那一格的 alpha 必须还是 64——透明区不能因为看了一眼灰度就变实心。
 */
TEST(PinViewFilter, GrayscaleFlattensChannelsAndKeepsAlpha)
{
    const QImage result = pinViewFiltered(makeSwatch(), true, false);

    EXPECT_EQ(result.pixelColor(0, 0), QColor(87, 87, 87, 255)) << "纯红的 luma 不是手算的 87";
    EXPECT_EQ(result.pixelColor(1, 0), QColor(103, 103, 103, 64)) << "半透明格的灰度值或 alpha 不对";
}

/**
 * @brief 钉住反色：RGB 反相、alpha 不动
 */
TEST(PinViewFilter, InvertFlipsChannelsAndKeepsAlpha)
{
    const QImage result = pinViewFiltered(makeSwatch(), false, true);

    EXPECT_EQ(result.pixelColor(0, 0), QColor(0, 255, 255, 255)) << "红反相该是青";
    EXPECT_EQ(result.pixelColor(1, 0), QColor(255, 127, 0, 64)) << "反相没按 255-c 来，或 alpha 被动了";
}

/**
 * @brief 钉住两档的组合次序：先灰度、后反色
 * @details 纯红先灰度成 87，再反相成 168。反过来算（先反相成青再灰度）会得到 167——
 *          两种次序在这张底样上差 1，正好把次序钉死。
 */
TEST(PinViewFilter, GrayscaleRunsBeforeInvert)
{
    const QImage result = pinViewFiltered(makeSwatch(), true, true);
    EXPECT_EQ(result.pixelColor(0, 0), QColor(168, 168, 168, 255)) << "两档的组合次序不是「先灰度、后反色」";
}
