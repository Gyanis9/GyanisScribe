#include "Core/History/HistoryRules.h"

#include "Base/Logger.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"

#include <QString>

#include <algorithm>
#include <format>

namespace
{
    using GyanisScribe::Core::HistoryEntry;
    using GyanisScribe::Core::HistoryKind;

    /// 列表日期那一栏的格式：与文件名里的时间戳同一口径，用户搜日期能搜到
    constexpr const char *kDateTextFormat = "yyyy-MM-dd";
} // namespace

namespace GyanisScribe::Core
{
    void sortNewestFirst(QVector<HistoryEntry> &entries)
    {
        // 时间新的在前；同一时刻的按路径升序，保证同一份历史文件两次给出同一次序
        std::ranges::sort(entries,
                          [](const HistoryEntry &left, const HistoryEntry &right)
                          {
                              if (left.moment != right.moment)
                              {
                                  return left.moment > right.moment;
                              }
                              return left.filePath.native() < right.filePath.native();
                          });
    }

    HistoryRetentionPolicy policyFromSettings(const Core::ISettingsStore &settings)
    {
        const auto maximumEntries = settings.intValue(Core::kHistoryMaximumEntries, kDefaultMaximumEntries);
        const auto retentionDays  = settings.intValue(Core::kHistoryRetentionDays, kDefaultRetentionDays);

        if (maximumEntries < 0 || retentionDays < 0)
        {
            Base::Logger::warning(
                    std::format("历史保留策略里有负数（条数 {}、天数 {}），本档按「不限」处理。请在设置的通用页把这两项改回不小于 0 的值", maximumEntries, retentionDays));
        }

        return HistoryRetentionPolicy{
                .maximumEntries = std::max(0, maximumEntries),
                .retentionDays  = std::max(0, retentionDays),
        };
    }

    QVector<HistoryEntry> pruneForPolicy(const QVector<HistoryEntry> &entries, const HistoryRetentionPolicy &policy, const QDateTime &now)
    {
        auto sorted = entries;
        sortNewestFirst(sorted);

        // 先按时长再按条数：条数上限指的是「最近的 N 条」，反过来会把老记录留在窗口里
        const auto            oldestAllowed = policy.retentionDays > 0 ? now.addDays(-policy.retentionDays) : QDateTime{};
        QVector<HistoryEntry> kept;
        for (const auto &entry: sorted)
        {
            if (oldestAllowed.isValid() && entry.moment < oldestAllowed)
            {
                continue;
            }
            if (policy.maximumEntries > 0 && kept.size() >= static_cast<qsizetype>(policy.maximumEntries))
            {
                break;
            }
            kept.append(entry);
        }
        return kept;
    }

    bool matchesFilter(const HistoryEntry &entry, const QString &query, const std::optional<HistoryKind> &kindFilter)
    {
        if (kindFilter && entry.kind != *kindFilter)
        {
            return false;
        }

        if (query.isEmpty())
        {
            return true;
        }

        const auto fileName = QString::fromStdWString(entry.filePath.filename().wstring());
        const auto dateText = entry.moment.toString(QLatin1String{kDateTextFormat});
        // 文件名、类别中文名、日期三处都查：用户记不住文件名，只会记得「上周的截图」
        return fileName.contains(query, Qt::CaseInsensitive) || historyKindLabel(entry.kind).contains(query, Qt::CaseInsensitive) || dateText.contains(query, Qt::CaseInsensitive);
    }

} // namespace GyanisScribe::Core
