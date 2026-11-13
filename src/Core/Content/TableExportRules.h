/**
 * @file TableExportRules.h
 * @brief 一张检测结果能不能落盘：CSV 与 XLSX 两个写出器共用这一条规则
 * @author Gyanis
 * @date 2026-11-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Content/ContentTypes.h"

#include <optional>

namespace GyanisScribe::Core
{

    /**
     * @brief 判一张检测结果能不能导出
     * @param table 检测结果
     * @return 能导出返回空；不能时返回给用户看的中文原因
     * @details 复杂表、未检出、行列不符三种各一句话；两个写出器共用同一份文案，避免说法漂移。
     */
    [[nodiscard]] std::optional<Base::Error> tableExportProblem(const TableDetection &table);

} // namespace GyanisScribe::Core
