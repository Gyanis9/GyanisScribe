// 钉住表格检测的简单表识别、复杂表标记、候选选择与退化输入安全

#include "Core/Content/TableDetector.h"
#include "Core/Ocr/OcrTypes.h"

#include <gtest/gtest.h>

using GyanisScribe::Core::detectTable;
using GyanisScribe::Core::kMaxTableColumns;
using GyanisScribe::Core::OcrLine;
using GyanisScribe::Core::OcrResult;
using GyanisScribe::Core::OcrWord;

namespace
{
    OcrLine makeLine(const int y, const QVector<QString> &texts, const int x0 = 10, const int cellWidth = 60, const int gap = 40)
    {
        OcrLine line;
        int     x = x0;
        for (const QString &text: texts)
        {
            const OcrWord word{text, QRect{x, y, cellWidth, 20}};
            line.words.push_back(word);
            line.boundingBox = line.boundingBox.isNull() ? word.boundingBox : line.boundingBox.united(word.boundingBox);
            x += cellWidth + gap;
        }
        return line;
    }

    OcrWord makeWord(const QString &text, const int x, const int y, const int width, const int height = 20)
    {
        return OcrWord{text, QRect{x, y, width, height}};
    }

    OcrLine makeWordsLine(const QVector<OcrWord> &words)
    {
        OcrLine line;
        line.words = words;
        for (const OcrWord &word: words)
        {
            line.boundingBox = line.boundingBox.isNull() ? word.boundingBox : line.boundingBox.united(word.boundingBox);
        }
        return line;
    }

    OcrResult makeResult(const QVector<OcrLine> &lines)
    {
        OcrResult result;
        result.language = QStringLiteral("zh-Hans");
        result.lines    = lines;
        return result;
    }
} // namespace

TEST(TableDetector, FindsSimpleTwoByTwo)
{
    const auto result = makeResult({makeLine(10, {QStringLiteral("A"), QStringLiteral("B")}), makeLine(40, {QStringLiteral("1"), QStringLiteral("2")})});
    const auto table  = detectTable(result);

    EXPECT_TRUE(table.found);
    EXPECT_FALSE(table.complex);
    EXPECT_EQ(table.rows, 2);
    EXPECT_EQ(table.columns, 2);
    ASSERT_EQ(table.cells.size(), 2);
    EXPECT_EQ(table.cells.at(0).at(0), QStringLiteral("A"));
    EXPECT_EQ(table.cells.at(1).at(1), QStringLiteral("2"));
    EXPECT_EQ(table.firstLine, 0);
    EXPECT_EQ(table.lastLine, 1);
    EXPECT_FALSE(table.boundingBox.isEmpty());
    EXPECT_GE(table.confidence, 0.8);
}

