#include "Core/Content/CsvWriter.h"

#include "Base/Paths.h"
#include "Base/Text.h"

#include <QByteArray>
#include <QSaveFile>

#include <string>

namespace GyanisScribe::Core
{
    namespace
    {
        [[nodiscard]] bool isExportable(const TableDetection &table)
        {
            if (!table.found || table.complex || table.rows <= 0 || table.columns <= 0)
            {
                return false;
            }
            if (table.cells.size() != table.rows)
            {
                return false;
            }
            for (const QVector<QString> &row: table.cells)
            {
                if (row.size() != table.columns)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] QString quotedField(const QString &value)
        {
            // 只在字段含分隔符、双引号或换行时加引号：这是 RFC 4180 的最小引用，
            // 纯数字与普通文本保持原样，避免把 `2026` 这类内容写成带引号的字符串
            const bool needsQuotes = value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"')) || value.contains(QLatin1Char('\r')) || value.contains(QLatin1Char('\n'));
            if (!needsQuotes)
            {
                return value;
            }
            QString escaped = value;
            escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            return QStringLiteral("\"") + escaped + QStringLiteral("\"");
        }
    } // namespace

    QString toCsv(const TableDetection &table)
    {
        if (!isExportable(table))
        {
            return {};
        }

        QString output;
        for (int row = 0; row < table.rows; ++row)
        {
            for (int column = 0; column < table.columns; ++column)
            {
                if (column > 0)
                {
                    output.append(QLatin1Char(','));
                }
                output.append(quotedField(table.cells.at(row).at(column)));
            }
            output.append(QStringLiteral("\r\n"));
        }
        return output;
    }

    Base::Expected<void> writeCsvFile(const std::filesystem::path &filePath, const TableDetection &table)
    {
        if (!isExportable(table))
        {
            // 拒绝原因分三档：复杂表、没找到表、尺寸对不上；三者都不该写 BOM 或碰目标文件
            if (table.complex)
            {
                return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("检测到复杂表格，暂不支持导出 CSV；请改用 Markdown 或 TXT 复制表格内容")});
            }
            if (!table.found)
            {
                return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("没有检测到可导出的表格，请先确认截图里有对齐的表格")});
            }
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("表格结构不完整，暂不支持导出 CSV；请改用 Markdown 或 TXT 复制表格内容")});
        }

        const QString     csv         = toCsv(table);
        const std::string displayPath = Base::Paths::toDisplayText(filePath).toStdString();
        // 与 writeTextFile 同口径：父目录不存在时先建出来，导出到新目录不必让调用方再跑一趟
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
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed, Base::uiFormat(Base::uiTextUtf8("CSV 写不出去：{}（{}）。请换一个可写的目录后重试"), displayPath,
                                                                                                output.errorString().toStdString())});
        }

        constexpr char   kUtf8Bom[] = "\xEF\xBB\xBF";
        const QByteArray bom(kUtf8Bom, 3);
        const QByteArray payload = csv.toUtf8();
        // 先 BOM 后正文：Excel 靠这三个字节认出 UTF-8，中文表头才不会乱码
        if (output.write(bom) != bom.size() || output.write(payload) != payload.size())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed,
                                               Base::uiFormat(Base::uiTextUtf8("CSV 没能写入：{}（{}）。请检查磁盘空间后重试"), displayPath, output.errorString().toStdString())});
        }
        if (!output.commit())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed,
                                               Base::uiFormat(Base::uiTextUtf8("CSV 没能落盘：{}（{}）。请检查磁盘空间后重试"), displayPath, output.errorString().toStdString())});
        }
        return {};
    }
} // namespace GyanisScribe::Core
