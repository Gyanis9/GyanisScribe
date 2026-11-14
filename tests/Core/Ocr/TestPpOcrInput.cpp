#include "Core/Ocr/PpOcrInput.h"

#include <QImage>

#include <gtest/gtest.h>

namespace Core = GyanisScribe::Core;

/// 输入构造器没有共享状态，空 fixture 只为按组聚合用例
class PpOcrInputFixture : public ::testing::Test
{
};

TEST_F(PpOcrInputFixture, DetInputResizesLongSideAndPadsToMultipleOf32)
{
    QImage image(100, 50, QImage::Format_RGB32); image.fill(qRgb(255, 255, 255));
    const auto input = Core::buildDetInput(image, 96);
    EXPECT_EQ(input.width, 96); EXPECT_EQ(input.height, 64);   // 96x48 -> 补到 64
    EXPECT_DOUBLE_EQ(input.scaleX, 0.96); EXPECT_DOUBLE_EQ(input.scaleY, 0.96);
    EXPECT_EQ(input.data.size(), 3u * 96u * 64u);
}
TEST_F(PpOcrInputFixture, DetInputUsesBgrAndPaddleNormalization)
{
    QImage image(32, 32, QImage::Format_RGB32); image.fill(qRgb(255, 0, 0)); // 纯红
    const auto input = Core::buildDetInput(image, 32);
    // 三通道按平面排开：三块等长的平面依次是 B、G、R，纯色图每块取首元素即可
    const auto planeSize = static_cast<size_t>(input.width) * static_cast<size_t>(input.height);
    EXPECT_NEAR(input.data[0], (0.0/255 - 0.485)/0.229, 1e-4);             // B
    EXPECT_NEAR(input.data[planeSize], (0.0/255 - 0.456)/0.224, 1e-4);     // G
    EXPECT_NEAR(input.data[planeSize * 2], (255.0/255 - 0.406)/0.225, 1e-4); // R
}
TEST_F(PpOcrInputFixture, RecInputTargetsHeight48AndClampsWidth)
{
    QImage wide(5000, 30, QImage::Format_RGB32); wide.fill(Qt::white);
    EXPECT_EQ(Core::buildRecInput(wide).width, 2400);
    QImage normal(100, 30, QImage::Format_RGB32); normal.fill(Qt::white);
    const auto rec = Core::buildRecInput(normal);
    EXPECT_EQ(rec.height, 48); EXPECT_EQ(rec.width, 160);       // round(48*100/30)
    QImage narrow(10, 48, QImage::Format_RGB32); narrow.fill(Qt::white);
    EXPECT_EQ(Core::buildRecInput(narrow).width, 16);           // 下限夹取
}
