#include "Core/Rewind/FrameChangeDetector.h"

#include <QColor>
#include <QImage>

#include <gtest/gtest.h>

using GyanisScribe::Core::isCandidateChange;
using GyanisScribe::Core::isSameFrame;
using GyanisScribe::Core::makeFrameFingerprint;

/**
 * @brief 钉住同一画面同一标题既算相同帧，也不标为候选
 */
TEST(FrameChangeDetector, SameImageAndTitleIsSame)
{
    QImage image(64, 36, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    const auto left  = makeFrameFingerprint(image, QStringLiteral("Editor"));
    const auto right = makeFrameFingerprint(image, QStringLiteral("Editor"));
    EXPECT_TRUE(isSameFrame(left, right));
    EXPECT_FALSE(isCandidateChange(right, left));
}

/**
 * @brief 钉住整屏像素变了就算不同帧，并标为候选
 */
TEST(FrameChangeDetector, DifferentPixelsAreDifferent)
{
    QImage leftImage(64, 36, QImage::Format_ARGB32_Premultiplied);
    leftImage.fill(Qt::white);
    QImage rightImage = leftImage.copy();
    rightImage.fill(Qt::black);

    const auto left  = makeFrameFingerprint(leftImage, QStringLiteral("Editor"));
    const auto right = makeFrameFingerprint(rightImage, QStringLiteral("Editor"));
    EXPECT_FALSE(isSameFrame(left, right));
    EXPECT_TRUE(isCandidateChange(right, left));
}

/**
 * @brief 钉住画面没变、前台标题变了也要标为候选
 */
TEST(FrameChangeDetector, ForegroundTitleChangeIsCandidate)
{
    QImage image(64, 36, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    const auto left  = makeFrameFingerprint(image, QStringLiteral("Editor"));
    const auto right = makeFrameFingerprint(image, QStringLiteral("Dialog"));
    EXPECT_TRUE(isSameFrame(left, right));
    EXPECT_TRUE(isCandidateChange(right, left));
}

/**
 * @brief 钉住空图的指纹与任何帧都不相等，避免把没抓到画面的两帧当成没变化
 */
TEST(FrameChangeDetector, EmptyImageIsNotSame)
{
    const auto left  = makeFrameFingerprint(QImage{}, QStringLiteral("Editor"));
    const auto right = makeFrameFingerprint(QImage{}, QStringLiteral("Editor"));
    EXPECT_FALSE(isSameFrame(left, right));
}
