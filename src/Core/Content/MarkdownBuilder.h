/**
 * @file MarkdownBuilder.h
 * @brief 把 OCR 结果按版面重排成 Markdown
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Content/ContentTypes.h"
#include "Core/Ocr/OcrTypes.h"

#include <QString>

#include <optional>

namespace GyanisScribe::Core
{
    /**
     * @brief 把识别结果重排成 Markdown
     * @param result 识别结果，行内词框决定阅读顺序与缩进
     * @param table 可选的表格检测结果；只有 found 且非 complex 时才替换对应行
     * @return LF 行尾、末尾恰好一个换行的 Markdown；没有可用文字时返回空串
     * @note 标题、列表、代码与段落全部按行高、缩进和文本启发式判定，不读字体信息
     */
    [[nodiscard]] QString buildMarkdown(const OcrResult &result, const std::optional<TableDetection> &table = std::nullopt);
} // namespace GyanisScribe::Core
