#include "Core/Content/XlsxWriter.h"

#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/Content/TableExportRules.h"
#include "Core/Content/ZipStoreWriter.h"

#include <QSaveFile>

#include <QByteArray>

#include <string>

namespace
{
    /// 单元格上限：与 Excel 自己的行列上限一致，超过直接拒绝
    constexpr int kMaximumXlsxRows    = 1048576;
    constexpr int kMaximumXlsxColumns = 16384;

    /// XML 文本转义：控制字符直接剔除，五个实体按规范换
    QByteArray escaped(const QString &text)
    {
        QByteArray       out;
        const QByteArray utf8 = text.toUtf8();
        out.reserve(utf8.size());
        for (const char raw: utf8)
        {
            const auto byte = static_cast<unsigned char>(raw);
            switch (raw)
            {
                case '&':
                    out.append("&amp;");
                    break;
                case '<':
                    out.append("&lt;");
                    break;
                case '>':
                    out.append("&gt;");
                    break;
                case '"':
                    out.append("&quot;");
                    break;
                case '\'':
                    out.append("&apos;");
                    break;
                default:
                    // 0x00-0x1F 里只有制表、换行、回车是合法 XML 字符，其余剔除
                    if (byte < 0x20 && raw != '\t' && raw != '\n' && raw != '\r')
                    {
                        break;
                    }
                    out.append(raw);
                    break;
            }
        }
        return out;
    }

    /// 列序号换成 A、B … Z、AA … 的坐标
    QByteArray columnName(int columnIndex)
    {
        QByteArray name;
        int        value = columnIndex + 1;
        while (value > 0)
        {
            const int remainder = (value - 1) % 26;
            name.prepend(static_cast<char>('A' + remainder));
            value = (value - 1) / 26;
        }
        return name;
    }

    QByteArray sheetXml(const GyanisScribe::Core::TableDetection &table)
    {
        QByteArray xml =
                R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><dimension ref="A1:)";
        xml.append(columnName(table.columns - 1));
        xml.append(QByteArray::number(table.rows));
        xml.append(R"("/><sheetData>)");
        for (int row = 0; row < table.rows; ++row)
        {
            xml.append(R"(<row r=")");
            xml.append(QByteArray::number(row + 1));
            xml.append(R"(">)");
            for (int column = 0; column < table.columns; ++column)
            {
                const QString &cell = table.cells.at(row).at(column);
                xml.append(R"(<c r=")");
                xml.append(columnName(column));
                xml.append(QByteArray::number(row + 1));
                if (cell.isEmpty())
                {
                    xml.append(R"("/>)");
                    continue;
                }
                xml.append(R"(" t="inlineStr"><is><t xml:space="preserve">)");
                xml.append(escaped(cell));
                xml.append(R"(</t></is></c>)");
            }
            xml.append(R"(</row>)");
        }
        xml.append(R"(</sheetData></worksheet>)");
        return xml;
    }

    QByteArray contentTypesXml()
    {
        return QByteArrayLiteral(
                R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>)");
    }

    QByteArray rootRelsXml()
    {
        return QByteArrayLiteral(
                R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>)");
    }

    QByteArray workbookXml()
    {
        return QByteArrayLiteral(
                R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)");
    }

    QByteArray workbookRelsXml()
    {
        return QByteArrayLiteral(
                R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)");
    }
} // namespace

namespace GyanisScribe::Core
{
    Base::Expected<void> writeXlsxFile(const std::filesystem::path &filePath, const TableDetection &table)
    {
        if (const auto problem = tableExportProblem(table); problem.has_value())
        {
            return std::unexpected(*problem);
        }
        if (table.rows > kMaximumXlsxRows || table.columns > kMaximumXlsxColumns)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("表格超出 Excel 的行列上限，无法导出 XLSX；请缩小截图范围后重试")});
        }

        ZipStoreWriter writer;
        writer.addFile(QByteArrayLiteral("[Content_Types].xml"), contentTypesXml());
        writer.addFile(QByteArrayLiteral("_rels/.rels"), rootRelsXml());
        writer.addFile(QByteArrayLiteral("xl/workbook.xml"), workbookXml());
        writer.addFile(QByteArrayLiteral("xl/_rels/workbook.xml.rels"), workbookRelsXml());
        writer.addFile(QByteArrayLiteral("xl/worksheets/sheet1.xml"), sheetXml(table));
        const QByteArray package = writer.finish();

        const std::string displayPath = Base::Paths::toDisplayText(filePath).toStdString();
        if (const auto parent = filePath.parent_path(); !parent.empty())
        {
            if (const auto ensured = Base::Paths::ensureDirectory(parent); !ensured)
            {
                return std::unexpected(ensured.error());
            }
        }
        QSaveFile output(Base::Paths::toDisplayText(filePath));
        if (!output.open(QIODevice::WriteOnly))
        {
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed, Base::uiFormat(Base::uiTextUtf8("XLSX 写不出去：{}（{}）。请换一个可写的目录后重试"), displayPath,
                                                                                                output.errorString().toStdString())});
        }
        if (output.write(package) != package.size())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed,
                                               Base::uiFormat(Base::uiTextUtf8("XLSX 无法写入：{}（{}）。请检查磁盘空间后重试"), displayPath, output.errorString().toStdString())});
        }
        if (!output.commit())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed,
                                               Base::uiFormat(Base::uiTextUtf8("XLSX 无法保存：{}（{}）。请检查磁盘空间后重试"), displayPath, output.errorString().toStdString())});
        }
        return {};
    }
} // namespace GyanisScribe::Core
