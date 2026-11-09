#include "Core/Content/MarkdownBuilder.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace GyanisScribe::Core
{
    namespace
    {
        /// 行高达到正文行高这几个倍数时分别升为一级、二级、三级标题
        inline constexpr double kHeadingLevelOneFactor   = 1.6;
        inline constexpr double kHeadingLevelTwoFactor   = 1.35;
        inline constexpr double kHeadingLevelThreeFactor = 1.15;

        /// 标题文本长度上限：再长就当成正文，避免把短段落误判成标题
        inline constexpr int kMaximumHeadingLength = 60;

        /// 列表最多缩进四层，再深也只是 OCR 框噪声
        inline constexpr int kMaximumListIndentLevel = 4;

        /// 代码行相对正文左边界的最小缩进，以及同一代码块允许的缩进抖动
        inline constexpr double kCodeIndentFactor          = 1.2;
        inline constexpr double kCodeIndentToleranceFactor = 0.5;

        /// 段落合并允许的纵向间隙，按正文行高折算
        inline constexpr double kParagraphGapFactor = 0.6;

        /// 参与排版的一行：原下标用于替换表格段，bounds 由有效词框并集得到
        struct TextLine
        {
            int     index = 0;
            QRect   bounds;
            QString text;
        };

        /// 从整份结果算出的版面基准值
        struct BodyMetrics
        {
            double bodyHeight      = 1.0;
            int    bodyLeft        = 0;
            double medianCharWidth = 1.0;
        };

        /// 列表行解析出的标记与正文
        struct ListMarker
        {
            bool    ordered = false;
            QString marker;
            QString body;
        };

        /// 一行在本次排版里的分类结果；表格段内的行不参与后续判定
        struct LineInfo
        {
            TextLine                  line;
            QString                   headingPrefix;
            std::optional<ListMarker> list;
            bool                      codeCandidate = false;
            bool                      inTable       = false;
        };

        enum class BlockKind
        {
            Heading,
            List,
            Code,
            Paragraph,
            Table,
        };

        struct Block
        {
            BlockKind kind = BlockKind::Paragraph;
            QString   text;
        };

        [[nodiscard]] bool isUsableWord(const OcrWord &word)
        {
            return !word.text.trimmed().isEmpty() && word.boundingBox.width() > 0 && word.boundingBox.height() > 0;
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

        [[nodiscard]] QString joinWordTexts(const QVector<OcrWord> &words)
        {
            QStringList parts;
            parts.reserve(words.size());
            for (const OcrWord &word: words)
            {
                parts.append(word.text);
            }
            return parts.join(QLatin1Char(' '));
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

        [[nodiscard]] int lowerQuartileOf(QVector<int> values)
        {
            if (values.isEmpty())
            {
                return 0;
            }
            std::sort(values.begin(), values.end());
            return values.at(values.size() / 4);
        }

        /// 过滤空行与空词，按阅读顺序返回行，并顺带算出三个版面基准值
        [[nodiscard]] QVector<TextLine> collectLines(const OcrResult &result, BodyMetrics &metrics)
        {
            QVector<TextLine> lines;
            QVector<int>      lineHeights;
            QVector<int>      lineLefts;
            QVector<double>   characterWidths;

            for (qsizetype lineIndex = 0; lineIndex < result.lines.size(); ++lineIndex)
            {
                const OcrLine &sourceLine = result.lines.at(lineIndex);

                QVector<OcrWord> words;
                words.reserve(sourceLine.words.size());
                for (const OcrWord &word: sourceLine.words)
                {
                    if (!isUsableWord(word))
                    {
                        continue;
                    }
                    words.push_back(word);
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

                TextLine line;
                line.index  = static_cast<int>(lineIndex);
                line.bounds = unionOfWords(words);
                line.text   = joinWordTexts(words);
                lineHeights.push_back(line.bounds.height());
                lineLefts.push_back(line.bounds.left());
                for (const OcrWord &word: words)
                {
                    const QString cleaned = word.text.trimmed();
                    characterWidths.push_back(static_cast<double>(word.boundingBox.width()) / static_cast<double>(std::max(1, static_cast<int>(cleaned.size()))));
                }
                lines.push_back(std::move(line));
            }

            // 识别结果通常已按阅读顺序给出，这里仍按坐标排序，避免个别引擎行序抖动
            std::stable_sort(lines.begin(), lines.end(),
                             [](const TextLine &left, const TextLine &right)
                             {
                                 if (left.bounds.top() != right.bounds.top())
                                 {
                                     return left.bounds.top() < right.bounds.top();
                                 }
                                 return left.bounds.left() < right.bounds.left();
                             });

            if (lines.isEmpty())
            {
                return lines;
            }

            // 正文行高取排序后的下四分位数，避免标题把正文基准抬高。
            // 正文左边界取非空行左界的最小值：最左侧的正文行给出整页的排版起点，
            // 列表层级与代码缩进都以它为基准。代码行可能占多数，用中位数会被推到代码缩进上。
            metrics.bodyHeight      = static_cast<double>(std::max(1, lowerQuartileOf(lineHeights)));
            metrics.bodyLeft        = *std::min_element(lineLefts.begin(), lineLefts.end());
            metrics.medianCharWidth = std::max(1.0, medianOf(characterWidths));
            return lines;
        }

        [[nodiscard]] QString headingPrefixFor(const QString &text, const int height, const BodyMetrics &metrics)
        {
            if (text.isEmpty() || text.size() > kMaximumHeadingLength)
            {
                return {};
            }
            const double lineHeight = static_cast<double>(std::max(1, height));
            if (lineHeight >= kHeadingLevelOneFactor * metrics.bodyHeight)
            {
                return QStringLiteral("#");
            }
            if (lineHeight >= kHeadingLevelTwoFactor * metrics.bodyHeight)
            {
                return QStringLiteral("##");
            }
            if (lineHeight >= kHeadingLevelThreeFactor * metrics.bodyHeight)
            {
                return QStringLiteral("###");
            }
            return {};
        }

        [[nodiscard]] std::optional<ListMarker> parseListMarker(const QString &text)
        {
            static const QRegularExpression pattern{QStringLiteral("^([-*•·▪◦]|\\d+[.)、]|[一二三四五六七八九十百]+[、.])\\s+")};
            const QRegularExpressionMatch   match = pattern.match(text);
            if (!match.hasMatch())
            {
                return std::nullopt;
            }

            ListMarker marker;
            marker.marker     = match.captured(1);
            marker.body       = text.mid(match.capturedEnd(0)).simplified();
            const QChar first = marker.marker.at(0);
            marker.ordered    = !QStringLiteral("-*•·▪◦").contains(first);
            if (marker.ordered)
            {
                // 统一成「编号. 」；1)、1、与中文编号都保留编号本身
                marker.marker.chop(1);
                marker.marker.append(QLatin1Char('.'));
            } else
            {
                marker.marker = QStringLiteral("-");
            }
            return marker;
        }

        [[nodiscard]] QString listBlockText(const TextLine &line, const ListMarker &marker, const BodyMetrics &metrics)
        {
            const double indentLevel = std::round(static_cast<double>(line.bounds.left() - metrics.bodyLeft) / (2.0 * metrics.medianCharWidth));
            const int    level       = std::clamp(static_cast<int>(indentLevel), 0, kMaximumListIndentLevel);

            QString output(level * 2, QLatin1Char(' '));
            output.append(marker.marker);
            if (!marker.body.isEmpty())
            {
                output.append(QStringLiteral(" "));
                output.append(marker.body);
            }
            return output;
        }

        [[nodiscard]] bool containsCodeSymbol(const QString &text)
        {
            const QString symbols = QStringLiteral("{}[]();=<>#");
            for (const QChar character: text)
            {
                if (symbols.contains(character))
                {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool isMostlyAscii(const QString &text)
        {
            qsizetype asciiCount = 0;
            for (const QChar character: text)
            {
                if (character.unicode() < 128)
                {
                    ++asciiCount;
                }
            }
            return asciiCount * 5 >= text.size() * 4;
        }

        [[nodiscard]] bool endsWithSentencePunctuation(const QString &text)
        {
            if (text.isEmpty())
            {
                return false;
            }
            return QStringLiteral("。！？；").contains(text.back());
        }

        [[nodiscard]] bool isCodeCandidate(const TextLine &line, const BodyMetrics &metrics)
        {
            const QString text = line.text.trimmed();
            if (text.isEmpty() || endsWithSentencePunctuation(text))
            {
                return false;
            }
            const double requiredIndent = static_cast<double>(metrics.bodyLeft) + kCodeIndentFactor * metrics.medianCharWidth;
            if (static_cast<double>(line.bounds.left()) < requiredIndent)
            {
                return false;
            }
            return containsCodeSymbol(text) || isMostlyAscii(text);
        }

        [[nodiscard]] QString withoutTrailingWhitespace(QString text)
        {
            while (!text.isEmpty() && text.back().isSpace())
            {
                text.chop(1);
            }
            return text;
        }

        [[nodiscard]] QString escapedParagraphLine(const QString &text)
        {
            const QString trimmed = text.trimmed();
            if (trimmed.isEmpty())
            {
                return {};
            }
            // 行首这几个字符会被 Markdown 当成标题、引用、列表、强调、setext 下划线或代码围栏，
            // 段落里一律转义掉
            if (QStringLiteral("#>-+*\\=~").contains(trimmed.at(0)))
            {
                return QStringLiteral("\\") + trimmed;
            }

            static const QRegularExpression orderedStart{QStringLiteral("^(\\d+)\\.")};
            const QRegularExpressionMatch   match = orderedStart.match(trimmed);
            if (!match.hasMatch())
            {
                return trimmed;
            }
            // 只转义数字后的点本身：插入空格会把 3.14 改写成 3. 14
            const QString digits = match.captured(1);
            return digits + QStringLiteral("\\.") + trimmed.mid(match.capturedEnd(0));
        }

        [[nodiscard]] QString escapeTableCell(const QString &value)
        {
            QString cell = value;
            cell.replace(QLatin1Char('\r'), QLatin1Char(' '));
            cell.replace(QLatin1Char('\n'), QLatin1Char(' '));
            cell.replace(QLatin1Char('|'), QStringLiteral("\\|"));
            return cell.trimmed();
        }

        [[nodiscard]] std::optional<QString> buildTableBlock(const TableDetection &table)
        {
            if (!table.found || table.complex || table.rows <= 0 || table.columns <= 0)
            {
                return std::nullopt;
            }
            if (table.cells.size() != table.rows)
            {
                return std::nullopt;
            }
            for (const QVector<QString> &row: table.cells)
            {
                if (row.size() != table.columns)
                {
                    return std::nullopt;
                }
            }

            QStringList markdownRows;
            QString     header = QStringLiteral("|");
            for (int column = 0; column < table.columns; ++column)
            {
                header.append(QLatin1Char(' '));
                header.append(escapeTableCell(table.cells.at(0).at(column)));
                header.append(QStringLiteral(" |"));
            }
            markdownRows.append(header);

            QString separator = QStringLiteral("|");
            for (int column = 0; column < table.columns; ++column)
            {
                separator.append(QStringLiteral(" --- |"));
            }
            markdownRows.append(separator);

            for (int row = 1; row < table.rows; ++row)
            {
                QString markdownRow = QStringLiteral("|");
                for (int column = 0; column < table.columns; ++column)
                {
                    markdownRow.append(QLatin1Char(' '));
                    markdownRow.append(escapeTableCell(table.cells.at(row).at(column)));
                    markdownRow.append(QStringLiteral(" |"));
                }
                markdownRows.append(markdownRow);
            }
            return markdownRows.join(QStringLiteral("\n"));
        }

        [[nodiscard]] QVector<LineInfo> classifyLines(const QVector<TextLine> &lines, const BodyMetrics &metrics, const TableDetection *table)
        {
            QVector<LineInfo> infos;
            infos.reserve(lines.size());
            for (const TextLine &line: lines)
            {
                LineInfo info;
                info.line    = line;
                info.inTable = table != nullptr && line.index >= table->firstLine && line.index <= table->lastLine;
                if (!info.inTable)
                {
                    const QString text = line.text.trimmed();
                    info.list          = parseListMarker(text);
                    if (!info.list)
                    {
                        info.headingPrefix = headingPrefixFor(text, line.bounds.height(), metrics);
                    }
                    info.codeCandidate = !info.list && info.headingPrefix.isEmpty() && isCodeCandidate(line, metrics);
                }
                infos.push_back(std::move(info));
            }
            return infos;
        }

        [[nodiscard]] qsizetype codeRunLength(const QVector<LineInfo> &infos, const qsizetype start, const BodyMetrics &metrics)
        {
            const double maximumDifference = kCodeIndentToleranceFactor * metrics.medianCharWidth;
            const double firstIndent       = static_cast<double>(infos.at(start).line.bounds.left());
            qsizetype    end               = start;
            while (end < infos.size() && infos.at(end).codeCandidate)
            {
                if (end > start && std::abs(static_cast<double>(infos.at(end).line.bounds.left()) - firstIndent) > maximumDifference)
                {
                    break;
                }
                ++end;
            }
            return end - start;
        }

        [[nodiscard]] QString codeBlockText(const QVector<LineInfo> &infos, const qsizetype start, const qsizetype count)
        {
            QStringList lines;
            lines.append(QStringLiteral("```"));
            for (qsizetype offset = 0; offset < count; ++offset)
            {
                lines.append(withoutTrailingWhitespace(infos.at(start + offset).line.text));
            }
            lines.append(QStringLiteral("```"));
            return lines.join(QStringLiteral("\n"));
        }
    } // namespace

    QString buildMarkdown(const OcrResult &result, const std::optional<TableDetection> &table)
    {
        BodyMetrics             metrics;
        const QVector<TextLine> lines = collectLines(result, metrics);
        if (lines.isEmpty())
        {
            return {};
        }

        const std::optional<QString> tableBlock  = table ? buildTableBlock(*table) : std::nullopt;
        const bool                   tableUsable = tableBlock.has_value() && table->firstLine >= 0 && table->lastLine >= table->firstLine;
        const QVector<LineInfo>      infos       = classifyLines(lines, metrics, tableUsable ? &*table : nullptr);

        QVector<Block> blocks;
        bool           tableEmitted = false;
        qsizetype      index        = 0;
        while (index < infos.size())
        {
            const LineInfo &info = infos.at(index);
            if (info.inTable)
            {
                if (!tableEmitted)
                {
                    blocks.push_back(Block{BlockKind::Table, *tableBlock});
                    tableEmitted = true;
                }
                ++index;
                continue;
            }

            if (!info.headingPrefix.isEmpty())
            {
                blocks.push_back(Block{BlockKind::Heading, info.headingPrefix + QStringLiteral(" ") + info.line.text.trimmed()});
                ++index;
                continue;
            }

            if (info.list)
            {
                blocks.push_back(Block{BlockKind::List, listBlockText(info.line, *info.list, metrics)});
                ++index;
                continue;
            }

            const qsizetype candidateCount = info.codeCandidate ? codeRunLength(infos, index, metrics) : 0;
            if (candidateCount >= 2)
            {
                blocks.push_back(Block{BlockKind::Code, codeBlockText(infos, index, candidateCount)});
                index += candidateCount;
                continue;
            }

            QStringList paragraphLines;
            qsizetype   previousIndex = -1;
            while (index < infos.size())
            {
                const LineInfo &current = infos.at(index);
                if (current.inTable || !current.headingPrefix.isEmpty() || current.list)
                {
                    break;
                }
                if (current.codeCandidate && codeRunLength(infos, index, metrics) >= 2)
                {
                    break;
                }
                if (previousIndex >= 0)
                {
                    const TextLine &previous = infos.at(previousIndex).line;
                    const int       gap      = current.line.bounds.top() - (previous.bounds.top() + previous.bounds.height());
                    if (static_cast<double>(gap) > kParagraphGapFactor * metrics.bodyHeight)
                    {
                        break;
                    }
                }
                paragraphLines.append(escapedParagraphLine(current.line.text));
                previousIndex = index;
                ++index;
            }
            if (!paragraphLines.isEmpty())
            {
                blocks.push_back(Block{BlockKind::Paragraph, paragraphLines.join(QStringLiteral("\n"))});
            }
        }

        if (blocks.isEmpty())
        {
            return {};
        }

        QString markdown;
        for (qsizetype blockIndex = 0; blockIndex < blocks.size(); ++blockIndex)
        {
            if (blockIndex > 0)
            {
                const Block &previous = blocks.at(blockIndex - 1);
                const Block &current  = blocks.at(blockIndex);
                // 列表块后接非列表块也必须空行：紧密跟随的段落会被 CommonMark 并入最后一个列表项
                const bool needsBlankLine = (previous.kind == BlockKind::Paragraph && current.kind == BlockKind::Paragraph) ||
                                            (previous.kind == BlockKind::List && current.kind != BlockKind::List) || previous.kind == BlockKind::Table ||
                                            current.kind == BlockKind::Table;
                markdown.append(needsBlankLine ? QStringLiteral("\n\n") : QStringLiteral("\n"));
            }
            markdown.append(blocks.at(blockIndex).text);
        }
        // 块文本本身不带收尾换行；这里收掉可能的多余空行，保证末尾恰好一个 LF
        while (markdown.endsWith(QLatin1Char('\n')))
        {
            markdown.chop(1);
        }
        markdown.append(QLatin1Char('\n'));
        return markdown;
    }
} // namespace GyanisScribe::Core
