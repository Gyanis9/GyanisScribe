/**
 * @file CsvWriter.h
 * @brief 把检测到的简单表格写成 CSV
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Content/ContentTypes.h"

#include <QString>

#include <filesystem>

namespace GyanisScribe::Core
{
    /**
     * @brief 把表格转成 CSV 文本
     * @param table 表格检测结果
     * @return CRLF 行尾、仅含逗号/双引号/CR/LF 的字段加双引号、内部引号双写的文本；
     *         复杂表、未找到表或 cells 尺寸与 rows/columns 不一致时返回空串
     */
    [[nodiscard]] QString toCsv(const TableDetection &table);

    /**
     * @brief 把表格写成带 UTF-8 BOM 的 CSV 文件
     * @param filePath 目标文件，已存在则覆盖
     * @param table 表格检测结果
     * @return 成功返回空载荷；表格不可导出返回 InvalidArgument，目录创建或写盘失败返回 FileWriteFailed，文案带中文原因
     * @note 目标父目录不存在时先创建；用 QSaveFile 原子替换，失败时不会留下半份文件
     */
    [[nodiscard]] Base::Expected<void> writeCsvFile(const std::filesystem::path &filePath, const TableDetection &table);
} // namespace GyanisScribe::Core
