#include "Core/Content/TableDetector.h"

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <utility>

namespace GyanisScribe::Core
{
    namespace
    {
        /// 首行高度超过行高中位数的这个倍数且横跨不足列数时，按多级表头处理
        inline constexpr double kMultiLevelHeaderHeightFactor = 1.3;

        /// 参与检测的一行：只保留有效词，bounds 由词框并集得到
        struct TableRow
        {
            int              lineIndex = -1;
            QVector<OcrWord> words;
            QRect            bounds;
        };

        /// 行内一处留白：gapLeft/gapRight 是左右单元格的边界，position 是留白中点
        struct BoundaryCandidate
        {
            double position = 0.0;
            int    gapLeft  = 0;
            int    gapRight = 0;
            int    rowIndex = 0;
        };

        /// 多行留白聚类后的一处列分界
        struct BoundaryCluster
        {
            double       positionSum    = 0.0;
            int          candidateCount = 0;
            int          gapLeft        = 0;
            int          gapRight       = 0;
            QVector<int> rows;

            [[nodiscard]] double position() const
            {
                return candidateCount > 0 ? positionSum / candidateCount : 0.0;
            }
        };

        /// 通过列分界与列数校验的一张候选表
        struct TableCandidate
        {
            bool                      complex = false;
            int                       rows    = 0;
            int                       columns = 0;
            QVector<QVector<QString>> cells;
            QRect                     boundingBox;
            int                       firstLine  = 0;
            int                       lastLine   = -1;
            double                    confidence = 0.0;
            QString                   complexReason;
        };

        /// 从整份结果算出的版面基准值
        struct BodyMetrics
        {
            double medianLineHeight = 1.0;
            double medianWordHeight = 1.0;
            double medianCharWidth  = 1.0;
            int    bodyLeft         = 0;
        };

        [[nodiscard]] bool isUsableWord(const OcrWord &word)
        {
            return !word.text.trimmed().isEmpty() && word.boundingBox.width() > 0 && word.boundingBox.height() > 0;
        }

        [[nodiscard]] int horizontalRight(const QRect &rect)
        {
            // QRect::right() 是闭区间坐标（left + width - 1），算留白时要用开区间边界
            return rect.left() + rect.width();
        }

        [[nodiscard]] int verticalBottom(const QRect &rect)
        {
            return rect.top() + rect.height();
        }

        [[nodiscard]] QRect unionOfWords(const QVector<OcrWord> &words)
        {
            QRect bounds;
            for (const OcrWord &word: words)
            {
                bounds = bounds.isNull() ? word.boundingBox : bounds.united(word.boundingBox);
            }
            return bounds;
        }

        [[nodiscard]] int medianOf(QVector<int> values)
        {
            if (values.isEmpty())
            {
                return 0;
            }
            std::sort(values.begin(), values.end());
            const qsizetype middle = values.size() / 2;
            if (values.size() % 2 != 0)
            {
                return values.at(middle);
            }
            return (values.at(middle - 1) + values.at(middle)) / 2;
        }

        [[nodiscard]] double medianOf(QVector<double> values)
        {
            if (values.isEmpty())
            {
                return 0.0;
            }
            std::sort(values.begin(), values.end());
            const qsizetype middle = values.size() / 2;
            if (values.size() % 2 != 0)
            {
                return values.at(middle);
            }
            return (values.at(middle - 1) + values.at(middle)) / 2.0;
        }

