/**
 * @file RegionHistory.h
 * @brief 历史选区的存取：最近若干次截图的选区，供框选时循环挑与「重复上次选区」
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QRect>
#include <QString>

#include <vector>

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Capture
{

    /// 历史里最多记几条：再多也翻不过来，最久的那条摆在哪早忘了
    inline constexpr std::size_t kRegionHistoryLimit = 5;

    /**
     * @brief 一条历史选区
     * @details 坐标与遮罩交付的选区同一套：所属屏幕内的局部逻辑坐标。
     *          记的是「哪块屏上的哪块矩形」，换屏之后仍能回到原来那一块上去复用。
     */
    struct RegionHistoryEntry
    {
        QRect   region;     ///< 选区，屏内局部逻辑坐标
        QString screenName; ///< 所属屏幕的 QScreen::name()

        [[nodiscard]] bool operator==(const RegionHistoryEntry &) const = default;
    };

    /**
     * @brief 把历史序列化成一行文本
     * @param entries 历史，最近的在前
     * @return 每条「x,y,width,height,屏名」，段间以 ';' 相连；空历史返回空串
     */
    [[nodiscard]] QString regionHistoryToText(const std::vector<RegionHistoryEntry> &entries);

    /**
     * @brief 解析历史文本
     * @param text 来自配置的文本；空串是合法输入，表示「还没有历史」
     * @return 成功返回历史（最多取 kRegionHistoryLimit 条）；形状不对时返回 InvalidArgument 并点名第几段
     */
    [[nodiscard]] Base::Expected<std::vector<RegionHistoryEntry>> regionHistoryFromText(std::string_view text);

    /**
     * @brief 把一条记录压进历史
     * @param entries 现有历史
     * @param entry 新记录
     * @return 新历史：同屏同矩形的前移去重（不重复记），新的排最前，超过上限丢最久的一条
     */
    [[nodiscard]] std::vector<RegionHistoryEntry> pushedRegionHistory(std::vector<RegionHistoryEntry> entries, const RegionHistoryEntry &entry);

    /**
     * @brief 挑出属于某块屏的那几条选区
     * @param history 整份历史，最近的在前
     * @param screenName 屏幕名
     * @return 该屏的选区（屏内局部坐标），保持原有顺序
     * @details 遮罩铺的是特定一块屏，只把同屏的几条交给它：别屏的矩形放上去会露出一块越界的框。
     */
    [[nodiscard]] std::vector<QRect> regionsOnScreen(const std::vector<RegionHistoryEntry> &history, const QString &screenName);

    /**
     * @brief 读整份历史
     * @param settings 配置存储
     * @return 成功返回历史；文本坏了返回原因
     * @details 历史键为空时回退读升级前的两把单值键（读到就当作一条）：旧配置里那份「上次选区」
     *          还复用得上，第一次成功截图后会连历史一起补齐。
     */
    [[nodiscard]] Base::Expected<std::vector<RegionHistoryEntry>> loadRegionHistory(Settings::ISettingsStore &settings);

    /**
     * @brief 记一次选区：压进历史并写回配置
     * @param settings 配置存储
     * @param entry 这次的选区
     * @details 历史与两把单值键在同一个写点一起写（单值键 = 历史首条），升级前后的读法指向同一条；
     *          写不下去只记日志——截图本身已经成功，不该为记账把它变成失败。
     */
    void rememberRegion(Settings::ISettingsStore &settings, const RegionHistoryEntry &entry);

} // namespace GyanisScribe::Capture
