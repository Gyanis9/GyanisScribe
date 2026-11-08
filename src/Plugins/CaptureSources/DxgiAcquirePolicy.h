/**
 * @file DxgiAcquirePolicy.h
 * @brief DXGI 取帧的纯决策层：等待超时、缓存、重开与全黑回落各走哪条路
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QtGlobal>

#ifdef Q_OS_WIN

namespace GyanisScribe::Plugins::Dxgi
{

    /**
     * @brief 一次 AcquireNextFrame 落在哪一档
     */
    enum class AcquireOutcome
    {
        NewFrame,          ///< 拿到了新帧
        CachedFrame,       ///< 桌面这一格没变，交回缓存帧
        FirstFrameMissing, ///< 桌面这一格没变且还没有缓存帧：首帧未就绪
        ReopenDuplication, ///< 重复接口失效，放掉重开一轮
        Failed,            ///< 其余失败，原样上报
    };

    /**
     * @brief 由 AcquireNextFrame 的返回值与缓存状态得出取帧结局
     * @param acquireResult AcquireNextFrame 交回的 HRESULT
     * @param hasCachedFrame 这块屏有没有可交回的上一帧
     * @return 调用方该走的那条路
     * @details 设备调用没法在单测里造假，这一层只钉决策；真机取帧由 DXGI 用例覆盖。
     */
    [[nodiscard]] AcquireOutcome resolveAcquireOutcome(long acquireResult, bool hasCachedFrame) noexcept;

    /**
     * @brief 新帧的处置方式
     */
    enum class FrameDisposition
    {
        Deliver,      ///< 直接交付
        FallbackToQt, ///< 全黑帧，改用 Qt 抓帧一次
    };

    /**
     * @brief 判断一帧要不要回落 Qt
     * @param isAllBlack 这一帧是不是整片全黑
     * @return 回落或直接交付
     * @details 桌面合成切换的瞬间 DXGI 会交回一整帧黑；直接交付会让回溯缓冲存进黑图。
     */
    [[nodiscard]] FrameDisposition resolveFrameDisposition(bool isAllBlack) noexcept;

} // namespace GyanisScribe::Plugins::Dxgi

#endif // Q_OS_WIN
