#include "Core/Record/RecordingSession.h"

#include "Core/Record/RecordingTiming.h"

#include <algorithm>
#include <format>

namespace
{
    /// 还没出错时 failure() 返回的空错误：用一个函数内静态量，避免在头文件里再铺一个默认构造入口
    const GyanisScribe::Base::Error &noError()
    {
        static const GyanisScribe::Base::Error kNone{.code = GyanisScribe::Base::ErrorCode::Unknown, .message = {}};
        return kNone;
    }
} // namespace

namespace GyanisScribe::Core
{
    RecordingSession::RecordingSession(const RecorderConfig &config, IRecordingSink &sink, const qint64 startedAtMs, const int countdownSeconds) :
        m_config(config), m_sink(sink), m_startedAtMs(startedAtMs), m_recordingStartMs(startedAtMs + static_cast<qint64>(std::max(0, countdownSeconds)) * 1000),
        m_countdownSeconds(std::max(0, countdownSeconds)), m_state(m_countdownSeconds > 0 ? RecordingState::Countdown : RecordingState::Recording)
        // 节拍从「倒计时结束那一刻」起算，这样恢复录制与开始录制的第 0 帧对齐在同一格上
        ,
        m_clock(config.frameRate, m_recordingStartMs)
    {
    }

    qint64 RecordingSession::virtualTime(const qint64 nowMs) const
    {
        // 虚拟时间 = 墙上时间减去累计暂停：暂停的那一段不该占用节拍，也不该计入时长。
        // 暂停还没结束时这一截尚未累加，要冻结在暂停那一刻，否则暂停中的每一跳都在推进时长
        if (m_state == RecordingState::Paused)
        {
            return m_pausedFromMs - m_pausedTotalMs;
        }
        return nowMs - m_pausedTotalMs;
    }

    SessionTick RecordingSession::tick(const qint64 nowMs, const QImage &frame)
    {
        if (m_state == RecordingState::Done || m_state == RecordingState::Failed)
        {
            return SessionTick{.state = m_state};
        }

        if (m_state == RecordingState::Paused)
        {
            // 暂停期间画面照常会抓到，但不写：写进去就会把「暂停」变成「录了一段静止画面」
            return SessionTick{.state = m_state};
        }

        if (m_state == RecordingState::Countdown)
        {
            const auto remainingMs = m_recordingStartMs - nowMs;
            if (remainingMs > 0)
            {
                // 向上取整：剩 1200ms 时报「2 秒」，报 1 会让用户以为已经可以开始了
                const auto remainingSeconds = static_cast<int>((remainingMs + 999) / 1000);
                return SessionTick{.state = m_state, .countdownRemainingSeconds = remainingSeconds};
            }
            m_state = RecordingState::Recording;
        }

        SessionTick result{.state = m_state};
        // 第一帧不等节拍：节拍构造时就把第 0 帧算作已取，等下去等于白丢开头一段
        const bool due = m_firstFramePending || m_clock.shouldTake(virtualTime(nowMs));
        if (due)
        {
            if (auto written = m_sink.write(frame, m_sink.writtenFrames()); !written)
            {
                // 写盘失败就整段收：继续录只会得到一份缺帧的成品，那种文件比不录更坏
                m_failure    = written.error();
                m_state      = RecordingState::Failed;
                result.state = m_state;
                return result;
            }
            m_firstFramePending = false;
            result.frameStored  = true;
        }

        result.state                     = m_state;
        result.durationReached           = recordedDurationMs(nowMs) >= static_cast<qint64>(m_config.maximumDurationSeconds) * 1000;
        result.countdownRemainingSeconds = 0;
        return result;
    }

    void RecordingSession::pause(const qint64 nowMs)
    {
        if (m_state != RecordingState::Recording)
        {
            return;
        }
        m_pausedFromMs = nowMs;
        m_state        = RecordingState::Paused;
    }

    void RecordingSession::resume(const qint64 nowMs)
    {
        if (m_state != RecordingState::Paused || nowMs <= m_pausedFromMs)
        {
            // 时间倒着走不认：单调时钟不该倒退，认了只会让累计暂停时长变成负数，节拍随即乱掉
            return;
        }
        m_pausedTotalMs += nowMs - m_pausedFromMs;
        m_pausedFromMs = 0;
        m_state        = RecordingState::Recording;
    }

    Base::Expected<void> RecordingSession::finish(const qint64 nowMs)
    {
        if (m_state == RecordingState::Done)
        {
            return {};
        }
        if (m_state == RecordingState::Failed)
        {
            return std::unexpected(m_failure);
        }

        // 还在暂停里也要能收尾：把暂停中那一段算进去，否则时长统计会比实际少一截
        if (m_state == RecordingState::Paused)
        {
            resume(nowMs);
        }

        if (auto sealed = m_sink.seal(); !sealed)
        {
            m_failure = sealed.error();
            m_state   = RecordingState::Failed;
            return std::unexpected(m_failure);
        }
        m_state = RecordingState::Done;
        return {};
    }

    RecordingState RecordingSession::state() const noexcept
    {
        return m_state;
    }

    int RecordingSession::storedFrames() const noexcept
    {
        return m_sink.writtenFrames();
    }

    qint64 RecordingSession::recordedDurationMs(const qint64 nowMs) const
    {
        if (m_state == RecordingState::Idle || m_state == RecordingState::Countdown)
        {
            return 0;
        }
        return std::max<qint64>(0, virtualTime(nowMs) - m_recordingStartMs);
    }

    const Base::Error &RecordingSession::failure() const noexcept
    {
        return m_state == RecordingState::Failed ? m_failure : noError();
    }

} // namespace GyanisScribe::Core
