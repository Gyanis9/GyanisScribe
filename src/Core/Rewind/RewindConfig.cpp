#include "Core/Rewind/RewindConfig.h"

#include "Base/Logger.h"
#include "Base/Text.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"

#include <format>
#include <string_view>

namespace GyanisScribe::Core
{
    namespace
    {
        int readBoundedInt(const ISettingsStore &settings, const std::string_view key, const int fallback, const int minimum, const int maximum)
        {
            const int value = settings.intValue(key, fallback);
            if (value < minimum || value > maximum)
            {
                Base::Logger::warning(std::format("回溯配置 {} 的值 {} 越界（{}–{}），按默认 {} 处理", key, value, minimum, maximum, fallback));
                return fallback;
            }
            return value;
        }
    } // namespace

    Base::Expected<RewindConfig> readRewindConfig(const ISettingsStore &settings)
    {
        RewindConfig config;
        config.enabled        = settings.boolValue(kRewindEnabled, false);
        config.bufferSeconds  = readBoundedInt(settings, kRewindBufferSeconds, kDefaultRewindBufferSeconds, kMinimumRewindBufferSeconds, kMaximumRewindBufferSeconds);
        config.frameRate      = readBoundedInt(settings, kRewindFrameRate, kDefaultRewindFrameRate, kMinimumRewindFrameRate, kMaximumRewindFrameRate);
        config.memoryBudgetMb = kRewindMemoryBudgetMb;

        const auto exclusions = parseRewindExclusions(settings.stringValue(kRewindExcludeProcesses, QString{}));
        if (!exclusions)
        {
            return std::unexpected(exclusions.error());
        }
        config.excludedProcesses = *exclusions;
        return config;
    }

    Base::Expected<QStringList> parseRewindExclusions(const QString &text)
    {
        if (text.size() > kRewindMaximumExclusionTextLength)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiFormat(Base::uiTextUtf8("排除名单太长（{} 字符）：上限 {} 字符，请删掉不用的项"),
                                                                                                text.size(), kRewindMaximumExclusionTextLength)});
        }

        QStringList result;
        const auto  parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const auto &part: parts)
        {
            const auto name = part.trimmed().toLower();
            if (name.isEmpty())
            {
                continue;
            }
            if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || name.contains(QLatin1Char('*')) || name.contains(QLatin1Char('?')))
            {
                return std::unexpected(
                        Base::Error{Base::ErrorCode::InvalidArgument,
                                    Base::uiFormat(Base::uiTextUtf8("排除名单里的「{}」不是可执行文件名：只填文件名（如 keepass.exe），不要带路径或通配符"), name.toStdString())});
            }
            if (!result.contains(name))
            {
                result.append(name);
            }
        }
        return result;
    }

    bool isExcludedProcessName(const QString &processName, const QStringList &excludedProcesses) noexcept
    {
        const auto normalized = processName.trimmed().toLower();
        if (normalized.isEmpty())
        {
            return false;
        }
        for (const auto &entry: excludedProcesses)
        {
            if (normalized == entry || normalized == entry + QStringLiteral(".exe"))
            {
                return true;
            }
        }
        return false;
    }
} // namespace GyanisScribe::Core
