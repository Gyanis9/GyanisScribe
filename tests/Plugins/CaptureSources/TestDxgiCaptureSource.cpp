// 真机 DXGI 抓帧：普通读数只报 p50/p95/p99；回溯抓帧的 16ms p99 门禁单独一条，
// 要 GYANISSCRIBE_REWIND_GATE=1 才跑。读数不断言时间（仓库惯例），但每一帧都必须真的抓到；
// 门禁在被启用时必须能红。首帧的重复接口与暂存纹理初始化放在预热里丢掉，不进样本。

#include "Plugins/CaptureSources/DxgiCaptureSource.h"

#ifdef Q_OS_WIN
#include <QApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>

#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

using GyanisScribe::Plugins::DxgiCaptureSource;

namespace
{
    /// 门禁默认采样 30 秒：给一档能一次跑完的时长，长跑读数留给发布前的全量门禁
    constexpr int kDefaultGateSeconds = 30;

    /// 门禁的 p99 上限：1440p 下 GUI 线程单次抓帧超过它就该改用专用抓帧线程
    constexpr qint64 kGateP99LimitUs = 16000;

    /// 普通读数的样本量：至少 200 帧，p99 才不是拿几个点凑出来的
    constexpr int kReadoutSampleCount = 200;

    std::unique_ptr<QApplication> application;

    /**
     * @brief 最近秩百分位
     * @param sortedSamples 升序排好的样本，不得为空
     * @param percentile 0–100 的百分位
     * @return 第 ceil(percentile/100 × n) 个样本；空样本交回 0
     */
    qint64 percentileUs(const std::vector<qint64> &sortedSamples, const int percentile)
    {
        if (sortedSamples.empty())
        {
            return 0;
        }
        const std::size_t rank = (static_cast<std::size_t>(percentile) * sortedSamples.size() + 99) / 100;
        return sortedSamples[rank == 0 ? 0 : rank - 1];
    }

    /// 打一行读数；样本必须已经升序
    void printPercentiles(const char *label, const std::vector<qint64> &sortedSamples)
    {
        std::cout << label << " samples=" << sortedSamples.size() << " p50=" << percentileUs(sortedSamples, 50) << " p95=" << percentileUs(sortedSamples, 95)
                  << " p99=" << percentileUs(sortedSamples, 99) << '\n';
    }

    class DxgiCaptureSourceFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestDxgiCaptureSource";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }
    };

    class RewindCaptureGate : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestDxgiCaptureSource";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }
    };
} // namespace

/**
 * @brief 钉住 0 超时路径真能从 DXGI 抓到一帧
 * @details 抓帧失败不再当成结构化错误放过：isUsable() 为真就要求真出帧；
 *          拿到的是 Qt 回落帧时 framesFromDxgi() 停在 0，同样判红。
 */
TEST_F(DxgiCaptureSourceFixture, TryGrabScreenReturnsAFrame)
{
    if (QGuiApplication::screens().isEmpty())
    {
        GTEST_SKIP() << "这个环境没有屏幕可抓";
    }

    const DxgiCaptureSource source;
    if (!source.isUsable())
    {
        GTEST_SKIP() << "本机建不起 DXGI 抓帧来源";
    }

    const auto frame = source.tryGrabScreen(QString{});
    ASSERT_TRUE(frame.has_value()) << frame.error().message;
    EXPECT_FALSE(frame->image.isNull());
    EXPECT_GT(frame->image.width(), 0);
    EXPECT_GT(frame->image.height(), 0);
    EXPECT_GT(source.framesFromDxgi(), 0) << "这一帧是从 Qt 回落路径拿到的，DXGI 没有真正取到帧";
}

/**
 * @brief 量 0 超时路径的单次抓帧耗时（读数用例，不断言时间）
 * @details 预热一帧并丢弃：开桌面重复、建暂存纹理与偶发的首帧全黑回落都只该发生一次。
 *          预热之后每一帧都必须成功，失败按 ASSERT 判红而不是跳过。
 */
