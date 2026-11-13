// 钉住表格 CSV 的引号、CRLF 行尾、UTF-8 BOM、拒绝面与失败不留半份文件

#include "Core/Content/CsvWriter.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <filesystem>

using GyanisScribe::Core::TableDetection;
using GyanisScribe::Core::toCsv;
using GyanisScribe::Core::writeCsvFile;

namespace
{
    TableDetection tableWith(const QVector<QVector<QString>> &cells)
    {
        TableDetection table;
        table.found   = true;
        table.rows    = static_cast<int>(cells.size());
        table.columns = table.rows > 0 ? static_cast<int>(cells.front().size()) : 0;
        table.cells   = cells;
        return table;
    }
} // namespace

TEST(CsvWriter, QuotesCommasQuotesAndEmptyCells)
{
    const auto table = tableWith({{QStringLiteral("a,b"), QStringLiteral("say \"hi\"")}, {QStringLiteral(""), QStringLiteral("2")}});
    EXPECT_EQ(toCsv(table), QStringLiteral("\"a,b\",\"say \"\"hi\"\"\"\r\n,2\r\n"));
}

TEST(CsvWriter, LeavesPlainFieldsUnquoted)
{
    const auto table = tableWith({{QStringLiteral("A"), QStringLiteral("2026")}});
    EXPECT_EQ(toCsv(table), QStringLiteral("A,2026\r\n"));
}

TEST(CsvWriter, KeepsNewlineInsideQuotedField)
{
    const auto table = tableWith({{QStringLiteral("line\nbreak")}});
    EXPECT_EQ(toCsv(table), QStringLiteral("\"line\nbreak\"\r\n"));
}

TEST(CsvWriter, RejectsComplexOrMissingTable)
{
    TableDetection complex;
    complex.found   = true;
    complex.complex = true;
    EXPECT_TRUE(toCsv(complex).isEmpty());
    EXPECT_TRUE(toCsv(TableDetection{}).isEmpty());
}

TEST(CsvWriter, RejectsCellsThatDoNotMatchRowsAndColumns)
{
    TableDetection table;
    table.found   = true;
    table.rows    = 2;
    table.columns = 2;
    table.cells   = {{QStringLiteral("只有一行")}};
    EXPECT_TRUE(toCsv(table).isEmpty());
}

TEST(CsvWriter, WritesUtf8BomAndCrlf)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto path = std::filesystem::path(temp.path().toStdString()) / "table.csv";
    ASSERT_TRUE(writeCsvFile(path, tableWith({{QStringLiteral("报价"), QStringLiteral("2026")}})).has_value());

    QFile file{QString::fromStdWString(path.wstring())};
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    ASSERT_GE(bytes.size(), 3);
    EXPECT_EQ(static_cast<unsigned char>(bytes.at(0)), 0xEF);
    EXPECT_EQ(static_cast<unsigned char>(bytes.at(1)), 0xBB);
    EXPECT_EQ(static_cast<unsigned char>(bytes.at(2)), 0xBF);
    EXPECT_TRUE(bytes.contains("\xE6\x8A\xA5\xE4\xBB\xB7")); // 报价
    EXPECT_TRUE(bytes.contains("\r\n"));
}

TEST(CsvWriter, RejectsComplexWriteWithoutCreatingFile)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto path = std::filesystem::path(temp.path().toStdString()) / "complex.csv";

    TableDetection table;
    table.found       = true;
    table.complex     = true;
    const auto result = writeCsvFile(path, table);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument);
    EXPECT_TRUE(QString::fromUtf8(result.error().message).contains(QStringLiteral("复杂表格")));
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(CsvWriter, CreatesMissingParentDirectory)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto path = std::filesystem::path(temp.path().toStdString()) / "missing" / "table.csv";

    ASSERT_TRUE(writeCsvFile(path, tableWith({{QStringLiteral("A")}})).has_value());
    EXPECT_TRUE(std::filesystem::is_regular_file(path));
}

TEST(CsvWriter, FailsWhenParentPathIsAFile)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto parent = std::filesystem::path(temp.path().toStdString()) / "blocker";
    QFile      blocker{QString::fromStdWString(parent.wstring())};
    ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    const auto path = parent / "table.csv";

    const auto result = writeCsvFile(path, tableWith({{QStringLiteral("A")}}));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, GyanisScribe::Base::ErrorCode::FileWriteFailed);
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(CsvWriter, PrefixesFormulaStartersWithASingleQuote)
{
    const auto cellOf = [](const QString &value) { return toCsv(tableWith({{value}})); };

    EXPECT_TRUE(cellOf(QStringLiteral("=cmd|calc")).startsWith(QStringLiteral("'=cmd|calc"))) << "等号开头没加前缀";
    EXPECT_TRUE(cellOf(QStringLiteral("+1+1")).startsWith(QStringLiteral("'+1+1")));
    EXPECT_TRUE(cellOf(QStringLiteral("@SUM(A1)")).startsWith(QStringLiteral("'@SUM(A1)")));
    EXPECT_TRUE(cellOf(QStringLiteral("\tHYPERLINK")).startsWith(QStringLiteral("'\tHYPERLINK"))) << "制表符开头没加前缀";
    EXPECT_EQ(cellOf(QStringLiteral("\rHYPERLINK")), QStringLiteral("\"'\rHYPERLINK\"\r\n")) << "回车开头的单元格要加前缀，同时仍按 RFC 4180 加引号";
    EXPECT_TRUE(cellOf(QStringLiteral("-cmd|calc")).startsWith(QStringLiteral("'-cmd|calc")));
    EXPECT_FALSE(cellOf(QStringLiteral("'=already")).startsWith(QStringLiteral("''"))) << "已经带引号前缀的又加了一层";
}

TEST(CsvWriter, LeavesNumbersAndPlainTextAloneButSeesThroughHalfWidthSpaces)
{
    const auto cellOf = [](const QString &value) { return toCsv(tableWith({{value}})); };

    EXPECT_EQ(cellOf(QStringLiteral("-5")), QStringLiteral("-5\r\n")) << "整数负数被改了";
    EXPECT_EQ(cellOf(QStringLiteral("-3.2")), QStringLiteral("-3.2\r\n")) << "小数负数被改了";
    EXPECT_EQ(cellOf(QStringLiteral("-1e5")), QStringLiteral("-1e5\r\n")) << "科学计数被改了";
    EXPECT_EQ(cellOf(QStringLiteral("普通文本")), QStringLiteral("普通文本\r\n"));
    EXPECT_TRUE(cellOf(QStringLiteral("  =cmd")).startsWith(QStringLiteral("'  =cmd"))) << "半角空格后的等号没被看见";
    EXPECT_FALSE(cellOf(QStringLiteral("　=cmd")).startsWith(QStringLiteral("'"))) << "全角空格属于内容，不该被当可跳过的前缀";
}

TEST(CsvWriter, FormulaPrefixStillQuotesWhenTheValueNeedsIt)
{
    const auto cellOf = [](const QString &value) { return toCsv(tableWith({{value}})); };
    EXPECT_EQ(cellOf(QStringLiteral("=cmd,a")), QStringLiteral("\"'=cmd,a\"\r\n")) << "净化与引用没叠加对";
}
