/**
 * @file HistoryRules.h
 * @brief 历史的排序、保留策略与筛选：不碰磁盘也不碰界面的一组判据
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "History/HistoryTypes.h"

#include <QVector>

#include <optional>

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::History
{

    /// 默认最多留 200 条：再多的历史列表要靠滚动条找，等于没有列表
    inline constexpr int kDefaultMaximumEntries = 200;

    /// 默认留 30 天：一个月的截图量足够回看，又不至于把盘塞满
    inline constexpr int kDefaultRetentionDays = 30;

    /**
     * @brief 保留策略：最多留几条、留几天
     * @details 两个上限取 0 都表示「这一维不限」。0 走「保留」而不是「清空」：
     *          歧义哨兵值宁可让用户多留一些历史，也不替他删文件。
     */
    struct HistoryRetentionPolicy
    {
        int maximumEntries = kDefaultMaximumEntries; ///< 最多留多少条，0 表示不限条数
        int retentionDays  = kDefaultRetentionDays;  ///< 最久留多少天，0 表示不限时长
    };

    /**
     * @brief 从配置读保留策略
     * @param settings 配置存储
     * @return 策略；配置里是负数时按 0（不限）处理，并记一条日志
     * @details 负数不当上限用：把 -1 编进「最多 -1 条」会让下一条记录刚写进去就被删掉，
     *          而这看起来像「历史功能坏了」。
     */
    [[nodiscard]] HistoryRetentionPolicy policyFromSettings(const Settings::ISettingsStore &settings);

    /**
     * @brief 按时间倒序排（新的在前）
     * @param entries 待排记录，原地排序
     * @details 同一秒产出的两条按路径升序排：并列时给一个确定次序，
     *          否则同一份历史文件两次运行会给出不同次序，用例没法钉。
     */
    void sortNewestFirst(QVector<HistoryEntry> &entries);

    /**
     * @brief 按策略裁掉超出的记录
     * @param entries 已按时间倒序或不排序的记录
     * @param policy 保留策略
     * @param now 判「过期」用的时刻，测试里由调用方给
     * @return 裁完并按时间倒序排好的记录
     * @details 先按时长再按条数：条数上限是「最近的 N 条」，先裁条数会把老记录留下。
     */
    [[nodiscard]] QVector<HistoryEntry> pruneForPolicy(const QVector<HistoryEntry> &entries, const HistoryRetentionPolicy &policy, const QDateTime &now);

    /**
     * @brief 这条记录是否落在筛选条件里
     * @param entry 待判记录
     * @param query 搜索词，空串表示不看文本；匹配文件名、类别中文名与 yyyy-MM-dd
     * @param kindFilter 类别筛选，空表示不限类别
     * @return true 表示应当显示
     * @note 大小写不敏感：用户在文件名里打的是英文，而文件名大小写他记不准
     */
    [[nodiscard]] bool matchesFilter(const HistoryEntry &entry, const QString &query, const std::optional<HistoryKind> &kindFilter);

} // namespace GyanisScribe::History
