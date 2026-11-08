#include "Core/Rewind/FrameRingBuffer.h"

#include "Base/Logger.h"
#include "Base/Text.h"

#include <unordered_set>
#include <utility>

namespace GyanisScribe::Core
{
    FrameRingBuffer::FrameRingBuffer(const int bufferSeconds, const int frameRate, const int memoryBudgetMb) :
        m_bufferSeconds(bufferSeconds), m_frameRate(frameRate), m_memoryBudgetBytes(static_cast<qint64>(memoryBudgetMb) * 1024 * 1024)
    {
    }

    Base::Expected<void> FrameRingBuffer::append(RewindFrameSample sample)
    {
        // 空样本只会在调用方拼错时出现，收进缓冲后选择器那边没法区分「没变」与「缺帧」
        if (sample.encodedPixels.isEmpty())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("回溯帧缓冲拒绝空样本：编码结果是空的")});
        }
        // 单帧自己就超预算时先算总账再收：收进来再淘汰只会把已有历史一起挤掉
        if (sample.encodedPixels.size() > m_memoryBudgetBytes)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument,
                                               Base::uiFormat(Base::uiTextUtf8("回溯帧本身超过内存预算（{} 字节）：请降低时长或帧率"), sample.encodedPixels.size())});
        }

        m_samples.push_back(std::move(sample));
        evictUntilWithinLimits();

        // 淘汰按「至少留一帧」收手，所以这一帧的缩略图仍可能把总账顶过预算，此时撤回它并如实报错
        const auto current = stats();
        if (current.overBudget)
        {
            m_samples.pop_back();
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiFormat(Base::uiTextUtf8("回溯缓冲超过内存预算（{} 字节）：请降低时长或帧率"),
                                                                                                current.uniquePixelBytes + current.thumbnailBytes)});
        }
        return {};
    }

    void FrameRingBuffer::clear()
    {
        m_samples.clear();
    }

    std::vector<RewindFrameView> FrameRingBuffer::views() const
    {
        std::vector<RewindFrameView> result;
        result.reserve(m_samples.size());
        for (const auto &sample: m_samples)
        {
            result.push_back(RewindFrameView{sample.id, sample.thumbnail, sample.capturedAt, sample.isCandidate, sample.foregroundTitle});
        }
        return result;
    }

    std::optional<RewindFrameSample> FrameRingBuffer::sampleById(const quint64 id) const
    {
        for (const auto &sample: m_samples)
        {
            if (sample.id == id)
            {
                return sample;
            }
        }
        return std::nullopt;
    }

    RewindBufferStats FrameRingBuffer::stats() const
    {
        RewindBufferStats result;
        result.sampleCount      = static_cast<int>(m_samples.size());
        result.uniquePixelBytes = uniquePixelBytes();
        result.thumbnailBytes   = thumbnailBytes();
        if (!m_samples.empty())
        {
            // 首尾都是时间有序追加进来的，跨度就是两者的单调时钟差
            result.coveredMilliseconds = m_samples.back().monotonicMs - m_samples.front().monotonicMs;
        }
        result.overBudget = result.uniquePixelBytes + result.thumbnailBytes > m_memoryBudgetBytes;
        return result;
    }

    void FrameRingBuffer::reconfigure(const int bufferSeconds, const int frameRate, const int memoryBudgetMb)
    {
        m_bufferSeconds     = bufferSeconds;
        m_frameRate         = frameRate;
        m_memoryBudgetBytes = static_cast<qint64>(memoryBudgetMb) * 1024 * 1024;
        evictUntilWithinLimits();
        // 预算缩得比任何一帧都小时淘汰收不了场，清空比留一个永远超限的缓冲更容易向用户解释
        if (stats().overBudget)
        {
            Base::Logger::warning("回溯缓冲在缩小预算后仍超限，清空已有样本");
            clear();
        }
    }

    qint64 FrameRingBuffer::uniquePixelBytes() const
    {
        // 未变化帧与上一帧共用同一份字节，按数据指针去重才能反映真实占用
        std::unordered_set<const void *> seen;
        qint64                           total = 0;
        for (const auto &sample: m_samples)
        {
            const auto *key = static_cast<const void *>(sample.encodedPixels.constData());
            if (seen.insert(key).second)
            {
                total += sample.encodedPixels.size();
            }
        }
        return total;
    }

    qint64 FrameRingBuffer::thumbnailBytes() const
    {
        qint64 total = 0;
        for (const auto &sample: m_samples)
        {
            total += sample.thumbnail.sizeInBytes();
        }
        return total;
    }

    void FrameRingBuffer::evictUntilWithinLimits()
    {
        const auto timeLimit = static_cast<qint64>(m_bufferSeconds) * 1000LL;
        // 至少留一帧：把最后一帧也淘汰掉等于静默丢光历史，预算问题该在追加时如实报错
        while (m_samples.size() > 1 && m_samples.back().monotonicMs - m_samples.front().monotonicMs > timeLimit)
        {
            m_samples.pop_front();
        }
        while (m_samples.size() > 1 && uniquePixelBytes() + thumbnailBytes() > m_memoryBudgetBytes)
        {
            m_samples.pop_front();
        }
    }
} // namespace GyanisScribe::Core
