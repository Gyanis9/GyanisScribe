#include "Core/Rewind/RewindFrameCodec.h"

#include <QColor>
#include <QImage>

#include <gtest/gtest.h>

using GyanisScribe::Core::decodeRewindFrame;
using GyanisScribe::Core::encodeRewindFrame;
using GyanisScribe::Core::makeRewindThumbnail;

/**
 * @brief 钉住编码再解码后尺寸与像素原样回来，且编码字节与缩略图都非空
 */
TEST(RewindFrameCodec, RoundTripsPixelsLosslessly)
{
    QImage image(8, 6, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(12, 34, 56));
    image.setPixelColor(2, 3, QColor(200, 10, 30));

    const auto encoded = encodeRewindFrame(image);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_FALSE(encoded->encodedPixels.isEmpty());
    EXPECT_FALSE(encoded->thumbnail.isNull());

    const auto decoded = decodeRewindFrame(encoded->encodedPixels);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->size(), image.size());
    EXPECT_EQ(decoded->pixelColor(2, 3), QColor(200, 10, 30));
}

/**
 * @brief 钉住缩略图按最大边等比缩小，原图本来就不超过上限时保持原尺寸
 */
TEST(RewindFrameCodec, ThumbnailKeepsAspectAndDoesNotUpscale)
{
    QImage largeImage(400, 200, QImage::Format_ARGB32_Premultiplied);
    largeImage.fill(Qt::red);
    const auto thumbnail = makeRewindThumbnail(largeImage);
    EXPECT_EQ(thumbnail.size(), QSize(160, 80));

    QImage smallImage(100, 50, QImage::Format_ARGB32_Premultiplied);
    smallImage.fill(Qt::blue);
    EXPECT_EQ(makeRewindThumbnail(smallImage).size(), QSize(100, 50));
}

/**
 * @brief 钉住拒绝面：空图不编码，损坏字节不解码
 */
TEST(RewindFrameCodec, RejectsEmptyImageAndCorruptData)
{
    EXPECT_FALSE(encodeRewindFrame(QImage{}).has_value());
    EXPECT_FALSE(decodeRewindFrame(QByteArrayLiteral("not an image")).has_value());
}
