#include "Core/Memory/DuplicateHash.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace GyanisScribe::Core
{
    namespace
    {
        /// dHash 固定比较 9×8 网格的每行相邻像素，得到 8×8 位
        inline constexpr int kDHashWidth  = 9;
        inline constexpr int kDHashHeight = 8;

        /// 64 位哈希的小写十六进制长度
        inline constexpr int kDHashHexLength = 16;

        /// 无法比较时的距离上限，大于任何合法 64 位哈希的 Hamming 距离
        inline constexpr int kInvalidHammingDistance = 64;

        /// 十六进制字符的数值，非法字符以负数表示
        int hexDigitValue(const QChar character)
        {
            const ushort code = character.unicode();
            if (code >= '0' && code <= '9')
            {
                return code - '0';
            }
            if (code >= 'a' && code <= 'f')
            {
                return code - 'a' + 10;
            }
            if (code >= 'A' && code <= 'F')
            {
                return code - 'A' + 10;
            }
            return -1;
        }
    } // namespace

    QString computeDHash(const QImage &image)
    {
        if (image.isNull())
        {
            return {};
        }

        // 统一先转灰度再缩放：感知差异来自亮度结构，逐通道缩放会把色偏算成结构变化
        const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
        if (gray.isNull())
        {
            return {};
        }

        const QImage scaled = gray.scaled(kDHashWidth, kDHashHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (scaled.isNull() || scaled.width() != kDHashWidth || scaled.height() != kDHashHeight)
        {
            return {};
        }

        // 平滑缩放可能换掉单通道格式，按行读取前把格式对齐回 8 位灰度
        const QImage normalized = scaled.format() == QImage::Format_Grayscale8 ? scaled : scaled.convertToFormat(QImage::Format_Grayscale8);
        if (normalized.isNull())
        {
            return {};
        }

        quint64 hash = 0;
        for (int row = 0; row < kDHashHeight; ++row)
        {
            const uchar *line = normalized.constScanLine(row);
            for (int column = 0; column + 1 < kDHashWidth; ++column)
            {
                hash <<= 1;
                // 左亮于右说明此处有从左到右的下降沿；位序与下方十六进制输出一致，距离比较不受影响
                if (line[column] > line[column + 1])
                {
                    hash |= 1U;
                }
            }
        }
        return QString::number(hash, 16).rightJustified(kDHashHexLength, QLatin1Char('0'));
    }

    int hammingDistance(const QString &left, const QString &right)
    {
        if (left.size() != kDHashHexLength || right.size() != kDHashHexLength)
        {
            return kInvalidHammingDistance;
        }

        int distance = 0;
        for (qsizetype index = 0; index < kDHashHexLength; ++index)
        {
            const int leftValue  = hexDigitValue(left.at(index));
            const int rightValue = hexDigitValue(right.at(index));
            // 任一侧含非十六进制字符时整对哈希不可比，按最大距离处理
            if (leftValue < 0 || rightValue < 0)
            {
                return kInvalidHammingDistance;
            }
            distance += std::popcount(static_cast<unsigned int>(leftValue ^ rightValue));
        }
        return distance;
    }

    std::vector<std::vector<MemoryResult>> groupDuplicates(std::vector<MemoryResult> results, const int threshold)
    {
        const std::size_t count = results.size();
        if (count == 0)
        {
            return {};
        }

        // 并查集只按位比较两两距离，当前结果页最多 200 条，O(n²) 足够
        std::vector<std::size_t> parents(count);
        std::iota(parents.begin(), parents.end(), std::size_t{0});

        const auto findRoot = [&parents](std::size_t index)
        {
            while (parents[index] != index)
            {
                parents[index] = parents[parents[index]];
                index          = parents[index];
            }
            return index;
        };

        for (std::size_t left = 0; left < count; ++left)
        {
            if (results[left].dhash.isEmpty())
            {
                continue;
            }
            for (std::size_t right = left + 1; right < count; ++right)
            {
                if (results[right].dhash.isEmpty())
                {
                    continue;
                }
                if (hammingDistance(results[left].dhash, results[right].dhash) <= threshold)
                {
                    const std::size_t leftRoot  = findRoot(left);
                    const std::size_t rightRoot = findRoot(right);
                    if (leftRoot != rightRoot)
                    {
                        parents[rightRoot] = leftRoot;
                    }
                }
            }
        }

        // 一组在结果页里的下标，以及组内最新条目的原位置
        struct MemberGroup
        {
            std::vector<std::size_t> members;
            std::size_t              newestOriginalIndex = 0;
        };

        constexpr std::size_t kNoGroup = std::numeric_limits<std::size_t>::max();

        std::vector<MemberGroup> memberGroups;
        std::vector<std::size_t> groupIndexByRoot(count, kNoGroup);
        for (std::size_t index = 0; index < count; ++index)
        {
            const std::size_t root = findRoot(index);
            if (groupIndexByRoot[root] == kNoGroup)
            {
                groupIndexByRoot[root] = memberGroups.size();
                memberGroups.push_back(MemberGroup{});
            }
            memberGroups[groupIndexByRoot[root]].members.push_back(index);
        }

        for (MemberGroup &group: memberGroups)
        {
            // 组内按时间倒序，同一时刻保持结果页原有先后
            std::stable_sort(group.members.begin(), group.members.end(),
                             [&results](const std::size_t left, const std::size_t right) { return results[left].capturedAt > results[right].capturedAt; });
            // 组间顺序由组内最新条目在结果页中的位置决定
            group.newestOriginalIndex = group.members.front();
        }

        std::stable_sort(memberGroups.begin(), memberGroups.end(),
                         [](const MemberGroup &left, const MemberGroup &right) { return left.newestOriginalIndex < right.newestOriginalIndex; });

        std::vector<std::vector<MemoryResult>> groups;
        groups.reserve(memberGroups.size());
        for (MemberGroup &group: memberGroups)
        {
            std::vector<MemoryResult> members;
            members.reserve(group.members.size());
            for (const std::size_t index: group.members)
            {
                members.push_back(std::move(results[index]));
            }
            groups.push_back(std::move(members));
        }
        return groups;
    }
} // namespace GyanisScribe::Core