        /// 过滤空行与空词，按阅读顺序返回行，并顺带算出四个版面基准值
        [[nodiscard]] QVector<TableRow> collectRows(const OcrResult &result, BodyMetrics &metrics)
        {
            QVector<TableRow> rows;
            QVector<int>      lineHeights;
            QVector<int>      wordHeights;
            QVector<int>      bodyLefts;
            QVector<double>   characterWidths;

            for (qsizetype lineIndex = 0; lineIndex < result.lines.size(); ++lineIndex)
            {
                const OcrLine &line = result.lines.at(lineIndex);

                QVector<OcrWord> words;
                words.reserve(line.words.size());
                for (const OcrWord &word: line.words)
                {
                    if (!isUsableWord(word))
                    {
                        continue;
                    }
                    OcrWord cleaned = word;
                    cleaned.text    = word.text.trimmed();
                    words.push_back(std::move(cleaned));
                }
                if (words.isEmpty())
                {
                    continue;
                }

                std::sort(words.begin(), words.end(),
                          [](const OcrWord &left, const OcrWord &right)
                          {
                              if (left.boundingBox.left() != right.boundingBox.left())
                              {
                                  return left.boundingBox.left() < right.boundingBox.left();
                              }
                              return left.boundingBox.top() < right.boundingBox.top();
                          });

                TableRow row;
                row.lineIndex = static_cast<int>(lineIndex);
                row.words     = std::move(words);
                row.bounds    = unionOfWords(row.words);
                lineHeights.push_back(row.bounds.height());
                bodyLefts.push_back(row.bounds.left());
                for (const OcrWord &word: row.words)
                {
                    wordHeights.push_back(word.boundingBox.height());
                    // 单字符宽度用词宽除以字符数估算；文本长度已保证至少为 1
                    const int characterCount = std::max(1, static_cast<int>(word.text.size()));
                    characterWidths.push_back(static_cast<double>(word.boundingBox.width()) / static_cast<double>(characterCount));
                }
                rows.push_back(std::move(row));
            }

            // 识别结果通常已按阅读顺序给出，这里仍按坐标排序，避免个别引擎行序抖动
            std::stable_sort(rows.begin(), rows.end(),
                             [](const TableRow &left, const TableRow &right)
                             {
                                 if (left.bounds.top() != right.bounds.top())
                                 {
                                     return left.bounds.top() < right.bounds.top();
                                 }
                                 return left.bounds.left() < right.bounds.left();
                             });

            // 中位数可能因退化输入为 0，统一抬到 1，后续阈值与除法不至于失效
            metrics.medianLineHeight = static_cast<double>(std::max(1, medianOf(lineHeights)));
            metrics.medianWordHeight = static_cast<double>(std::max(1, medianOf(wordHeights)));
            metrics.medianCharWidth  = std::max(1.0, medianOf(characterWidths));
            metrics.bodyLeft         = medianOf(bodyLefts);
            return rows;
        }

        [[nodiscard]] bool rangesOverlapHorizontally(const QRect &left, const QRect &right)
        {
            return left.left() < horizontalRight(right) && right.left() < horizontalRight(left);
        }

        [[nodiscard]] bool isSameTableBlock(const TableRow &previous, const TableRow &current, const double verticalLimit)
        {
            const int verticalGap = current.bounds.top() - verticalBottom(previous.bounds);
            return static_cast<double>(verticalGap) <= verticalLimit && rangesOverlapHorizontally(previous.bounds, current.bounds);
        }

        /// 按纵向间距与横向重叠把相邻行并成候选行段
        [[nodiscard]] QVector<QVector<TableRow>> collectSegments(const QVector<TableRow> &rows, const double verticalLimit)
        {
            QVector<QVector<TableRow>> segments;
            qsizetype                  segmentStart = 0;
            for (qsizetype index = 1; index < rows.size(); ++index)
            {
                if (isSameTableBlock(rows.at(index - 1), rows.at(index), verticalLimit))
                {
                    continue;
                }
                segments.push_back(rows.mid(segmentStart, index - segmentStart));
                segmentStart = index;
            }
            if (segmentStart < rows.size())
            {
                segments.push_back(rows.mid(segmentStart));
            }
            return segments;
        }

