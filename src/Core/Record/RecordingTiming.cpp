#include "Core/Record/RecordingTiming.h"

#include <algorithm>

namespace GyanisScribe::Core
{
    FrameClock::FrameClock(const int frameRate, const qint64 startedAtMs)
        // 0 或负帧率在这里当成 1 帧每秒：帧率 0 的含义是「不录」，那种调用不该由时钟兜住，
        // 兜住只会让上层误判成「一直在录但每帧都很慢」
        : m_frameRate(std::max(1, frameRate)), m_startedAtMs(startedAtMs), m_taken(1)
    {
    }

    qint64 FrameClock::dueMsFor(const int index) const noexcept
    {
        // 位置按起点加「第 n 帧的理论时刻」现算：不累加 1000/帧率 的舍入误差，长跑也不会漂
        return m_startedAtMs + (static_cast<qint64>(index) * 1000) / m_frameRate;
    }

    bool FrameClock::shouldTake(const qint64 nowMs) noexcept
    {
        if (nowMs < dueMsFor(m_taken))
        {
            return false;
        }

        // 落后很多时对齐到最后那一格，中间丢掉的帧不补：补发会把一次卡顿变成几十帧重复画面。
        // 落点要在「这一格之后」：只对齐到 targetIndex 的话，同一时刻的第二次调用会再取一帧
        const auto targetIndex = static_cast<int>((nowMs - m_startedAtMs) * m_frameRate / 1000);
        m_taken                = std::max(m_taken + 1, targetIndex + 1);
        return true;
    }

    qint64 FrameClock::nextDueMs() const noexcept
    {
        return dueMsFor(m_taken);
    }

    int FrameClock::takenFrames() const noexcept
    {
        return m_taken;
    }

    int FrameClock::frameRate() const noexcept
    {
        return m_frameRate;
    }

} // namespace GyanisScribe::Core
