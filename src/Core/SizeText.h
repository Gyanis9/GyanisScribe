/**
 * @file SizeText.h
 * @brief 「宽x高」这类尺寸文本的解析：采集与贴图两条路共用一处
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QSize>
#include <QString>

#include <optional>

namespace GyanisScribe::Core
{

    /**
     * @brief 把「宽x高」文本解析成尺寸
     * @param text 形如「300x200」「300×200」「300,200」「300 200」；x 不分大小写
     * @return 成功返回正数尺寸；解析不出或不是正数时为空
     * @details 采集那边的尺寸输入框与贴图的「自定义尺寸」共用这一处：
     *          两处各写一份解析，认的分隔符迟早会不一样
     */
    [[nodiscard]] std::optional<QSize> parseSizeText(const QString &text);

} // namespace GyanisScribe::Core
