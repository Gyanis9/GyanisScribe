#include "Plugins/CaptureSources/DxgiAcquirePolicy.h"

#ifdef Q_OS_WIN

#include <windows.h>

#include <dxgi.h>

namespace GyanisScribe::Plugins::Dxgi
{

    AcquireOutcome resolveAcquireOutcome(const long acquireResult, const bool hasCachedFrame) noexcept
    {
        if (acquireResult == DXGI_ERROR_WAIT_TIMEOUT)
        {
            // 桌面这一格没变：有上一帧就交缓存，没有就是重复接口开了之后首帧还没来
            return hasCachedFrame ? AcquireOutcome::CachedFrame : AcquireOutcome::FirstFrameMissing;
        }
        if (acquireResult == DXGI_ERROR_ACCESS_LOST)
        {
            // 切分辨率、锁屏或会话切换会让重复接口失效，只能放掉重开
            return AcquireOutcome::ReopenDuplication;
        }
        if (SUCCEEDED(acquireResult))
        {
            return AcquireOutcome::NewFrame;
        }
        return AcquireOutcome::Failed;
    }

    FrameDisposition resolveFrameDisposition(const bool isAllBlack) noexcept
    {
        return isAllBlack ? FrameDisposition::FallbackToQt : FrameDisposition::Deliver;
    }

} // namespace GyanisScribe::Plugins::Dxgi

#endif // Q_OS_WIN
