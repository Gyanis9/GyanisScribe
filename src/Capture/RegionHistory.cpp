#include "Capture/RegionHistory.h"

#include "Capture/ScreenUtils.h"
#include "Core/Logger.h"
#include "Core/Text.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"

#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <format>

namespace GyanisScribe::Capture
{
    QString regionHistoryToText(const std::vector<RegionHistoryEntry> &entries)
    {
        QStringList parts;
        parts.reserve(static_cast<qsizetype>(entries.size()));
        for (const auto &entry: entries)
        {
            parts.append(regionToText(entry.region) + QLatin1Char(',') + entry.screenName);
        }
        return parts.join(QLatin1Char(';'));
    }

    Core::Expected<std::vector<RegionHistoryEntry>> regionHistoryFromText(const std::string_view text)
    {
        const auto trimmed = QString::fromUtf8(text).trimmed();
        if (trimmed.isEmpty())
        {
            return std::vector<RegionHistoryEntry>{};
        }

        std::vector<RegionHistoryEntry> entries;
        const auto                      segments = trimmed.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (int index = 0; index < segments.size() && entries.size() < kRegionHistoryLimit; ++index)
        {
            const auto &segment = segments[index];
            const auto  fields  = segment.split(QLatin1Char(','));
            if (fields.size() < 5)
            {
                return std::unexpected(
                        Core::Error{Core::ErrorCode::InvalidArgument,
                                    Core::uiFormat(Core::uiTextUtf8("选区历史的记录格式不对：第 {} 段「{}」应为「x,y,width,height,屏名」。请删掉这一项，让程序重新记一次"),
                                                   index + 1, segment.toStdString())});
            }

            // 前四段交给选区那条解析路：数字的校验与措辞只有一处
            const auto numbers = fields.mid(0, 4).join(QLatin1Char(','));
            const auto region  = regionFromText(numbers.toStdString());
            if (!region)
            {
                return std::unexpected(region.error());
            }

            // 第五段起都算屏名：屏名里真带上逗号时也不会被截断
            RegionHistoryEntry entry;
            entry.region     = *region;
            entry.screenName = fields.mid(4).join(QLatin1Char(','));
            entries.push_back(std::move(entry));
        }
        return entries;
    }

    std::vector<RegionHistoryEntry> pushedRegionHistory(std::vector<RegionHistoryEntry> entries, const RegionHistoryEntry &entry)
    {
        entries.erase(std::remove(entries.begin(), entries.end(), entry), entries.end());
        entries.insert(entries.begin(), entry);
        if (entries.size() > kRegionHistoryLimit)
        {
            entries.resize(kRegionHistoryLimit);
        }
        return entries;
    }

    std::vector<QRect> regionsOnScreen(const std::vector<RegionHistoryEntry> &history, const QString &screenName)
    {
        std::vector<QRect> regions;
        for (const auto &entry: history)
        {
            if (entry.screenName == screenName)
            {
                regions.push_back(entry.region);
            }
        }
        return regions;
    }

    Core::Expected<std::vector<RegionHistoryEntry>> loadRegionHistory(Settings::ISettingsStore &settings)
    {
        const auto stored = settings.stringValue(Settings::kCaptureRegionHistory, QString());
        if (!stored.trimmed().isEmpty())
        {
            return regionHistoryFromText(stored.toStdString());
        }

        // 升级前的配置里只有「最近一条」那把单值键：读到就当作一条历史，让旧记录还复用得上
        const auto legacyRegion = settings.stringValue(Settings::kCaptureLastRegion, QString());
        if (legacyRegion.trimmed().isEmpty())
        {
            return std::vector<RegionHistoryEntry>{};
        }
        const auto region = regionFromText(legacyRegion.toStdString());
        if (!region)
        {
            return std::unexpected(region.error());
        }
        return std::vector<RegionHistoryEntry>{RegionHistoryEntry{*region, settings.stringValue(Settings::kCaptureLastScreen, QString())}};
    }

    void rememberRegion(Settings::ISettingsStore &settings, const RegionHistoryEntry &entry)
    {
        auto history = loadRegionHistory(settings);
        if (!history)
        {
            // 旧记录坏了：这次从新的一条重新记起，别让一条坏文本把「记事本」整体卡死
            Core::Logger::warning(std::format("选区历史读不出，这次从新的一条重新记起：{}", history.error().message));
            history = std::vector<RegionHistoryEntry>{};
        }

        const auto updated = pushedRegionHistory(*history, entry);
        if (const auto stored = settings.setValue(Settings::kCaptureRegionHistory, QVariant(regionHistoryToText(updated))); !stored)
        {
            Core::Logger::warning(std::format("选区历史没能记下：{}", stored.error().message));
        }
        // 单值键与历史首条同一个写点一起写：升级前后的两种读法永远指向同一条
        if (const auto stored = settings.setValue(Settings::kCaptureLastRegion, QVariant(regionToText(entry.region))); !stored)
        {
            Core::Logger::warning(std::format("上次截图区域没能记下：{}", stored.error().message));
        }
        if (const auto stored = settings.setValue(Settings::kCaptureLastScreen, QVariant(entry.screenName)); !stored)
        {
            Core::Logger::warning(std::format("上次截图所在屏没能记下：{}", stored.error().message));
        }
    }

} // namespace GyanisScribe::Capture
