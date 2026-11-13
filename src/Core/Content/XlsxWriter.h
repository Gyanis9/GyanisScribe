/**
 * @file XlsxWriter.h
 * @brief 把一张检测结果写成单工作表的 XLSX
 * @author Gyanis
 * @date 2026-11-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Content/ContentTypes.h"

#include <filesystem>

namespace GyanisScribe::Core
{

    /**
     * @brief 把表格写成 XLSX
     * @param filePath 目标路径，后缀由调用方保证是 .xlsx
     * @param table 检测结果
     * @return 成功返回空；表不能导出、行列超限或写盘失败时返回中文原因
     * @details 单工作表、inlineStr、无样式/无公式/无合并；拒绝面与 CSV 共用 `tableExportProblem`，
     *          拒绝时不创建也不改写目标文件。
     */
    [[nodiscard]] Base::Expected<void> writeXlsxFile(const std::filesystem::path &filePath, const TableDetection &table);

} // namespace GyanisScribe::Core
