#include "Core/Ocr/PpOcrPostprocess.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace GyanisScribe::Core
{
    namespace
    {
        /// 一个连通域的统计量：外接矩形、像素数与概率总和
        struct ComponentStats
        {
            int    minX  = std::numeric_limits<int>::max();
            int    minY  = std::numeric_limits<int>::max();
            int    maxX  = -1;
            int    maxY  = -1;
            int    count = 0;
            double sum   = 0.0;
        };

        /**
         * @brief 从一个前景像素出发收集整个 4 连通域
         * @param map 概率图
         * @param width 概率图宽
         * @param height 概率图高
         * @param threshold 前景阈值
         * @param startX 起点横坐标
         * @param startY 起点纵坐标
         * @param visited 已访问标记，取值为 1 表示该位置已经归入某个连通域
         * @return 连通域的统计量
         */
        ComponentStats collectComponent(const float *map, const int width, const int height, const float threshold, const int startX, const int startY, std::vector<char> &visited)
        {
            ComponentStats   stats;
            std::vector<int> pending;
            pending.push_back(startY * width + startX);
            visited[static_cast<size_t>(startY) * static_cast<size_t>(width) + static_cast<size_t>(startX)] = 1;

            while (!pending.empty())
            {
                const int index = pending.back();
                pending.pop_back();

                const int x = index % width;
                const int y = index / width;
                ++stats.count;
                stats.sum += static_cast<double>(map[index]);
                stats.minX = std::min(stats.minX, x);
                stats.maxX = std::max(stats.maxX, x);
                stats.minY = std::min(stats.minY, y);
                stats.maxY = std::max(stats.maxY, y);

                constexpr int offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (const auto &offset : offsets)
                {
                    const int neighbourX = x + offset[0];
                    const int neighbourY = y + offset[1];
                    if (neighbourX < 0 || neighbourX >= width || neighbourY < 0 || neighbourY >= height)
                    {
                        continue;
                    }
                    const size_t neighbour = static_cast<size_t>(neighbourY) * static_cast<size_t>(width) + static_cast<size_t>(neighbourX);
                    if (visited[neighbour] != 0 || map[neighbour] < threshold)
                    {
                        continue;
                    }
                    visited[neighbour] = 1;
                    pending.push_back(neighbourY * width + neighbourX);
                }
            }
            return stats;
        }

        /// 两个框的纵向重叠是否超过较矮框高度的一半
        bool overlapsVertically(const QRect &lhs, const QRect &rhs) noexcept
        {
            const int overlap = std::min(lhs.bottom(), rhs.bottom()) - std::max(lhs.top(), rhs.top()) + 1;
            const int shorter = std::min(lhs.height(), rhs.height());
            return overlap * 2 > shorter;
        }
    } // namespace

    QVector<PpOcrTextBox> extractTextBoxes(const float *map, const int width, const int height, const float threshold, const float minArea, const float unclipRatio)
    {
        QVector<PpOcrTextBox> boxes;
        if (map == nullptr || width <= 0 || height <= 0)
        {
            return boxes;
        }

        const size_t              pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        std::vector<char>         visited(pixelCount, 0);
        std::vector<PpOcrTextBox> found;

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const size_t index = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
                if (visited[index] != 0 || map[index] < threshold)
                {
                    continue;
                }

                const ComponentStats stats = collectComponent(map, width, height, threshold, x, y, visited);
                if (static_cast<float>(stats.count) < minArea)
                {
                    continue;
                }
                const auto score = static_cast<float>(stats.sum / static_cast<double>(stats.count));
                if (score < 0.5F)
                {
                    continue;
                }

                const int boxWidth  = stats.maxX - stats.minX + 1;
                const int boxHeight = stats.maxY - stats.minY + 1;
                // 宽或高不足 3 像素的连通域是笔画交点之类的噪声，按规格直接丢掉
                if (boxWidth < 3 || boxHeight < 3)
                {
                    continue;
                }
                const int expand    = std::max(2, static_cast<int>(std::lround(static_cast<double>(unclipRatio) * static_cast<double>(std::min(boxWidth, boxHeight)))));

                const int left   = std::max(0, stats.minX - expand);
                const int top    = std::max(0, stats.minY - expand);
                const int right  = std::min(width - 1, stats.maxX + expand);
                const int bottom = std::min(height - 1, stats.maxY + expand);
                found.push_back({QRect(left, top, right - left + 1, bottom - top + 1), score});
            }
        }

        std::stable_sort(found.begin(), found.end(), [](const PpOcrTextBox &lhs, const PpOcrTextBox &rhs)
        {
            if (lhs.rect.top() != rhs.rect.top())
            {
                return lhs.rect.top() < rhs.rect.top();
            }
            return lhs.rect.left() < rhs.rect.left();
        });

        // 按 top 排好序后逐行聚合：与已入行的任一框纵向重叠超过较矮框一半即视为同一行，行内再按 left 排序
        std::vector<char>   grouped(found.size(), 0);
        std::vector<size_t> row;
        for (size_t i = 0; i < found.size(); ++i)
        {
            if (grouped[i] != 0)
            {
                continue;
            }
            grouped[i] = 1;
            row.assign(1, i);
            for (size_t j = i + 1; j < found.size(); ++j)
            {
                if (grouped[j] != 0)
                {
                    continue;
                }
                const bool sameRow = std::any_of(row.begin(), row.end(), [&found, j](const size_t member)
                {
                    return overlapsVertically(found[member].rect, found[j].rect);
                });
                if (sameRow)
                {
                    grouped[j] = 1;
                    row.push_back(j);
                }
            }
            std::stable_sort(row.begin(), row.end(), [&found](const size_t lhs, const size_t rhs)
            {
                return found[lhs].rect.left() < found[rhs].rect.left();
            });
            for (const size_t member : row)
            {
                boxes.push_back(found[member]);
            }
        }
        return boxes;
    }

    PpOcrDecodedText decodeCtc(const float *logits, const int timeSteps, const int classes, const QStringList &dictionary)
    {
        PpOcrDecodedText decoded;
        if (logits == nullptr || timeSteps <= 0 || classes <= 0)
        {
            return decoded;
        }

        int    previousIndex = -1;
        double confidenceSum = 0.0;
        int    keptSteps     = 0;
        for (int t = 0; t < timeSteps; ++t)
        {
            const float *row  = logits + static_cast<size_t>(t) * static_cast<size_t>(classes);
            int          best = 0;
            for (int c = 1; c < classes; ++c)
            {
                if (row[c] > row[best])
                {
                    best = c;
                }
            }

            // blank 也参与相邻比较：同一字符被 blank 隔开时两次都要输出
            if (best != previousIndex && best != 0)
            {
                QString glyph;
                if (best >= 1 && best <= dictionary.size())
                {
                    glyph = dictionary.at(best - 1);
                }
                else if (best == dictionary.size() + 1)
                {
                    glyph = QStringLiteral(" ");
                }
                if (!glyph.isEmpty())
                {
                    decoded.text += glyph;
                    confidenceSum += static_cast<double>(row[best]);
                    ++keptSteps;
                }
            }
            previousIndex = best;
        }

        if (keptSteps > 0)
        {
            decoded.confidence = confidenceSum / static_cast<double>(keptSteps);
        }
        return decoded;
    }

} // namespace GyanisScribe::Core
