// 钉住表格检测的简单表识别、复杂表标记、候选选择与退化输入安全

#include "Core/Content/TableDetector.h"
#include "Core/Ocr/OcrTypes.h"

#include <gtest/gtest.h>

using GyanisScribe::Core::detectTable;
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

TEST(TableDetector, RejectsSingleRowOrSingleColumn)
{
    EXPECT_FALSE(detectTable(makeResult({makeLine(10, {QStringLiteral("A"), QStringLiteral("B")})})).found);
    EXPECT_FALSE(detectTable(makeResult({makeLine(10, {QStringLiteral("A")}), makeLine(40, {QStringLiteral("1")})})).found);
}

TEST(TableDetector, MarksCrossColumnTextComplex)
{
    OcrLine first     = makeLine(10, {});
    OcrLine second    = makeLine(40, {QStringLiteral("1"), QStringLiteral("2")});
    first.words       = {OcrWord{QStringLiteral("Wide"), QRect{10, 10, 140, 20}}, OcrWord{QStringLiteral("B"), QRect{180, 10, 60, 20}}};
    first.boundingBox = QRect{10, 10, 230, 20};

    const auto table = detectTable(makeResult({first, second}));
    EXPECT_TRUE(table.found);
    EXPECT_TRUE(table.complex);
    EXPECT_FALSE(table.complexReason.isEmpty());
}

TEST(TableDetector, MarksInconsistentRowsComplex)
{
    const auto result = makeResult({makeLine(10, {QStringLiteral("A"), QStringLiteral("B")}), makeLine(40, {QStringLiteral("1")})});
    const auto table  = detectTable(result);
    EXPECT_TRUE(table.found);
    EXPECT_TRUE(table.complex);
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
}

TEST(TableDetector, PrefersSimpleOverComplex)
{
    const auto result = makeResult({makeLine(10, {QStringLiteral("A"), QStringLiteral("B")}), makeLine(40, {QStringLiteral("1")}),
                                    makeLine(120, {QStringLiteral("X"), QStringLiteral("Y")}), makeLine(150, {QStringLiteral("3"), QStringLiteral("4")})});
    const auto table  = detectTable(result);
    ASSERT_TRUE(table.found);
    EXPECT_FALSE(table.complex);
    EXPECT_EQ(table.cells.at(0).at(0), QStringLiteral("X"));
}

TEST(TableDetector, EmptyAndDegenerateInputIsSafe)
{
    EXPECT_FALSE(detectTable(OcrResult{}).found);
    OcrResult emptyLine;
    emptyLine.lines = {OcrLine{}};
    EXPECT_FALSE(detectTable(emptyLine).found);
}
