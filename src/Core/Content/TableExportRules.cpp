#include "Core/Content/TableExportRules.h"

#include "Base/Text.h"

namespace GyanisScribe::Core
{
    std::optional<Base::Error> tableExportProblem(const TableDetection &table)
    {
        if (!table.found)
        {
            return Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("没有检测到可导出的表格，请先确认截图里有对齐的表格")};
        }
        if (table.complex)
        {
            return Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("检测到复杂表格，暂不支持导出 CSV；请改用 Markdown 或 TXT 复制表格内容")};
        }
        if (table.rows <= 0 || table.columns <= 0 || table.cells.size() != table.rows)
        {
            return Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("表格结构不完整，暂不支持导出 CSV；请改用 Markdown 或 TXT 复制表格内容")};
        }
        for (const QVector<QString> &row: table.cells)
        {
            if (row.size() != table.columns)
            {
                return Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("表格结构不完整，暂不支持导出 CSV；请改用 Markdown 或 TXT 复制表格内容")};
            }
        }
        return std::nullopt;
    }
} // namespace GyanisScribe::Core
