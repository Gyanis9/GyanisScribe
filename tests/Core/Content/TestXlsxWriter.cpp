// XLSX 写出的结构与内容：store-only ZIP 的签名与 CRC、sheet1.xml 的坐标与转义、拒绝面不落文件。

#include "Core/Content/CsvWriter.h"
#include "Core/Content/ContentTypes.h"
#include "Core/Content/XlsxWriter.h"
#include "Core/Content/ZipStoreWriter.h"

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

using GyanisScribe::Core::TableDetection;
using GyanisScribe::Core::writeXlsxFile;
using GyanisScribe::Core::ZipStoreWriter;

namespace
{
    /// 造一张 2×2 的表，单元格内容由调用方给
    TableDetection makeTable(const std::string &first, const std::string &second = "B1")
    {
        TableDetection table;
        table.found   = true;
        table.rows    = 2;
        table.columns = 2;
        table.cells   = {QVector<QString>{QString::fromStdString(first), QString::fromStdString(second)}, QVector<QString>{QStringLiteral("a2"), QStringLiteral("b2")}};
        return table;
    }

    quint32 littleEndian16(const QByteArray &bytes, qsizetype offset)
    {
        return static_cast<quint32>(static_cast<quint8>(bytes[offset])) | (static_cast<quint32>(static_cast<quint8>(bytes[offset + 1])) << 8);
    }

    quint32 littleEndian32(const QByteArray &bytes, qsizetype offset)
    {
        return static_cast<quint32>(static_cast<quint8>(bytes[offset])) | (static_cast<quint32>(static_cast<quint8>(bytes[offset + 1])) << 8) |
               (static_cast<quint32>(static_cast<quint8>(bytes[offset + 2])) << 16) | (static_cast<quint32>(static_cast<quint8>(bytes[offset + 3])) << 24);
    }

    /// 顺次走本地头，找出指定名字那一条的数据段
    QByteArray storedEntry(const QByteArray &zip, const QByteArray &wanted)
    {
        qsizetype offset = 0;
        while (offset + 30 <= zip.size() && zip.mid(offset, 4) == QByteArrayLiteral("PK\x03\x04"))
        {
            const quint32 crc = littleEndian32(zip, offset + 14);
            static_cast<void>(crc);
            const quint32    size      = littleEndian32(zip, offset + 18);
            const quint32    nameSize  = littleEndian16(zip, offset + 26);
            const quint32    extraSize = littleEndian16(zip, offset + 28);
            const QByteArray name      = zip.mid(offset + 30, nameSize);
            const qsizetype  dataAt    = offset + 30 + nameSize + extraSize;
            if (name == wanted)
            {
                return zip.mid(dataAt, size);
            }
            offset = dataAt + size;
        }
        return {};
    }

    /// 数一数 ZIP 里有多少条本地头
    int entryCount(const QByteArray &zip)
    {
        int       count  = 0;
        qsizetype offset = 0;
        while (offset + 30 <= zip.size() && zip.mid(offset, 4) == QByteArrayLiteral("PK\x03\x04"))
        {
            const quint32 size      = littleEndian32(zip, offset + 18);
            const quint32 nameSize  = littleEndian16(zip, offset + 26);
            const quint32 extraSize = littleEndian16(zip, offset + 28);
            offset += 30 + nameSize + extraSize + size;
            ++count;
        }
        return count;
    }
} // namespace

/// 钉住容器本身：签名、条目数、以及一条已知向量上的 CRC32
TEST(XlsxWriter, WritesAWellFormedStoreOnlyZip)
{
    ZipStoreWriter writer;
    writer.addFile(QByteArrayLiteral("abc"), QByteArrayLiteral("abc"));
    const QByteArray zip = writer.finish();

    ASSERT_GE(zip.size(), 30);
    EXPECT_EQ(zip.mid(0, 4), QByteArrayLiteral("PK\x03\x04")) << "本地头签名不对";
    EXPECT_EQ(zip.mid(zip.size() - 22, 4), QByteArrayLiteral("PK\x05\x06")) << "EOCD 不在结尾";
    EXPECT_EQ(littleEndian32(zip, 0 + 14), 0x352441C2u) << "CRC32 与已知向量不符";
    EXPECT_EQ(littleEndian32(zip, 0 + 18), 3u) << "长度字段应等于原文长度";
    EXPECT_EQ(littleEndian32(zip, 0 + 22), 3u) << "store-only 的未压缩长度应与原文一致";
}

