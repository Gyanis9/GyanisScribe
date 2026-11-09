/**
 * @file MemoryToken.h
 * @brief 中文感知的 FTS 文本预处理与查询串构造
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QString>

namespace GyanisScribe::Core
{
    /**
     * @brief 预处理入库文本：CJK 逐字加空格，ASCII 字母数字保持成段
     * @param text 原始 OCR 文本、窗口标题或文件名
     * @return 供 FTS5 索引的文本；控制字符被丢弃，空白归一并去掉首尾
     */
    [[nodiscard]] QString prepareFtsText(const QString &text);

    /**
     * @brief 把用户输入构造成 FTS5 MATCH 表达式
     * @param userText 用户原文，可为空
     * @return 只由引号短语与 ASCII 前缀组成的查询串；没有任何有效 token 时为空
     * @note 返回值只能作为 MATCH 的绑定值，不得拼进 SQL 文本
     */
    [[nodiscard]] QString buildFtsQuery(const QString &userText);
} // namespace GyanisScribe::Core
