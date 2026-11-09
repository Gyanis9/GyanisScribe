/**
 * @file TableDetector.h
 * @brief 从 OCR 行与词框检测一张表格
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Content/ContentTypes.h"
#include "Core/Ocr/OcrTypes.h"

namespace GyanisScribe::Core
{
    /**
     * @brief 在识别结果里找一张表格
     * @param result 一次识别的完整结果，词框坐标系与结果图一致
     * @return 唯一一张最可信的表；没有候选时 found 为 false，复杂表的 complex 为 true 且带中文原因
     * @note 只按词框对齐推断，不读取表格线；存在多张表时优先简单表，其次取 rows × columns 最大者
     */
    [[nodiscard]] TableDetection detectTable(const OcrResult &result);
} // namespace GyanisScribe::Core