/// 钉住工作簿的五个部件与 sheet1.xml 的坐标、文本与转义
TEST(XlsxWriter, WritesTheFivePartsAndEscapesCells)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = std::filesystem::path{directory.path().toStdWString()} / "table.xlsx";

    const auto table = makeTable("A&B <x> \"q\"");
    ASSERT_TRUE(writeXlsxFile(path, table).has_value());

    QFile file{QString::fromStdWString(path.wstring())};
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray zip = file.readAll();

    EXPECT_EQ(entryCount(zip), 5) << "XLSX 该有五个部件";
    EXPECT_FALSE(storedEntry(zip, QByteArrayLiteral("[Content_Types].xml")).isEmpty());
    EXPECT_FALSE(storedEntry(zip, QByteArrayLiteral("_rels/.rels")).isEmpty());
    EXPECT_FALSE(storedEntry(zip, QByteArrayLiteral("xl/workbook.xml")).isEmpty());
    EXPECT_FALSE(storedEntry(zip, QByteArrayLiteral("xl/_rels/workbook.xml.rels")).isEmpty());

    const QByteArray sheet = storedEntry(zip, QByteArrayLiteral("xl/worksheets/sheet1.xml"));
    ASSERT_FALSE(sheet.isEmpty()) << "找不到工作表部件";
    EXPECT_TRUE(sheet.contains(QByteArrayLiteral("<dimension ref=\"A1:B2\"/>")));
    EXPECT_TRUE(sheet.contains(QByteArrayLiteral("<c r=\"A1\" t=\"inlineStr\"><is><t xml:space=\"preserve\">A&amp;B &lt;x&gt; &quot;q&quot;</t></is></c>"))) << sheet.toStdString();
    EXPECT_TRUE(sheet.contains(QByteArrayLiteral("<c r=\"B2\" t=\"inlineStr\"><is><t xml:space=\"preserve\">b2</t></is></c>")));

    // 验收钩子：设了目录就把同一份包另存一份，供 Expand-Archive 真打包器验一遍
    if (const QByteArray dump = qgetenv("GYANISSCRIBE_XLSX_DUMP"); !dump.isEmpty())
    {
        const auto      target = std::filesystem::path{QString::fromUtf8(dump).toStdWString()} / "table.xlsx";
        std::error_code ignored;
        std::filesystem::create_directories(target.parent_path(), ignored);
        std::filesystem::copy_file(path, target, std::filesystem::copy_options::overwrite_existing, ignored);
    }
}

/// 空单元格与超过 26 列的坐标
TEST(XlsxWriter, WritesEmptyCellsAndTwoLetterColumns)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = std::filesystem::path{directory.path().toStdWString()} / "wide.xlsx";

    TableDetection table;
    table.found   = true;
    table.rows    = 1;
    table.columns = 27;
    QVector<QString> row;
    row.resize(27);
    row[26]     = QStringLiteral("last");
    table.cells = {row};
    ASSERT_TRUE(writeXlsxFile(path, table).has_value());

    QFile file{QString::fromStdWString(path.wstring())};
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray sheet = storedEntry(file.readAll(), QByteArrayLiteral("xl/worksheets/sheet1.xml"));
    EXPECT_TRUE(sheet.contains(QByteArrayLiteral("<c r=\"A1\"/>"))) << "空串该写成空单元格";
    EXPECT_TRUE(sheet.contains(QByteArrayLiteral("<c r=\"AA1\" t=\"inlineStr\">"))) << "第 27 列应是 AA";
}

/// 拒绝面：文案与 CSV 逐字一致，且不留下文件
TEST(XlsxWriter, RejectsTheSameTablesAsCsvAndNeverCreatesTheFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = std::filesystem::path{directory.path().toStdWString()} / "bad.xlsx";

    TableDetection complex  = makeTable("a");
    complex.complex         = true;
    const auto complexError = writeXlsxFile(path, complex);
    ASSERT_FALSE(complexError.has_value());
    const auto csvError = GyanisScribe::Core::writeCsvFile(path, complex);
    ASSERT_FALSE(csvError.has_value());
    EXPECT_EQ(complexError.error().message, csvError.error().message) << "两个写出器的拒绝文案不一致";
    EXPECT_FALSE(std::filesystem::exists(path));

    TableDetection missing;
    EXPECT_FALSE(writeXlsxFile(path, missing).has_value());
    EXPECT_FALSE(std::filesystem::exists(path));

    TableDetection ragged = makeTable("a");
    ragged.cells.pop_back();
    EXPECT_FALSE(writeXlsxFile(path, ragged).has_value());
    EXPECT_FALSE(std::filesystem::exists(path));
}