        /// 收集每行单元格之间的留白；单格行不产生分界，但保留在行段里参与复杂表判定
        [[nodiscard]] QVector<BoundaryCandidate> collectBoundaryCandidates(const QVector<TableRow> &rows, const double cellGap, const double boundaryFloor)
        {
            QVector<BoundaryCandidate> candidates;
            for (qsizetype rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
            {
                const QVector<OcrWord> &words = rows.at(rowIndex).words;
                if (words.size() < 2)
                {
                    continue;
                }

                int cellRight = horizontalRight(words.first().boundingBox);
                for (qsizetype wordIndex = 1; wordIndex < words.size(); ++wordIndex)
                {
                    const QRect &wordBounds = words.at(wordIndex).boundingBox;
                    const int    gap        = wordBounds.left() - cellRight;
                    if (static_cast<double>(gap) < cellGap)
                    {
                        // 词间留白不足一个单元格间隙，仍属于同一格
                        cellRight = std::max(cellRight, horizontalRight(wordBounds));
                        continue;
                    }

                    const double position = (static_cast<double>(cellRight) + static_cast<double>(wordBounds.left())) / 2.0;
                    // 正文左边界以左或紧贴左边界的分界是行首缩进噪声，不作为列分界
                    if (position > boundaryFloor)
                    {
                        candidates.push_back(BoundaryCandidate{position, cellRight, wordBounds.left(), static_cast<int>(rowIndex)});
                    }
                    cellRight = horizontalRight(wordBounds);
                }
            }
            return candidates;
        }

        /// 把各行的留白中点聚成列分界，聚类结果记录有多少行支持它
        [[nodiscard]] QVector<BoundaryCluster> clusterBoundaries(const QVector<BoundaryCandidate> &candidates, const double tolerance)
        {
            const qsizetype count = candidates.size();
            QVector<int>    parents(count, 0);
            std::iota(parents.begin(), parents.end(), 0);

            const auto findRoot = [&parents](int index)
            {
                while (parents.at(index) != index)
                {
                    parents[index] = parents.at(parents.at(index));
                    index          = parents.at(index);
                }
                return index;
            };

            for (qsizetype left = 0; left < count; ++left)
            {
                for (qsizetype right = left + 1; right < count; ++right)
                {
                    const BoundaryCandidate &leftCandidate  = candidates.at(left);
                    const BoundaryCandidate &rightCandidate = candidates.at(right);
                    const bool               isNear         = std::abs(leftCandidate.position - rightCandidate.position) <= tolerance;
                    // 两行的留白区间重叠时，中点会因左右单元格宽度不同而错开，仍属同一处列分界
                    const bool isOverlapping = leftCandidate.gapLeft <= rightCandidate.gapRight && rightCandidate.gapLeft <= leftCandidate.gapRight;
                    if (!isNear && !isOverlapping)
                    {
                        continue;
                    }
                    const int leftRoot  = findRoot(static_cast<int>(left));
                    const int rightRoot = findRoot(static_cast<int>(right));
                    if (leftRoot != rightRoot)
                    {
                        parents[rightRoot] = leftRoot;
                    }
                }
            }

            QVector<BoundaryCluster> clusters;
            QVector<int>             clusterIndexByRoot(count, -1);
            for (qsizetype index = 0; index < count; ++index)
            {
                const int root = findRoot(static_cast<int>(index));
                if (clusterIndexByRoot[root] < 0)
                {
                    clusterIndexByRoot[root] = static_cast<int>(clusters.size());
                    clusters.push_back(BoundaryCluster{});
                }
                BoundaryCluster         &cluster   = clusters[clusterIndexByRoot[root]];
                const BoundaryCandidate &candidate = candidates.at(index);
                if (cluster.candidateCount == 0)
                {
                    cluster.gapLeft  = candidate.gapLeft;
                    cluster.gapRight = candidate.gapRight;
                } else
                {
                    cluster.gapLeft  = std::min(cluster.gapLeft, candidate.gapLeft);
                    cluster.gapRight = std::max(cluster.gapRight, candidate.gapRight);
                }
                cluster.positionSum += candidate.position;
                ++cluster.candidateCount;
                if (!cluster.rows.contains(candidate.rowIndex))
                {
                    cluster.rows.push_back(candidate.rowIndex);
                }
            }
            return clusters;
        }

        /// 过滤支持行数不足的分界；全都不足时退回支持最多的那些并标记复杂
        [[nodiscard]] QVector<BoundaryCluster> selectSupportedClusters(const QVector<BoundaryCluster> &clusters, const int requiredSupport, bool &usedFallback)
        {
            QVector<BoundaryCluster> supported;
            int                      maximumSupport = 0;
            for (const BoundaryCluster &cluster: clusters)
            {
                maximumSupport = std::max(maximumSupport, static_cast<int>(cluster.rows.size()));
            }
            for (const BoundaryCluster &cluster: clusters)
            {
                if (static_cast<int>(cluster.rows.size()) >= requiredSupport)
                {
                    supported.push_back(cluster);
                }
            }

            usedFallback = supported.isEmpty() && maximumSupport > 0;
            if (usedFallback)
            {
                for (const BoundaryCluster &cluster: clusters)
                {
                    if (static_cast<int>(cluster.rows.size()) == maximumSupport)
                    {
                        supported.push_back(cluster);
                    }
                }
            }
            return supported;
        }

        /// 按选定的列分界把词落格，并判定复杂条件与置信度
        [[nodiscard]] std::optional<TableCandidate> buildCandidate(const QVector<TableRow> &rows, const BodyMetrics &metrics)
        {
            const double cellGap       = std::max(2.0, kTableCellGapFactor * metrics.medianWordHeight);
            const double boundaryFloor = static_cast<double>(metrics.bodyLeft) + std::max(cellGap, metrics.medianCharWidth);
            const double tolerance     = kTableBoundaryTolerance * metrics.medianWordHeight;

            const QVector<BoundaryCandidate> candidates = collectBoundaryCandidates(rows, cellGap, boundaryFloor);
            if (candidates.isEmpty())
            {
                return std::nullopt;
            }

            QVector<BoundaryCluster> clusters = clusterBoundaries(candidates, tolerance);

            // 单格行（合并表头或合并单元格）不能产生分界，因此不参与支持率的分母
            int boundaryRowCount = 0;
            for (const TableRow &row: rows)
            {
                if (row.words.size() >= 2)
                {
                    ++boundaryRowCount;
                }
            }
            const int requiredSupport = std::max(1, static_cast<int>(std::ceil(kTableBoundarySupport * static_cast<double>(boundaryRowCount))));

            bool                     usedFallback = false;
            QVector<BoundaryCluster> supported    = selectSupportedClusters(clusters, requiredSupport, usedFallback);
            if (supported.isEmpty())
            {
                return std::nullopt;
            }
            std::sort(supported.begin(), supported.end(), [](const BoundaryCluster &left, const BoundaryCluster &right) { return left.position() < right.position(); });

            const int columns = static_cast<int>(supported.size()) + 1;
            if (columns < kMinTableColumns || columns > kMaxTableColumns)
            {
                return std::nullopt;
            }

            QVector<double> separators;
            separators.reserve(supported.size());
            for (const BoundaryCluster &cluster: supported)
            {
                separators.push_back(cluster.position());
            }

            const qsizetype           rowCount = rows.size();
            QVector<QVector<QString>> cells(rowCount, QVector<QString>(columns));
            QVector<int>              nonEmptyCounts(rowCount, 0);
            for (qsizetype rowIndex = 0; rowIndex < rowCount; ++rowIndex)
            {
                QVector<QVector<OcrWord>> wordsByColumn(columns);
                for (const OcrWord &word: rows.at(rowIndex).words)
                {
                    const double center    = static_cast<double>(word.boundingBox.center().x());
                    const auto   separator = std::upper_bound(separators.cbegin(), separators.cend(), center);
                    int          column    = static_cast<int>(separator - separators.cbegin());
                    column                 = std::clamp(column, 0, columns - 1);
                    wordsByColumn[column].push_back(word);
                }

                for (int column = 0; column < columns; ++column)
                {
                    QVector<OcrWord> &cellWords = wordsByColumn[column];
                    if (cellWords.isEmpty())
                    {
                        continue;
                    }
                    std::sort(cellWords.begin(), cellWords.end(), [](const OcrWord &left, const OcrWord &right) { return left.boundingBox.left() < right.boundingBox.left(); });
                    QStringList parts;
                    parts.reserve(cellWords.size());
                    for (const OcrWord &word: cellWords)
                    {
                        parts.append(word.text);
                    }
                    cells[rowIndex][column] = parts.join(QLatin1Char(' '));
                    ++nonEmptyCounts[rowIndex];
                }
            }

            QStringList reasons;
            if (usedFallback)
            {
                reasons.append(QStringLiteral("列边界的支持行数不足"));
            }

            bool crossesColumnBoundary = false;
            for (const TableRow &row: rows)
            {
                for (const OcrWord &word: row.words)
                {
                    const double wordLeft  = static_cast<double>(word.boundingBox.left());
                    const double wordRight = static_cast<double>(horizontalRight(word.boundingBox));
                    for (const double separator: separators)
                    {
                        if (wordLeft < separator - tolerance && wordRight > separator + tolerance)
                        {
                            crossesColumnBoundary = true;
                            break;
                        }
                    }
                    if (crossesColumnBoundary)
                    {
                        break;
                    }
                }
                if (crossesColumnBoundary)
                {
                    break;
                }
            }
            if (crossesColumnBoundary)
            {
                reasons.append(QStringLiteral("表格中有文字跨过列边界"));
            }

            bool hasMiddleEmptyCell = false;
            for (qsizetype rowIndex = 0; rowIndex < rowCount && !hasMiddleEmptyCell; ++rowIndex)
            {
                qsizetype firstNonEmpty = -1;
                qsizetype lastNonEmpty  = -1;
                for (int column = 0; column < columns; ++column)
                {
                    if (cells.at(rowIndex).at(column).isEmpty())
                    {
                        continue;
                    }
                    if (firstNonEmpty < 0)
                    {
                        firstNonEmpty = column;
                    }
                    lastNonEmpty = column;
                }
                for (qsizetype column = firstNonEmpty + 1; column < lastNonEmpty; ++column)
                {
                    if (cells.at(rowIndex).at(column).isEmpty())
                    {
                        hasMiddleEmptyCell = true;
                        break;
                    }
                }
            }
            if (hasMiddleEmptyCell)
            {
                reasons.append(QStringLiteral("某行中间列缺少文字"));
            }

            bool hasInconsistentCounts = false;
            for (const int count: nonEmptyCounts)
            {
                if (count != nonEmptyCounts.first())
                {
                    hasInconsistentCounts = true;
                    break;
                }
            }
            if (hasInconsistentCounts)
            {
                reasons.append(QStringLiteral("各行非空单元格数不一致"));
            }

            // 首行又高又少格，通常是横跨多列的多级表头
            const double firstRowHeight = static_cast<double>(rows.first().bounds.height());
            if (nonEmptyCounts.first() < columns && firstRowHeight > kMultiLevelHeaderHeightFactor * metrics.medianLineHeight)
            {
                reasons.append(QStringLiteral("首行高度异常，疑似多级表头"));
            }

            bool hasBoundaryMismatch = false;
            for (const BoundaryCandidate &candidate: candidates)
            {
                bool matchesColumn = false;
                for (const double separator: separators)
                {
                    const bool isNear      = std::abs(candidate.position - separator) <= tolerance;
                    const bool isContained = candidate.gapLeft <= separator && candidate.gapRight >= separator;
                    if (isNear || isContained)
                    {
                        matchesColumn = true;
                        break;
                    }
                }
                if (!matchesColumn)
                {
                    hasBoundaryMismatch = true;
                    break;
                }
            }
            if (hasBoundaryMismatch)
            {
                reasons.append(QStringLiteral("行内单元格边界与表格列不一致"));
            }

            int completeRows = 0;
            for (const int count: nonEmptyCounts)
            {
                if (count == columns)
                {
                    ++completeRows;
                }
            }
            const double confidence = static_cast<double>(completeRows) / static_cast<double>(rowCount);

            bool complex = !reasons.isEmpty();
            // 不复杂但完整度不够时同样不能导出 CSV，归入复杂表说明原因
            if (!complex && confidence < kTableSimpleConfidence)
            {
                complex = true;
                reasons.append(QStringLiteral("表格结构不完整，无法按列还原"));
            }

            TableCandidate candidate;
            candidate.complex       = complex;
            candidate.rows          = static_cast<int>(rowCount);
            candidate.columns       = columns;
            candidate.cells         = std::move(cells);
            candidate.firstLine     = rows.first().lineIndex;
            candidate.lastLine      = rows.last().lineIndex;
            candidate.confidence    = confidence;
            candidate.complexReason = reasons.join(QStringLiteral("；"));
            for (const TableRow &row: rows)
            {
                candidate.boundingBox = candidate.boundingBox.isNull() ? row.bounds : candidate.boundingBox.united(row.bounds);
            }
            return candidate;
        }

        [[nodiscard]] TableDetection toDetection(TableCandidate candidate)
        {
            TableDetection detection;
            detection.found         = true;
            detection.complex       = candidate.complex;
            detection.rows          = candidate.rows;
            detection.columns       = candidate.columns;
            detection.cells         = std::move(candidate.cells);
            detection.boundingBox   = candidate.boundingBox;
            detection.firstLine     = candidate.firstLine;
            detection.lastLine      = candidate.lastLine;
            detection.confidence    = candidate.confidence;
            detection.complexReason = std::move(candidate.complexReason);
            return detection;
        }
    } // namespace

