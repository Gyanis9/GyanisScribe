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

namespace GyanisScribe::Base
{

    /**
     * @brief 把「宽x高」文本解析成尺寸
     * @param text 形如「300x200」「300×200」「300,200」「300 200」；x 不分大小写
     * @return 成功返回正数尺寸；解析不出或不是正数时为空
     * @details 采集那边的尺寸输入框与贴图的「自定义尺寸」共用这一处：
     *          两处各写一份解析，认的分隔符迟早会不一样
     */
    [[nodiscard]] std::optional<QSize> parseSizeText(const QString &text);

    /// 「倍率」那一形的上限：再大就是拿程序当缩放器用，先挡住
    inline constexpr double kMaximumScaleRatio = 100.0;

    /**
     * @brief 把「倍率」文本解析成缩放比
     * @param text 形如「2x」「1.5X」「50%」「0.5」；末尾一个 x 或 % 都认，纯数字按倍率算
     * @return 成功返回 (0, 100] 内的倍率；解析不出、非正或超上限时为空
     * @details 与「宽x高」分成两条解析：混在一起时 `2x` 会被当成「宽 2、高缺失」，
     *          用户看到的是「没读懂这个尺寸」，而他填的明明是一个常见写法。
     */
    [[nodiscard]] std::optional<double> parseScaleRatio(const QString &text);

} // namespace GyanisScribe::Base
