// DXGI 0 超时路径的四条边界：等待超时有/无缓存、重复接口失效、全黑回落。
// 设备调用本身造不出假重复接口，这一组只钉纯决策层；真机取帧由 TestDxgiCaptureSource 的读数与门禁用例覆盖。

#include "Plugins/CaptureSources/DxgiAcquirePolicy.h"

#ifdef Q_OS_WIN
#include <windows.h>

#include <dxgi.h>

#include <gtest/gtest.h>

using GyanisScribe::Plugins::Dxgi::AcquireOutcome;
using GyanisScribe::Plugins::Dxgi::FrameDisposition;
using GyanisScribe::Plugins::Dxgi::resolveAcquireOutcome;
using GyanisScribe::Plugins::Dxgi::resolveFrameDisposition;

/**
 * @brief 钉住等待超时但有缓存帧时交回缓存，而不是报错
 */
TEST(DxgiAcquirePolicy, WaitTimeoutWithCachedFrameReusesIt)
{
    EXPECT_EQ(resolveAcquireOutcome(DXGI_ERROR_WAIT_TIMEOUT, true), AcquireOutcome::CachedFrame);
}

/**
 * @brief 钉住等待超时且没有缓存帧时按首帧未就绪上报
 */
TEST(DxgiAcquirePolicy, WaitTimeoutWithoutCachedFrameReportsFirstFrameMissing)
{
    EXPECT_EQ(resolveAcquireOutcome(DXGI_ERROR_WAIT_TIMEOUT, false), AcquireOutcome::FirstFrameMissing);
}

/**
 * @brief 钉住重复接口失效时走重开一轮，不论有没有缓存帧
 */
TEST(DxgiAcquirePolicy, AccessLostReopensDuplication)
{
    EXPECT_EQ(resolveAcquireOutcome(DXGI_ERROR_ACCESS_LOST, true), AcquireOutcome::ReopenDuplication);
    EXPECT_EQ(resolveAcquireOutcome(DXGI_ERROR_ACCESS_LOST, false), AcquireOutcome::ReopenDuplication);
}

/**
 * @brief 钉住成功取帧走新帧这一档
 */
TEST(DxgiAcquirePolicy, SuccessYieldsNewFrame)
{
    EXPECT_EQ(resolveAcquireOutcome(S_OK, false), AcquireOutcome::NewFrame);
    EXPECT_EQ(resolveAcquireOutcome(S_OK, true), AcquireOutcome::NewFrame);
}

/**
 * @brief 钉住其余失败原样上报，不被当成超时或失效
 */
TEST(DxgiAcquirePolicy, OtherFailuresAreReported)
{
    EXPECT_EQ(resolveAcquireOutcome(E_FAIL, true), AcquireOutcome::Failed);
    EXPECT_EQ(resolveAcquireOutcome(DXGI_ERROR_INVALID_CALL, false), AcquireOutcome::Failed);
}

/**
 * @brief 钉住全黑帧回落 Qt，其余帧直接交付
 */
TEST(DxgiAcquirePolicy, AllBlackFrameFallsBackToQt)
{
    EXPECT_EQ(resolveFrameDisposition(true), FrameDisposition::FallbackToQt);
    EXPECT_EQ(resolveFrameDisposition(false), FrameDisposition::Deliver);
}
#endif
