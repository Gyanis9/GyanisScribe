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

TEST(MarkdownBuilder, WrapsTwoIndentedCodeLinesInFence)
{
    OcrResult result;
    result.lines           = {line(QStringLiteral("正文"), 10), line(QStringLiteral("if (x) {"), 40, 20, 80), line(QStringLiteral("  return 1;"), 65, 20, 80)};
    const QString markdown = buildMarkdown(result);
    EXPECT_TRUE(markdown.contains(QStringLiteral("```\nif (x) {\n  return 1;\n```")));
    EXPECT_TRUE(markdown.contains(QStringLiteral("正文")));
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

TEST(MarkdownBuilder, EmptyResultIsEmpty)
{
    EXPECT_TRUE(buildMarkdown(OcrResult{}).isEmpty());
}
