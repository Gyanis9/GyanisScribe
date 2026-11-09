// 钉住 OCR 结果转 Markdown 的阅读顺序、标题、列表、代码、表格与段落规则

#include "Core/Content/MarkdownBuilder.h"

#include <gtest/gtest.h>

using GyanisScribe::Core::buildMarkdown;
using GyanisScribe::Core::OcrLine;
using GyanisScribe::Core::OcrResult;
using GyanisScribe::Core::OcrWord;
using GyanisScribe::Core::TableDetection;

namespace
{
    OcrLine line(const QString &text, const int top, const int height = 20, const int left = 10)
    {
        OcrLine line;
        line.words       = {OcrWord{text, QRect{left, top, qMax(10, static_cast<int>(text.size()) * 10), height}}};
        line.boundingBox = line.words.front().boundingBox;
        return line;
    }

    /// 造一行由多个词框组成的文本：词文本本身不带前导空格，缩进只体现在框的位置上
    OcrLine wordBoxesLine(const int top, const int left, const QVector<QString> &texts)
    {
        OcrLine line;
        int     x = left;
        for (const QString &text: texts)
        {
            const OcrWord word{text, QRect{x, top, static_cast<int>(text.size()) * 10, 20}};
            line.words.push_back(word);
            line.boundingBox = line.boundingBox.isNull() ? word.boundingBox : line.boundingBox.united(word.boundingBox);
            x += static_cast<int>(text.size()) * 10 + 10;
        }
        return line;
    }
} // namespace

TEST(MarkdownBuilder, LevelsHeadingsByLineHeight)
{
    OcrResult result;
    result.lines = {line(QStringLiteral("主标题"), 10, 40), line(QStringLiteral("二级"), 60, 30), line(QStringLiteral("正文内容"), 100, 20)};
    EXPECT_EQ(buildMarkdown(result), QStringLiteral("# 主标题\n## 二级\n正文内容\n"));
}

TEST(MarkdownBuilder, BuildsNestedBulletAndOrderedLists)
{
    OcrResult result;
    result.lines           = {line(QStringLiteral("• 第一项"), 10), line(QStringLiteral("  • 子项"), 35, 20, 30), line(QStringLiteral("1. 第一步"), 60)};
    const QString markdown = buildMarkdown(result);
    EXPECT_TRUE(markdown.contains(QStringLiteral("- 第一项")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("  - 子项")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("1. 第一步")));
}

TEST(MarkdownBuilder, SeparatesListFromFollowingParagraph)
{
    OcrResult result;
    result.lines = {line(QStringLiteral("- 第一项"), 10), line(QStringLiteral("说明文字"), 35)};
    EXPECT_EQ(buildMarkdown(result), QStringLiteral("- 第一项\n\n说明文字\n"));
}

TEST(MarkdownBuilder, WrapsTwoIndentedCodeLinesInFence)
{
    OcrResult result;
    result.lines           = {line(QStringLiteral("正文"), 10), line(QStringLiteral("if (x) {"), 40, 20, 80), line(QStringLiteral("return 1;"), 65, 20, 80)};
    const QString markdown = buildMarkdown(result);
    EXPECT_TRUE(markdown.contains(QStringLiteral("```\nif (x) {\nreturn 1;\n```")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("正文")));
}

TEST(MarkdownBuilder, WrapsCodeLinesInFenceExactly)
{
    OcrResult result;
    result.lines = {line(QStringLiteral("正文"), 10), line(QStringLiteral("if (x) {"), 40, 20, 80), line(QStringLiteral("return 1;"), 65, 20, 80),
                    line(QStringLiteral("}"), 90, 20, 80), line(QStringLiteral("if (y) {"), 115, 20, 80)};
    EXPECT_EQ(buildMarkdown(result), QStringLiteral("正文\n```\nif (x) {\nreturn 1;\n}\nif (y) {\n```\n"));
}

TEST(MarkdownBuilder, FencesFlatCodeRunFromWordBoxesWithoutTextIndent)
{
    // 真实 OCR 的词文本不带前导空格，缩进只体现在词框位置：平铺缩进的连续行照样成块
    OcrResult result;
    result.lines           = {line(QStringLiteral("正文"), 10), wordBoxesLine(40, 80, {QStringLiteral("if"), QStringLiteral("(x)"), QStringLiteral("{")}),
                              wordBoxesLine(65, 80, {QStringLiteral("return"), QStringLiteral("1;")})};
    const QString markdown = buildMarkdown(result);
    EXPECT_TRUE(markdown.contains(QStringLiteral("```\nif (x) {\nreturn 1;\n```"))) << markdown.toStdString();
}

TEST(MarkdownBuilder, DoesNotFenceSingleIndentedLine)
{
    OcrResult result;
    result.lines           = {line(QStringLiteral("正文"), 10), line(QStringLiteral("if (x) {"), 40, 20, 80), line(QStringLiteral("继续正文"), 70)};
    const QString markdown = buildMarkdown(result);
    EXPECT_FALSE(markdown.contains(QStringLiteral("```")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("if (x) {")));
}

TEST(MarkdownBuilder, ReplacesSimpleTableWithMarkdownTable)
{
    OcrResult result;
    result.lines = {line(QStringLiteral("A"), 10), line(QStringLiteral("1"), 40)};
    TableDetection table;
    table.found     = true;
    table.rows      = 2;
    table.columns   = 2;
    table.cells     = {{QStringLiteral("A"), QStringLiteral("B")}, {QStringLiteral("1"), QStringLiteral("2")}};
    table.firstLine = 0;
    table.lastLine  = 1;

    const QString markdown = buildMarkdown(result, table);
    EXPECT_TRUE(markdown.contains(QStringLiteral("| A | B |")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("| --- | --- |")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("| 1 | 2 |")));
}

