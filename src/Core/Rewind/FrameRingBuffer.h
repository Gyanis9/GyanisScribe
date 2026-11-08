/**
 * @file FrameRingBuffer.h
 * @brief 按时间与字节上限淘汰的回溯帧环形缓冲
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Rewind/RewindTypes.h"

#include <deque>
#include <optional>
#include <vector>

namespace GyanisScribe::Core
{
    /**
     * @brief 回溯帧环形缓冲
     * @details 只保存编码后的样本；未变化帧共用同一份 QByteArray，字节预算按唯一数据指针记账。
     *          追加时先按时间、再按字节淘汰最旧；单帧本身超预算时拒绝追加，不静默产出空缓冲。
     * @note 只在 GUI 线程使用，内部不加锁。
     */
    class FrameRingBuffer
    {
    public:
        /**
         * @brief 建缓冲
         * @param bufferSeconds 时间上限，5–30 秒
         * @param frameRate 目标帧率，2–10
         * @param memoryBudgetMb 像素预算，单位 MB
         */
        FrameRingBuffer(int bufferSeconds, int frameRate, int memoryBudgetMb);

        /**
         * @brief 追加一个样本
         * @param sample 样本；encodedPixels 不能为空
         * @return 成功返回空；单帧超预算或样本无效返回错误
         */
        [[nodiscard]] Base::Expected<void> append(RewindFrameSample sample);

        /// 清空全部样本
        void clear();

        /// 选择器快照；只复制缩略图与元数据，不复制整帧
        [[nodiscard]] std::vector<RewindFrameView> views() const;

        /**
         * @brief 按样本号取样本
         * @param id 样本号
         * @return 找到返回样本副本；没有该号返回空
         */
        [[nodiscard]] std::optional<RewindFrameSample> sampleById(quint64 id) const;

        /// 当前读数
        [[nodiscard]] RewindBufferStats stats() const;

        /**
         * @brief 应用新容量
         * @param bufferSeconds 新的时间上限
         * @param frameRate 新的帧率
         * @param memoryBudgetMb 新的预算
         * @details 立即按新上限淘汰；若淘汰到只剩一帧仍超预算则清空并记日志。
         */
        void reconfigure(int bufferSeconds, int frameRate, int memoryBudgetMb);

    private:
        [[nodiscard]] qint64 uniquePixelBytes() const;
        [[nodiscard]] qint64 thumbnailBytes() const;
        void                 evictUntilWithinLimits();

        std::deque<RewindFrameSample> m_samples;
        int                           m_bufferSeconds     = 10;
        int                           m_frameRate         = 5;
        qint64                        m_memoryBudgetBytes = 100LL * 1024 * 1024;
    };
} // namespace GyanisScribe::Core
