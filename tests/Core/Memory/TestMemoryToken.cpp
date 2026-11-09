// 钉住 FTS 中文逐字分词与查询串构造：中文短语能命中，运算符与纯标点不会变成 FTS5 语法错误

#include "Core/Memory/MemoryToken.h"

#include <gtest/gtest.h>

using GyanisScribe::Core::buildFtsQuery;
using GyanisScribe::Core::prepareFtsText;

TEST(MemoryToken, SpacesCjkAndTransitionsForIndexing)
{
    EXPECT_EQ(prepareFtsText(QStringLiteral("本周报价表2026")), QStringLiteral("本 周 报 价 表 2026"));
    EXPECT_EQ(prepareFtsText(QStringLiteral("Report 2026")), QStringLiteral("Report 2026"));
    EXPECT_EQ(prepareFtsText(QStringLiteral("Report\t2026")), QStringLiteral("Report 2026"));
    EXPECT_EQ(prepareFtsText(QStringLiteral("  报价\t表  ")), QStringLiteral("报 价 表"));
}

TEST(MemoryToken, BuildsCjkPhrasesAndAsciiPrefixes)
{
    EXPECT_EQ(buildFtsQuery(QStringLiteral("报价")), QStringLiteral("\"报 价\""));
    EXPECT_EQ(buildFtsQuery(QStringLiteral("报价2026")), QStringLiteral("\"报 价 2026\""));
    EXPECT_EQ(buildFtsQuery(QStringLiteral("报价 合同")), QStringLiteral("\"报 价\" \"合 同\""));
    EXPECT_EQ(buildFtsQuery(QStringLiteral("quot")), QStringLiteral("\"quot\"*"));
    EXPECT_EQ(buildFtsQuery(QStringLiteral("a")), QStringLiteral("\"a\""));
}

TEST(MemoryToken, DropsEmptySegmentsAndEscapesQuotes)
{
    EXPECT_TRUE(buildFtsQuery(QStringLiteral("   ")).isEmpty());
    EXPECT_TRUE(buildFtsQuery(QStringLiteral("@@")).isEmpty());
    EXPECT_EQ(buildFtsQuery(QStringLiteral("a\"b")), QStringLiteral("\"a\"\"b\""));
}

// 钉住 Unicode 空白（含 U+3000 全角空格）与 ASCII 空白一样切开查询段：
// 全角空格是中文输入法的常见分隔符，粘成一段会把两个独立词误当成一个连续短语
TEST(MemoryToken, SplitsOnUnicodeWhitespace)
{
    EXPECT_EQ(buildFtsQuery(QStringLiteral("报价　合同")), QStringLiteral("\"报 价\" \"合 同\""));
    EXPECT_EQ(buildFtsQuery(QStringLiteral("报价\u3000合同")), QStringLiteral("\"报 价\" \"合 同\""));
    EXPECT_EQ(buildFtsQuery(QStringLiteral("报价\u2003合同")), QStringLiteral("\"报 价\" \"合 同\""));
}