    TableDetection detectTable(const OcrResult &result)
    {
        BodyMetrics             metrics;
        const QVector<TableRow> rows = collectRows(result, metrics);
        if (rows.size() < kMinTableRows)
        {
            return {};
        }

        const double                     verticalLimit = kTableVerticalGapFactor * metrics.medianLineHeight;
        const QVector<QVector<TableRow>> segments      = collectSegments(rows, verticalLimit);

        std::optional<TableCandidate> bestSimple;
        std::optional<TableCandidate> bestComplex;
        for (const QVector<TableRow> &segment: segments)
        {
            if (segment.size() < kMinTableRows)
            {
                continue;
            }
            std::optional<TableCandidate> candidate = buildCandidate(segment, metrics);
            if (!candidate)
            {
                continue;
            }

            const int area = candidate->rows * candidate->columns;
            if (!candidate->complex && candidate->confidence >= kTableSimpleConfidence)
            {
                // 多个简单表并存时取面积最大的那张；同面积保留先出现的
                if (!bestSimple || area > bestSimple->rows * bestSimple->columns)
                {
                    bestSimple = std::move(candidate);
                }
                continue;
            }
            // 没有简单表时，复杂表只用来向界面说明「检测到复杂表格」
            if (!bestComplex || area > bestComplex->rows * bestComplex->columns)
            {
                bestComplex = std::move(candidate);
            }
        }

        if (bestSimple)
        {
            return toDetection(std::move(*bestSimple));
        }
        if (bestComplex)
        {
            return toDetection(std::move(*bestComplex));
        }
        return {};
    }
} // namespace GyanisScribe::Core
