/**
 * @file OcrLanguage.h
 * @brief 识别语言的平台中立词表：设置页下拉与默认档的真源
 * @author Gyanis
 * @date 2026-10-16
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QString>
#include <QStringList>

namespace GyanisScribe::Core
{

    /// 用户可见的默认语言标签（也是设置项 Ocr/Language 的默认值）
    constexpr const char *kDefaultOcrLanguage = "zh-Hans";

    /**
     * @brief 本程序认识的语言标签
     * @return 供设置页下拉用的标签列表，顺序即显示顺序
     */
    [[nodiscard]] QStringList supportedOcrLanguageTags();

} // namespace GyanisScribe::Core