TEST(TableDetector, FindsThreeByThree)
{
    const auto result = makeResult({makeLine(10, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}),
                                    makeLine(40, {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")}),
                                    makeLine(70, {QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6")})});
    const auto table  = detectTable(result);
    ASSERT_TRUE(table.found);
    EXPECT_FALSE(table.complex);
    EXPECT_EQ(table.rows, 3);
    EXPECT_EQ(table.columns, 3);
    ASSERT_EQ(table.cells.size(), 3);
    EXPECT_EQ(table.cells.at(2).at(2), QStringLiteral("6"));
}

TEST(TableDetector, MarksTwoColumnProseAsComplex)
{
    // 正常行距的两栏正文：每一格都是长句，词框宽度几乎铺满整列；这不是可导出的简单表
    QVector<OcrLine> lines;
    for (int row = 0; row < 4; ++row)
    {
        const int y = 10 + row * 30;
        lines.push_back(makeWordsLine({makeWord(QStringLiteral("left"), 10, y, 80), makeWord(QStringLiteral("right"), 130, y, 80)}));
    }
    const auto table = detectTable(makeResult(lines));

    ASSERT_TRUE(table.found);
    EXPECT_TRUE(table.complex) << "两栏正文被判成了可导出简单表";
    EXPECT_EQ(table.columns, 2);
    EXPECT_TRUE(table.complexReason.contains(QStringLiteral("两栏正文"))) << table.complexReason.toStdString();
}

TEST(TableDetector, ShortCellsInTwoColumnsStaySimple)
{
    // 同几何、每格只有短文本（宽度不到列宽 40%）：这是紧凑的两列表格，不许被两栏正文守卫误伤
    QVector<OcrLine> lines;
    for (int row = 0; row < 4; ++row)
    {
        const int y = 10 + row * 30;
        lines.push_back(makeWordsLine({makeWord(QStringLiteral("A"), 10, y, 30), makeWord(QStringLiteral("B"), 130, y, 30)}));
    }
    const auto table = detectTable(makeResult(lines));

    ASSERT_TRUE(table.found);
    EXPECT_FALSE(table.complex) << "短文本两列表格被两栏正文守卫误判成复杂表";
    EXPECT_EQ(table.rows, 4);
    EXPECT_EQ(table.columns, 2);
}

TEST(TableDetector, ShortAndLongCellsOnOneBoundaryStayOneTable)
{
    // 同一列第一行短文本、第二行长文本：两处留白区间重叠，中点却相差 70 格；
    // 旧上限把两行拆成各自孤立的单行证据，表直接丢了
    const auto first  = makeWordsLine({makeWord(QStringLiteral("A"), 10, 10, 30), makeWord(QStringLiteral("B"), 150, 10, 30)});
    const auto second = makeWordsLine({makeWord(QStringLiteral("longlong"), 10, 40, 120), makeWord(QStringLiteral("tail"), 200, 40, 120)});
    const auto table  = detectTable(makeResult({first, second}));

    ASSERT_TRUE(table.found) << "短/长文本的同一处列分界被拆开，两行表丢了";
    EXPECT_FALSE(table.complex);
    EXPECT_EQ(table.rows, 2);
    EXPECT_EQ(table.columns, 2);
    EXPECT_EQ(table.cells.at(0).at(1), QStringLiteral("B"));
    EXPECT_EQ(table.cells.at(1).at(1), QStringLiteral("tail"));
}

TEST(TableDetector, GroupsWordsInsideOneCell)
{
    OcrLine first      = makeLine(10, {});
    OcrLine second     = makeLine(10, {});
    first.words        = {OcrWord{QStringLiteral("Hello"), QRect{10, 10, 50, 20}}, OcrWord{QStringLiteral("World"), QRect{65, 10, 50, 20}},
                          OcrWord{QStringLiteral("B"), QRect{180, 10, 60, 20}}};
    first.boundingBox  = QRect{10, 10, 230, 20};
    second.words       = {OcrWord{QStringLiteral("1"), QRect{10, 40, 60, 20}}, OcrWord{QStringLiteral("2"), QRect{180, 40, 60, 20}}};
    second.boundingBox = QRect{10, 40, 230, 20};
    const auto table   = detectTable(makeResult({first, second}));

    ASSERT_TRUE(table.found);
    EXPECT_FALSE(table.complex);
    EXPECT_EQ(table.columns, 2);
    EXPECT_EQ(table.cells.at(0).at(0), QStringLiteral("Hello World"));
    EXPECT_EQ(table.cells.at(0).at(1), QStringLiteral("B"));
}

TEST(TableDetector, KeepsNarrowFirstColumnBoundary)
{
    const auto first  = makeWordsLine({makeWord(QStringLiteral("1"), 10, 10, 10), makeWord(QStringLiteral("12"), 36, 10, 40), makeWord(QStringLiteral("5"), 96, 10, 20)});
    const auto second = makeWordsLine({makeWord(QStringLiteral("2"), 10, 40, 10), makeWord(QStringLiteral("34"), 36, 40, 40), makeWord(QStringLiteral("6"), 96, 40, 20)});
    const auto table  = detectTable(makeResult({first, second}));

    ASSERT_TRUE(table.found);
    EXPECT_FALSE(table.complex);
    EXPECT_EQ(table.rows, 2);
    ASSERT_EQ(table.columns, 3);
    ASSERT_EQ(table.cells.size(), 2);
    EXPECT_EQ(table.cells.at(0).at(0), QStringLiteral("1"));
    EXPECT_EQ(table.cells.at(0).at(1), QStringLiteral("12"));
    EXPECT_EQ(table.cells.at(0).at(2), QStringLiteral("5"));
}

TEST(TableDetector, RejectsHeadingWithSingleMultiWordLine)
{
    const auto result = makeResult({makeLine(10, {QStringLiteral("Title")}), makeLine(40, {QStringLiteral("A"), QStringLiteral("B")})});
    EXPECT_FALSE(detectTable(result).found);
}

TEST(TableDetector, RejectsMisalignedGaps)
{
    const auto first  = makeWordsLine({makeWord(QStringLiteral("Hi"), 10, 10, 40), makeWord(QStringLiteral("there"), 80, 10, 50)});
    const auto second = makeWordsLine({makeWord(QStringLiteral("Welcome"), 10, 40, 100), makeWord(QStringLiteral("home"), 140, 40, 40)});
    EXPECT_FALSE(detectTable(makeResult({first, second})).found);
}

TEST(TableDetector, RejectsSingleRowOrSingleColumn)
{
    EXPECT_FALSE(detectTable(makeResult({makeLine(10, {QStringLiteral("A"), QStringLiteral("B")})})).found);
    EXPECT_FALSE(detectTable(makeResult({makeLine(10, {QStringLiteral("A")}), makeLine(40, {QStringLiteral("1")})})).found);
}

TEST(TableDetector, MarksCrossColumnTextComplex)
{
    const auto first  = makeWordsLine({makeWord(QStringLiteral("A"), 10, 10, 30), makeWord(QStringLiteral("Wide"), 44, 10, 86), makeWord(QStringLiteral("C"), 160, 10, 30)});
    const auto second = makeWordsLine({makeWord(QStringLiteral("A"), 10, 40, 30), makeWord(QStringLiteral("B"), 70, 40, 60), makeWord(QStringLiteral("C"), 160, 40, 30)});
    const auto third  = makeWordsLine({makeWord(QStringLiteral("A"), 10, 70, 30), makeWord(QStringLiteral("B"), 70, 70, 60), makeWord(QStringLiteral("C"), 160, 70, 30)});
    const auto table  = detectTable(makeResult({first, second, third}));

    ASSERT_TRUE(table.found);
    EXPECT_TRUE(table.complex);
    ASSERT_EQ(table.columns, 3);
    EXPECT_EQ(table.cells.at(0).at(1), QStringLiteral("Wide"));
    EXPECT_TRUE(table.complexReason.contains(QStringLiteral("跨过列边界"))) << table.complexReason.toStdString();
    EXPECT_FALSE(table.complexReason.contains(QStringLiteral("中间列缺少文字"))) << table.complexReason.toStdString();
    EXPECT_FALSE(table.complexReason.contains(QStringLiteral("非空单元格数不一致"))) << table.complexReason.toStdString();
    EXPECT_FALSE(table.complexReason.contains(QStringLiteral("边界与表格列不一致"))) << table.complexReason.toStdString();
}

TEST(TableDetector, MarksInconsistentRowsComplex)
{
    const auto result =
            makeResult({makeLine(10, {QStringLiteral("A"), QStringLiteral("B")}), makeLine(40, {QStringLiteral("1"), QStringLiteral("2")}), makeLine(70, {QStringLiteral("3")})});
    const auto table = detectTable(result);
    ASSERT_TRUE(table.found);
    EXPECT_TRUE(table.complex);
    EXPECT_TRUE(table.complexReason.contains(QStringLiteral("非空单元格数不一致"))) << table.complexReason.toStdString();
}

TEST(TableDetector, MarksMiddleEmptyCellComplex)
{
    const auto first  = makeWordsLine({makeWord(QStringLiteral("A"), 40, 10, 40), makeWord(QStringLiteral("B"), 120, 10, 40), makeWord(QStringLiteral("C"), 200, 10, 40)});
    const auto second = makeWordsLine({makeWord(QStringLiteral("A"), 40, 40, 40), makeWord(QStringLiteral("Wide"), 140, 40, 100)});
    const auto third  = makeWordsLine({makeWord(QStringLiteral("A"), 40, 70, 40), makeWord(QStringLiteral("B"), 120, 70, 40), makeWord(QStringLiteral("C"), 200, 70, 40)});
    const auto fourth = makeWordsLine({makeWord(QStringLiteral("A"), 40, 100, 40), makeWord(QStringLiteral("B"), 120, 100, 40), makeWord(QStringLiteral("C"), 200, 100, 40)});
    const auto table  = detectTable(makeResult({first, second, third, fourth}));

    ASSERT_TRUE(table.found);
    EXPECT_TRUE(table.complex);
    ASSERT_EQ(table.columns, 3);
    EXPECT_TRUE(table.cells.at(1).at(1).isEmpty());
    EXPECT_EQ(table.cells.at(1).at(2), QStringLiteral("Wide"));
    EXPECT_TRUE(table.complexReason.contains(QStringLiteral("中间列缺少文字"))) << table.complexReason.toStdString();
}

TEST(TableDetector, MarksMultiLevelHeaderComplex)
{
    OcrLine header     = makeLine(10, {});
    header.words       = {OcrWord{QStringLiteral("Merged"), QRect{10, 10, 200, 28}}};
    header.boundingBox = QRect{10, 10, 200, 28};
    const auto result  = makeResult({header, makeLine(50, {QStringLiteral("A"), QStringLiteral("B")}), makeLine(80, {QStringLiteral("1"), QStringLiteral("2")})});
    const auto table   = detectTable(result);
    EXPECT_TRUE(table.found);
    EXPECT_TRUE(table.complex);
    EXPECT_TRUE(table.complexReason.contains(QStringLiteral("多级表头"))) << table.complexReason.toStdString();
}

TEST(TableDetector, PrefersSimpleOverComplex)
{
    const auto header = makeWordsLine({makeWord(QStringLiteral("Merged"), 10, 10, 200, 28)});
    const auto result = makeResult({header, makeLine(50, {QStringLiteral("A"), QStringLiteral("B")}), makeLine(80, {QStringLiteral("1"), QStringLiteral("2")}),
                                    makeLine(160, {QStringLiteral("X"), QStringLiteral("Y")}), makeLine(190, {QStringLiteral("3"), QStringLiteral("4")})});
    const auto table  = detectTable(result);
    ASSERT_TRUE(table.found);
    EXPECT_FALSE(table.complex);
    EXPECT_EQ(table.rows, 2);
    EXPECT_EQ(table.columns, 2);
    EXPECT_EQ(table.cells.at(0).at(0), QStringLiteral("X"));
}

TEST(TableDetector, EnforcesColumnLimit)
{
    const auto makeTexts = [](const int count)
    {
        QVector<QString> texts;
        texts.reserve(count);
        for (int index = 0; index < count; ++index)
        {
            texts.push_back(QString::number(index + 1));
        }
        return texts;
    };

    const auto accepted = detectTable(makeResult({makeLine(10, makeTexts(kMaxTableColumns)), makeLine(40, makeTexts(kMaxTableColumns))}));
    ASSERT_TRUE(accepted.found);
    EXPECT_FALSE(accepted.complex);
    EXPECT_EQ(accepted.columns, kMaxTableColumns);

    EXPECT_FALSE(detectTable(makeResult({makeLine(10, makeTexts(kMaxTableColumns + 1)), makeLine(40, makeTexts(kMaxTableColumns + 1))})).found);
}

TEST(TableDetector, KeepsOriginalLineIndicesWhenBoxesAreOutOfOrder)
{
    const auto result = makeResult(
            {makeLine(40, {QStringLiteral("1"), QStringLiteral("2")}), makeLine(10, {QStringLiteral("A"), QStringLiteral("B")}), makeLine(200, {QStringLiteral("End")})});
    const auto table = detectTable(result);
    ASSERT_TRUE(table.found);
    EXPECT_EQ(table.rows, 2);
    EXPECT_EQ(table.firstLine, 0);
    EXPECT_EQ(table.lastLine, 1);
}

TEST(TableDetector, EmptyAndDegenerateInputIsSafe)
{
    EXPECT_FALSE(detectTable(OcrResult{}).found);
    OcrResult emptyLine;
    emptyLine.lines = {OcrLine{}};
    EXPECT_FALSE(detectTable(emptyLine).found);
}

namespace
{
    OcrLine makeCellLine(const int x, const int y, const QString &text, const int width = 40, const int height = 20)
    {
        OcrLine       line;
        const OcrWord word{text, QRect{x, y, width, height}};
        line.words.push_back(word);
        line.boundingBox = word.boundingBox;
        return line;
    }
} // namespace

TEST(TableDetector, MergesCellPerLineColumnMajorIntoSingleTable)
{
    // WinRT 对带框线表格按列优先、每格一行输出：同一视觉行的两格纵带重叠
    const auto result = makeResult({
            makeCellLine(10, 10, QStringLiteral("A")),
            makeCellLine(10, 40, QStringLiteral("1")),
            makeCellLine(100, 10, QStringLiteral("B")),
            makeCellLine(100, 40, QStringLiteral("2")),
            makeCellLine(10, 70, QStringLiteral("3")),
            makeCellLine(100, 70, QStringLiteral("4")),
    });
    const auto table  = detectTable(result);

    ASSERT_TRUE(table.found) << "每格一行的表格没有被认出来";
    EXPECT_FALSE(table.complex);
    EXPECT_EQ(table.rows, 3);
    EXPECT_EQ(table.columns, 2);
    ASSERT_EQ(table.cells.size(), 3);
    EXPECT_EQ(table.cells[0][0], QStringLiteral("A"));
    EXPECT_EQ(table.cells[0][1], QStringLiteral("B"));
    EXPECT_EQ(table.cells[2][0], QStringLiteral("3"));
    EXPECT_EQ(table.cells[2][1], QStringLiteral("4"));
    // 表覆盖的源行是全部 6 条 OCR 行
    EXPECT_EQ(table.firstLine, 0);
    EXPECT_EQ(table.lastLine, 5);
}

TEST(TableDetector, MissingCellDoesNotChainTwoColumnBoundaries)
{
    // 一行缺一格时长留白会同时覆盖相邻两处列分界；串联合并会把两列并成一列（5 列变 4 列）
    QVector<OcrLine> lines;
    const int        columns[] = {37, 165, 293, 421, 548};
    for (int row = 0; row < 4; ++row)
    {
        const int y = 89 + row * 38;
        for (int column = 0; column < 5; ++column)
        {
            if (row == 2 && column == 2)
            {
                continue; // 第三行第三格没有识别出文字
            }
            lines.push_back(makeCellLine(columns[column], y, QStringLiteral("x"), 50));
        }
    }
    const auto table = detectTable(makeResult(lines));

    ASSERT_TRUE(table.found);
    EXPECT_EQ(table.columns, 5) << "缺一格的行把两处列分界串成了一处";
    EXPECT_EQ(table.rows, 4);
}

TEST(TableDetector, CellPaddingRowPitchStillFormsOneTable)
{
    // 真 OCR 的格内文字高 15、行顶间距 38（padding + 边框）：合并后的行间隙 23 比 1.5×15=22.5 还大，
    // 分段上限若只看行高会把表格切成一条条单行段，检测不到表
    const auto result = makeResult({
            makeCellLine(37, 89, QStringLiteral("列1"), 50, 15),  makeCellLine(197, 89, QStringLiteral("列2"), 50, 15),
            makeCellLine(357, 89, QStringLiteral("列3"), 50, 15), makeCellLine(516, 89, QStringLiteral("列4"), 50, 15),
            makeCellLine(37, 127, QStringLiteral("A"), 50, 15),   makeCellLine(197, 127, QStringLiteral("B"), 50, 15),
            makeCellLine(357, 127, QStringLiteral("C"), 50, 15),  makeCellLine(516, 127, QStringLiteral("D"), 50, 15),
            makeCellLine(37, 165, QStringLiteral("1"), 50, 15),   makeCellLine(197, 165, QStringLiteral("2"), 50, 15),
            makeCellLine(357, 165, QStringLiteral("3"), 50, 15),  makeCellLine(516, 165, QStringLiteral("4"), 50, 15),
            makeCellLine(37, 203, QStringLiteral("5"), 50, 15),   makeCellLine(197, 203, QStringLiteral("6"), 50, 15),
            makeCellLine(357, 203, QStringLiteral("7"), 50, 15),  makeCellLine(516, 203, QStringLiteral("8"), 50, 15),
            makeCellLine(37, 241, QStringLiteral("9"), 50, 15),   makeCellLine(197, 241, QStringLiteral("10"), 50, 15),
            makeCellLine(357, 241, QStringLiteral("11"), 50, 15), makeCellLine(516, 241, QStringLiteral("12"), 50, 15),
    });
    const auto table = detectTable(result);

    ASSERT_TRUE(table.found) << "带单元格内边距的行距把表格切成了单行段";
    EXPECT_EQ(table.rows, 5);
    EXPECT_EQ(table.columns, 4);
    EXPECT_EQ(table.cells[1][3], QStringLiteral("D"));
}

TEST(TableDetector, CellPerLineRowMajorMatchesColumnMajor)
{
    const auto result = makeResult({
            makeCellLine(10, 10, QStringLiteral("A")),
            makeCellLine(100, 10, QStringLiteral("B")),
            makeCellLine(10, 40, QStringLiteral("1")),
            makeCellLine(100, 40, QStringLiteral("2")),
            makeCellLine(10, 70, QStringLiteral("3")),
            makeCellLine(100, 70, QStringLiteral("4")),
    });
    const auto table  = detectTable(result);

    ASSERT_TRUE(table.found);
    EXPECT_EQ(table.rows, 3);
    EXPECT_EQ(table.columns, 2);
    EXPECT_EQ(table.cells[1][0], QStringLiteral("1"));
    EXPECT_EQ(table.cells[1][1], QStringLiteral("2"));
}
