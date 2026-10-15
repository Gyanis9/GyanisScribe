#include "Core/Settings/NamingRules.h"

#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"

#include <format>

namespace GyanisScribe::Core
{
    QString nameTemplateOrDefault(const ISettingsStore &settings)
    {
        const auto stored = settings.stringValue(kSaveNameTemplate, QString{});
        if (stored.trimmed().isEmpty())
        {
            // 留空是合法选择（默认档），不是读失败：这一路不记警告，否则每次截图都往日志里塞一条
            return Base::Paths::defaultNameTemplate();
        }

        const auto parsed = Base::Paths::parseNameTemplate(stored);
        if (parsed)
        {
            return *parsed;
        }
        Base::Logger::warning(std::format("设置里的文件名模板不可用：{}，本次按默认档命名", parsed.error().message));
        return Base::Paths::defaultNameTemplate();
    }
} // namespace GyanisScribe::Core
