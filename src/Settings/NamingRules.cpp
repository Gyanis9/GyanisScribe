#include "Settings/NamingRules.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"

#include <format>

namespace GyanisScribe::Settings
{
    QString nameTemplateOrDefault(const ISettingsStore &settings)
    {
        const auto stored = settings.stringValue(kSaveNameTemplate, QString{});
        if (stored.trimmed().isEmpty())
        {
            // 留空是合法选择（默认档），不是读失败：这一路不记警告，否则每次截图都往日志里塞一条
            return Core::Paths::defaultNameTemplate();
        }

        const auto parsed = Core::Paths::parseNameTemplate(stored);
        if (parsed)
        {
            return *parsed;
        }
        Core::Logger::warning(std::format("设置里的文件名模板不可用：{}，本次按默认档命名", parsed.error().message));
        return Core::Paths::defaultNameTemplate();
    }
} // namespace GyanisScribe::Settings
