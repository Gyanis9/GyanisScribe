/**
 * @file RecordingTiming.h
 * @brief 录屏的节拍：什么时候该取下一帧，错过的那几格怎么办
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QtGlobal>

namespace GyanisScribe::Record
{

    /**
     * @brief 按目标帧率算节拍的时钟
     * @details 时钟只回答「现在该不该取一帧」，不自己起定时器：这样用例能把时间轴喂给它逐个判，
     *          而不是依赖真定时器的精度。取帧位置按「起始时刻 + n 帧」现算，不累加间隔，
     *          否则整数舍入会随时间漂出可见的间隔差。
     */
    class FrameClock
    {
    public:
        /**
         * @brief 建时钟
         * @param frameRate 目标帧率，必须为正；非正在构造时就当作 1，因为 0 帧率意味着「不录」
         * @param startedAtMs 录制起点，单调时钟毫秒
         * @note 第 0 帧算作已经取过：录屏一开始就有一帧底，编码器不会拿到空序列
         */
        FrameClock(int frameRate, qint64 startedAtMs);

        /**
         * @brief 现在该不该取一帧
         * @param nowMs 当前时刻，单调时钟毫秒
         * @return true 表示该取一帧（每次调用最多取得一帧）
         * @note 落后时丢掉中间那几格而不是连发补齐：补发会让一次卡顿变成几十帧重复画面，
         *       成品看起来像是卡在那里循环
         */
        [[nodiscard]] bool shouldTake(qint64 nowMs) noexcept;

        /**
         * @brief 下一格的到期时刻
         * @return 单调时钟毫秒
         */
        [[nodiscard]] qint64 nextDueMs() const noexcept;

        /// 已经消耗掉的格子数（含第 0 帧与被丢掉的格）：落后时中间那几格也算消耗，否则同一格会被取两次
        [[nodiscard]] int takenFrames() const noexcept;

        /// 目标帧率
        [[nodiscard]] int frameRate() const noexcept;

    private:
        [[nodiscard]] qint64 dueMsFor(int index) const noexcept;

        int    m_frameRate;   ///< 目标帧率
        qint64 m_startedAtMs; ///< 录制起点
        int    m_taken;       ///< 已取帧数
    };

    /**
     * @brief 一段时长能出多少帧
     * @param durationMs 时长，毫秒
     * @param frameRate 目标帧率
     * @return 帧数，向下取整
     * @details 进度条与编码参数都用它算帧数；向下取整是因为「还差半格的那一帧」不该被提前计入。
     */
    [[nodiscard]] constexpr int frameCountFor(qint64 durationMs, int frameRate)
    {
        return frameRate <= 0 ? 0 : static_cast<int>((durationMs * frameRate) / 1000);
    }

} // namespace GyanisScribe::Record
