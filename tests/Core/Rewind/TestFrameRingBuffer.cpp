#include "Core/Rewind/FrameRingBuffer.h"

#include <QColor>
#include <QImage>

#include <gtest/gtest.h>

using GyanisScribe::Core::FrameRingBuffer;
using GyanisScribe::Core::RewindFrameSample;

namespace
{
    RewindFrameSample makeSample(const quint64 id, const qint64 monotonicMs, const int bytes)
    {
        RewindFrameSample sample;
        sample.id            = id;
        sample.monotonicMs   = monotonicMs;
        sample.capturedAt    = QDateTime::currentDateTime();
        sample.encodedPixels = QByteArray(bytes, 'x');
        sample.thumbnail     = QImage(2, 2, QImage::Format_ARGB32_Premultiplied);
        sample.thumbnail.fill(Qt::red);
        sample.logicalSize       = QSize(100, 50);
        sample.devicePixelRatio  = 2.0;
        sample.screenName        = QStringLiteral("FakeScreen-1");
        sample.screenLogicalRect = QRect(0, 0, 100, 50);
        return sample;
    }
} // namespace

/**
 * @brief 钉住追加后能按样本号取回，且视图快照与读数同步增长
 */
TEST(FrameRingBuffer, AppendsAndFindsById)
{
    FrameRingBuffer buffer(10, 5, 1);
    ASSERT_TRUE(buffer.append(makeSample(1, 0, 10)).has_value());
    ASSERT_TRUE(buffer.append(makeSample(2, 200, 10)).has_value());

    EXPECT_EQ(buffer.stats().sampleCount, 2);
    EXPECT_TRUE(buffer.sampleById(1).has_value());
    EXPECT_FALSE(buffer.sampleById(99).has_value());
    EXPECT_EQ(buffer.views().size(), 2U);
}

/**
 * @brief 钉住时间跨度超过时长上限时从最旧一端淘汰，后到的帧保留
 */
TEST(FrameRingBuffer, EvictsByTime)
{
    FrameRingBuffer buffer(5, 5, 1);
    ASSERT_TRUE(buffer.append(makeSample(1, 0, 10)).has_value());
    ASSERT_TRUE(buffer.append(makeSample(2, 2000, 10)).has_value());
    ASSERT_TRUE(buffer.append(makeSample(3, 6000, 10)).has_value());

    EXPECT_EQ(buffer.stats().sampleCount, 2);
    EXPECT_FALSE(buffer.sampleById(1).has_value());
    EXPECT_TRUE(buffer.sampleById(3).has_value());
}

/**
 * @brief 钉住唯一编码字节超出预算时淘汰最旧的帧
 */
TEST(FrameRingBuffer, EvictsByBytes)
{
    FrameRingBuffer buffer(10, 5, 1);
    ASSERT_TRUE(buffer.append(makeSample(1, 0, 600 * 1024)).has_value());
    ASSERT_TRUE(buffer.append(makeSample(2, 200, 600 * 1024)).has_value());

    EXPECT_EQ(buffer.stats().sampleCount, 1);
    EXPECT_TRUE(buffer.sampleById(2).has_value());
}

/**
 * @brief 钉住未变化帧共用同一份编码字节时只按一份记账
 */
TEST(FrameRingBuffer, SharedEncodedBytesCountOnce)
{
    FrameRingBuffer  buffer(10, 5, 1);
    const QByteArray shared(6, 'x');

    auto first           = makeSample(1, 0, 0);
    auto second          = makeSample(2, 200, 0);
    first.encodedPixels  = shared;
    second.encodedPixels = shared;

    ASSERT_TRUE(buffer.append(first).has_value());
    ASSERT_TRUE(buffer.append(second).has_value());
    EXPECT_EQ(buffer.stats().uniquePixelBytes, 6);
    EXPECT_EQ(buffer.stats().sampleCount, 2);
}

/**
 * @brief 钉住单帧本身就超预算时拒绝追加，缓冲保持为空
 */
TEST(FrameRingBuffer, SingleOversizedFrameIsRejected)
{
    FrameRingBuffer buffer(10, 5, 1);
    const auto      result = buffer.append(makeSample(1, 0, 2 * 1024 * 1024));
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(buffer.stats().sampleCount, 0);
}

/**
 * @brief 钉住缩短时长上限后立即按新上限淘汰
 */
TEST(FrameRingBuffer, ReconfigureShorterDurationEvicts)
{
    FrameRingBuffer buffer(30, 5, 1);
    ASSERT_TRUE(buffer.append(makeSample(1, 0, 10)).has_value());
    ASSERT_TRUE(buffer.append(makeSample(2, 6000, 10)).has_value());

    buffer.reconfigure(5, 5, 1);
    EXPECT_EQ(buffer.stats().sampleCount, 1);
}

/**
 * @brief 钉住清空后样本与视图都为 0
 */
TEST(FrameRingBuffer, ClearDropsEverything)
{
    FrameRingBuffer buffer(10, 5, 1);
    ASSERT_TRUE(buffer.append(makeSample(1, 0, 10)).has_value());
    buffer.clear();
    EXPECT_EQ(buffer.stats().sampleCount, 0);
    EXPECT_TRUE(buffer.views().empty());
}
