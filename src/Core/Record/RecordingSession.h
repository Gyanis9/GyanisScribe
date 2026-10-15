/**
 * @file RecordingSession.h
 * @brief 一轮录屏的状态机：倒计时、暂停、按节拍收帧与时长上限
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Record/RecorderConfig.h"
#include "Core/Record/RecordingSinks.h"
#include "Core/Record/RecordingTiming.h"

#include <QImage>

namespace GyanisScribe::Core
{

    /// 一轮录屏所处的状态
    enum class RecordingState
    {
        Idle,      ///< 还没开始
        Countdown, ///< 倒计时里，不写帧
        Recording, ///< 正在按节拍写帧
        Paused,    ///< 暂停：时间在走，帧不写
        Done,      ///< 正常收尾，帧序列已封好
        Failed,    ///< 出过错，后续 tick 全部忽略
    };

    /**
     * @brief 一次 tick 的结果
     * @details 把结果做成值而不是一路发信号：定时器那一层照着它决定要不要通知界面，
     *          用例则可以直接判「这一跳写没写帧」。
     */
    struct SessionTick
    {
        RecordingState state;                             ///< tick 之后的状态
        bool           frameStored               = false; ///< 这一跳是否写了一帧
        bool           durationReached           = false; ///< 是否触到了时长上限，调用方该收尾
        int            countdownRemainingSeconds = 0;     ///< 倒计时剩余整秒数
    };

    /**
     * @brief 录屏会话
     * @details 不认识 QTimer、也不认识抓屏来源：外部每次跳动作一次 tick 并把这一帧交进来。
     *          这样时间轴由用例给定，判据不依赖「定时器恰好这时候响了」。
     * @note 暂停用「虚拟时间」实现：喂给节拍的位置是 nowMs 减去累计暂停时长，
     *       恢复之后节拍会接着暂停前那一格走，而不是按墙上时钟补发一堆重复帧。
     */
    class RecordingSession
    {
    public:
        /**
         * @brief 建会话
         * @param config 录屏参数（帧率与时长上限从这儿取）
         * @param sink 帧的去处，生命周期必须长于本会话
         * @param startedAtMs 开始时刻，单调时钟毫秒
         * @param countdownSeconds 倒计时秒数，0 表示直接开始
         */
        RecordingSession(const RecorderConfig &config, IRecordingSink &sink, qint64 startedAtMs, int countdownSeconds);

        /**
         * @brief 推进一格
         * @param nowMs 当前时刻，单调时钟毫秒
         * @param frame 这一跳抓到的画面，物理像素
         * @return tick 结果；已收尾或已失败的会话返回原状态且什么都不做
         */
        [[nodiscard]] SessionTick tick(qint64 nowMs, const QImage &frame);

        /**
         * @brief 暂停：之后的 tick 都不写帧，也不计入录制时长
         * @param nowMs 暂停时刻
         * @note 只有 Recording 状态下才生效，其它状态调用是空操作
         */
        void pause(qint64 nowMs);

        /**
         * @brief 继续：开始把这段时间记入累计暂停时长
         * @param nowMs 恢复时刻
         */
        void resume(qint64 nowMs);

        /**
         * @brief 收尾：让帧去处封盘
         * @param nowMs 收尾时刻
         * @return 成功返回空载荷；一帧都没有或写盘报错时返回原因，并把状态置为 Failed
         */
        [[nodiscard]] Base::Expected<void> finish(qint64 nowMs);

        /// 当前状态
        [[nodiscard]] RecordingState state() const noexcept;

        /// 已写下的帧数
        [[nodiscard]] int storedFrames() const noexcept;

        /**
         * @brief 到这里为止录了多久
         * @param nowMs 当前时刻
         * @return 实际录制时长（不含倒计时与暂停），毫秒
         */
        [[nodiscard]] qint64 recordedDurationMs(qint64 nowMs) const;

        /**
         * @brief 失败原因
         * @return 仅在 Failed 状态下非空
         */
        [[nodiscard]] const Base::Error &failure() const noexcept;

    private:
        /// 把墙上时间换成录制用的虚拟时间
        [[nodiscard]] qint64 virtualTime(qint64 nowMs) const;

        RecorderConfig  m_config;                   ///< 参数
        IRecordingSink &m_sink;                     ///< 帧的去处，不拥有
        qint64          m_startedAtMs;              ///< 开始时刻
        qint64          m_recordingStartMs;         ///< 倒计时结束、真正开始录的时刻，时长从这里算
        int             m_countdownSeconds;         ///< 倒计时秒数
        RecordingState  m_state;                    ///< 当前状态
        FrameClock      m_clock;                    ///< 节拍
        qint64          m_pausedFromMs      = 0;    ///< 本次暂停起点，未在暂停中为 0
        qint64          m_pausedTotalMs     = 0;    ///< 累计暂停时长
        bool            m_firstFramePending = true; ///< 第一帧无条件写：节拍把第 0 帧算作已取，不补这一帧就白丢开头那 100 毫秒
        Base::Error     m_failure;                  ///< 失败原因，Failed 时有效
    };

} // namespace GyanisScribe::Core