TEST(MarkdownBuilder, BuildsSimpleTableExactly)
{
    OcrResult result;
    result.lines = {line(QStringLiteral("A"), 10), line(QStringLiteral("1"), 40)};
    TableDetection table;
    table.found     = true;
    table.rows      = 2;
    table.columns   = 2;
    table.cells     = {{QStringLiteral("A"), QStringLiteral("B")}, {QStringLiteral("1"), QStringLiteral("2")}};
    table.firstLine = 0;
    table.lastLine  = 1;

    EXPECT_EQ(buildMarkdown(result, table), QStringLiteral("| A | B |\n| --- | --- |\n| 1 | 2 |\n"));
}

TEST(MarkdownBuilder, EscapesPipeInsideTableCells)
{
    OcrResult result;
    result.lines = {line(QStringLiteral("A|B"), 10), line(QStringLiteral("1"), 40)};
    TableDetection table;
    table.found     = true;
    table.rows      = 2;
    table.columns   = 2;
    table.cells     = {{QStringLiteral("A|B"), QStringLiteral("B")}, {QStringLiteral("1"), QStringLiteral("2")}};
    table.firstLine = 0;
    table.lastLine  = 1;

    const QString markdown = buildMarkdown(result, table);
    EXPECT_TRUE(markdown.contains(QStringLiteral("A\\|B")));
}

TEST(MarkdownBuilder, IgnoresTableCellsWhenTableNotFound)
{
    OcrResult result;
    result.lines = {line(QStringLiteral("A"), 10), line(QStringLiteral("1"), 40)};
    TableDetection table;
    table.cells     = {{QStringLiteral("X"), QStringLiteral("Y")}, {QStringLiteral("3"), QStringLiteral("4")}};
    table.firstLine = 0;
    table.lastLine  = 1;

    const QString markdown = buildMarkdown(result, table);
    EXPECT_FALSE(markdown.contains(QStringLiteral("| --- |")));
    EXPECT_FALSE(markdown.contains(QStringLiteral("X")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("A")));
}

TEST(MarkdownBuilder, ComplexTableFallsBackToText)
{
    OcrResult result;
    result.lines = {line(QStringLiteral("A"), 10), line(QStringLiteral("1"), 40)};
    TableDetection table;
    table.found     = true;
    table.complex   = true;
    table.firstLine = 0;
    table.lastLine  = 1;

    const QString markdown = buildMarkdown(result, table);
    EXPECT_FALSE(markdown.contains(QStringLiteral("| --- |")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("A")));
}

TEST(MarkdownBuilder, KeepsUnrelatedParagraphInsideTableIndexRange)
{
    // OCR 行序抖动时，无关段落可能落在表格首末下标之间；按外接框判定才不会把它一起替换掉
    OcrResult result;
    result.lines = {line(QStringLiteral("A"), 100), line(QStringLiteral("说明文字"), 500), line(QStringLiteral("1"), 130)};
    TableDetection table;
    table.found       = true;
    table.rows        = 2;
    table.columns     = 2;
    table.cells       = {{QStringLiteral("A"), QStringLiteral("B")}, {QStringLiteral("1"), QStringLiteral("2")}};
    table.firstLine   = 0;
    table.lastLine    = 2;
    table.boundingBox = QRect{10, 100, 100, 50};

    const QString markdown = buildMarkdown(result, table);
    EXPECT_TRUE(markdown.contains(QStringLiteral("| 1 | 2 |"))) << markdown.toStdString();
    EXPECT_TRUE(markdown.contains(QStringLiteral("说明文字"))) << markdown.toStdString();
}

TEST(MarkdownBuilder, EscapesParagraphLineStartsAndEndsWithOneNewline)
{
    OcrResult result;
    result.lines           = {line(QStringLiteral("# 不是标题，只是一段很长的正文。"), 10), line(QStringLiteral("> 引用样式"), 35)};
    const QString markdown = buildMarkdown(result);
    EXPECT_TRUE(markdown.contains(QStringLiteral("\\# 不是标题")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("\\> 引用样式")));
    EXPECT_TRUE(markdown.endsWith(QLatin1Char('\n')));
    EXPECT_FALSE(markdown.endsWith(QStringLiteral("\n\n")));
}

TEST(MarkdownBuilder, EscapesOnlyTheDotAfterLeadingDigits)
{
    OcrResult result;
    result.lines = {line(QStringLiteral("3.14 是圆周率"), 10)};
    EXPECT_EQ(buildMarkdown(result), QStringLiteral("3\\.14 是圆周率\n"));
}

TEST(MarkdownBuilder, EscapesLeadingEqualsAndTilde)
{
    OcrResult result;
    result.lines = {line(QStringLiteral("= 不是下划线"), 10), line(QStringLiteral("~~~ 不是围栏"), 35)};
    EXPECT_EQ(buildMarkdown(result), QStringLiteral("\\= 不是下划线\n\\~~~ 不是围栏\n"));
}

TEST(MarkdownBuilder, EmptyResultIsEmpty)
{
    EXPECT_TRUE(buildMarkdown(OcrResult{}).isEmpty());
}