TEST_F(DxgiCaptureSourceFixture, TryGrabScreenReadout)
{
    if (QGuiApplication::screens().isEmpty())
    {
        GTEST_SKIP() << "这个环境没有屏幕可抓";
    }

    const DxgiCaptureSource source;
    if (!source.isUsable())
    {
        GTEST_SKIP() << "本机建不起 DXGI 抓帧来源";
    }

    const auto warmup = source.tryGrabScreen(QString{});
    ASSERT_TRUE(warmup.has_value()) << "预热这一帧就没抓到：" << warmup.error().message;
    ASSERT_FALSE(warmup->image.isNull());
    ASSERT_GT(source.framesFromDxgi(), 0) << "预热帧来自 Qt 回落，DXGI 读数无从量起";

    std::vector<qint64> elapsedUs;
    elapsedUs.reserve(kReadoutSampleCount);
    for (int index = 0; index < kReadoutSampleCount; ++index)
    {
        QElapsedTimer timer;
        timer.start();
        const auto frame = source.tryGrabScreen(QString{});
        elapsedUs.push_back(timer.nsecsElapsed() / 1000);
        ASSERT_TRUE(frame.has_value()) << "第 " << index << " 帧没抓到：" << frame.error().message;
        ASSERT_FALSE(frame->image.isNull());
    }

    std::ranges::sort(elapsedUs);
    printPercentiles("Rewind capture readout us", elapsedUs);
}

/**
 * @brief 回溯抓帧的 p99 门禁：1440p 下 GUI 线程单次抓帧不得超过 16ms
 * @details 默认不跑（普通 CI 只跑读数），GYANISSCRIBE_REWIND_GATE=1 才量；
 *          GYANISSCRIBE_REWIND_GATE_SECONDS 控制采样时长，默认 30 秒。启用后 p99 超线就判红。
 */
TEST_F(RewindCaptureGate, P99Under16Ms)
{
    if (qEnvironmentVariable("GYANISSCRIBE_REWIND_GATE") != "1")
    {
        GTEST_SKIP() << "门禁默认不跑：设 GYANISSCRIBE_REWIND_GATE=1 才量 p99";
    }
    if (QGuiApplication::screens().isEmpty())
    {
        GTEST_SKIP() << "这个环境没有屏幕可抓";
    }

    const DxgiCaptureSource source;
    if (!source.isUsable())
    {
        GTEST_SKIP() << "本机建不起 DXGI 抓帧来源";
    }

    const auto warmup = source.tryGrabScreen(QString{});
    ASSERT_TRUE(warmup.has_value()) << "预热这一帧就没抓到：" << warmup.error().message;
    ASSERT_GT(source.framesFromDxgi(), 0) << "预热帧来自 Qt 回落，门禁量不到 DXGI";

    bool durationOk = false;
    int  seconds    = qEnvironmentVariableIntValue("GYANISSCRIBE_REWIND_GATE_SECONDS", &durationOk);
    if (!durationOk || seconds <= 0)
    {
        seconds = kDefaultGateSeconds;
    }
    const qint64 durationMs = static_cast<qint64>(seconds) * 1000;

    std::vector<qint64> elapsedUs;
    QElapsedTimer       total;
    total.start();
    while (total.elapsed() < durationMs)
    {
        QElapsedTimer timer;
        timer.start();
        const auto frame = source.tryGrabScreen(QString{});
        elapsedUs.push_back(timer.nsecsElapsed() / 1000);
        ASSERT_TRUE(frame.has_value()) << "门禁期间第 " << elapsedUs.size() << " 帧没抓到：" << frame.error().message;
    }

    std::ranges::sort(elapsedUs);
    printPercentiles("Rewind capture gate us", elapsedUs);
    EXPECT_LE(percentileUs(elapsedUs, 99), kGateP99LimitUs) << "1440p 下 GUI 线程单次抓帧 p99 超过 16ms，要改用专用抓帧线程";
}
#endif
