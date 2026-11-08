#include "Plugins/CaptureSources/DxgiCaptureSource.h"

#ifdef Q_OS_WIN
#include <QApplication>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QScreen>

#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

using GyanisScribe::Plugins::DxgiCaptureSource;

namespace
{
    std::unique_ptr<QApplication> application;

    class DxgiCaptureSourceFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
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

TEST_F(DxgiCaptureSourceFixture, TryGrabScreenReturnsAFrameOrStructuredError)
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
    if (!frame)
    {
        EXPECT_FALSE(frame.error().message.empty());
        return;
    }
    EXPECT_FALSE(frame->image.isNull());
    EXPECT_GT(source.framesFromDxgi(), 0);
}

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

    std::vector<qint64> elapsed;
    for (int index = 0; index < 100; ++index)
    {
        QElapsedTimer timer;
        timer.start();
        const auto frame = source.tryGrabScreen(QString{});
        elapsed.push_back(timer.nsecsElapsed() / 1000);
        if (!frame)
        {
            GTEST_SKIP() << "抓帧失败，无法完成读数：" << frame.error().message;
        }
    }

    std::ranges::sort(elapsed);
    std::cout << "Rewind capture readout us p50=" << elapsed[elapsed.size() / 2] << " p95=" << elapsed[elapsed.size() * 95 / 100]
              << " p99=" << elapsed[elapsed.size() * 99 / 100] << std::endl;
}
#endif
