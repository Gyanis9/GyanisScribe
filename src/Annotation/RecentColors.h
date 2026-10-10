/**
 * @file RecentColors.h
 * @brief 标注色板的「最近使用颜色」清单：入队、去重、序列化与读回
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QColor>
#include <QStringList>

#include <cstddef>
#include <span>
#include <vector>

namespace GyanisScribe::Annotation
{
    /// 最近使用颜色的上限：再多就挤满工具栏，也记不住那么多次
    inline constexpr std::size_t kRecentColorCapacity = 6;

    /**
     * @brief 把刚用过的颜色插到清单最前
     * @param current 当前清单，首位是最久用不到的那端之外的最新值
     * @param picked 这次用的颜色
     * @param capacity 清单上限，超出时从尾部丢弃
     * @return 新清单：picked 在首位，重复值只留最新的那一处
     * @note 无效颜色（!isValid）原样返回旧清单：色板不该被一次失败的取色改动。
     */
    [[nodiscard]] std::vector<QColor> pushRecentColor(std::span<const QColor> current, const QColor &picked, std::size_t capacity = kRecentColorCapacity);

    /**
     * @brief 序列化一条颜色
     * @param color 颜色
     * @return 形如 #RRGGBB 的文本
     */
    [[nodiscard]] QString recentColorToken(const QColor &color);

    /**
     * @brief 把清单序列化成配置里存的那些词
     * @param colors 清单
     * @return 与清单同序的文本列表
     */
    [[nodiscard]] QStringList serializeRecentColors(std::span<const QColor> colors);

    /**
     * @brief 读回配置里的清单
     * @param tokens 配置文本列表
     * @return 解析出的颜色清单；任一词不是 #RRGGBB 时返回带该词的原因
     * @details 整条一起拒绝而不是跳过坏词：跳过的话用户看到的就是「我明明存了八个只剩五个」，
     *          而原因落在日志里他看不见。
     */
    [[nodiscard]] Core::Expected<std::vector<QColor>> parseRecentColors(const QStringList &tokens);

} // namespace GyanisScribe::Annotation
