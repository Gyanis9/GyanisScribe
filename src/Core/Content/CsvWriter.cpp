#include "Core/Content/CsvWriter.h"

#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/Content/TableExportRules.h"

#include <QByteArray>
#include <QSaveFile>

#include <string>

namespace GyanisScribe::Core
{
    namespace
    {
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
        if (tableExportProblem(table).has_value())
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
        if (const auto problem = tableExportProblem(table); problem.has_value())
        {
            return std::unexpected(*problem);
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
                                               Base::uiFormat(Base::uiTextUtf8("CSV 无法写入：{}（{}）。请检查磁盘空间后重试"), displayPath, output.errorString().toStdString())});
        }
        if (!output.commit())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed,
                                               Base::uiFormat(Base::uiTextUtf8("CSV 无法保存：{}（{}）。请检查磁盘空间后重试"), displayPath, output.errorString().toStdString())});
        }
        return {};
    }
} // namespace GyanisScribe::Core
