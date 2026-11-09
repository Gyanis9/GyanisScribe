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
    EXPECT_EQ(toCsv(table), QStringLiteral("\"a,b\",\"say \"\"hi\"\"\"\r\n,\"2\"\r\n"));
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

TEST(CsvWriter, OpenFailureLeavesNoFile)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const auto path = std::filesystem::path(temp.path().toStdString()) / "missing" / "table.csv";

    const auto result = writeCsvFile(path, tableWith({{QStringLiteral("A")}}));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, GyanisScribe::Base::ErrorCode::FileWriteFailed);
    EXPECT_FALSE(std::filesystem::exists(path));
}
