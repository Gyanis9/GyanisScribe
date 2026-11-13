#include "App/SceneCleanupGuard.h"

#include "Base/Logger.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Platform/IPlatformIntegration.h"

#include <QDateTime>
#include <QString>

#include <format>

namespace GyanisScribe::App
{
    SceneCleanupGuard::SceneCleanupGuard(GyanisScribe::Platform::IPlatformIntegration &integration, GyanisScribe::Core::ISettingsStore &settings) :
        m_integration(integration), m_settings(settings)
    {
    }

    SceneCleanupGuard::~SceneCleanupGuard()
    {
        // 只有本对象真的清过场才由析构兜底：启动兜底那种「只有标记残留」的临时守卫
        // 已经显式恢复过，失败时平台可能已经恢复成功、只是标记没写掉；
        // 析构再发一次会把恢复翻成把所有窗口最小化，甚至清掉要留给下次启动的标记
        if (!m_cleared)
        {
            return;
        }

        // 析构里没有再调一次的机会：落日志，标记留着供下次启动补恢复
        if (const auto restored = restore(); !restored)
        {
            GyanisScribe::Base::Logger::warning(std::format("清场恢复未完成：{}", restored.error().message));
        }
    }

    Base::Expected<void> SceneCleanupGuard::clear()
    {
        if (m_cleared)
        {
            return {};
        }

        if (const auto cleared = m_integration.setSceneCleared(true); !cleared)
        {
            // 平台可能已经动了一半：补一次恢复把桌面撤回去，原错误交回给调用方说明原因
            if (const auto undone = m_integration.setSceneCleared(false); !undone)
            {
                GyanisScribe::Base::Logger::warning(std::format("清场失败后撤销半边动作也没成：{}", undone.error().message));
            }
            return std::unexpected(cleared.error());
        }

        // 标记是崩溃后补恢复的唯一线索：写不进去就不算清场成功，平台动作也要撤掉
        const auto written = m_settings.setValue(GyanisScribe::Core::kSceneClearMarker, QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        if (!written)
        {
            if (const auto undone = m_integration.setSceneCleared(false); !undone)
            {
                GyanisScribe::Base::Logger::warning(std::format("清场标记写不进去，撤销平台动作也没成：{}", undone.error().message));
            }
            return std::unexpected(written.error());
        }

        m_cleared = true;
        return {};
    }

    Base::Expected<void> SceneCleanupGuard::restore()
    {
        // 标记残留而内存里没记清场，说明是上一次没收尾留下的：同样要恢复并清标记
        const auto marker = m_settings.stringValue(GyanisScribe::Core::kSceneClearMarker, QString{});
        if (!m_cleared && marker.isEmpty())
        {
            return {};
        }

        if (const auto restored = m_integration.setSceneCleared(false); !restored)
        {
            // 标记原样留着：下次启动还会再试一次
            return std::unexpected(restored.error());
        }

        if (const auto cleared = m_settings.setValue(GyanisScribe::Core::kSceneClearMarker, QVariant{}); !cleared)
        {
            return std::unexpected(cleared.error());
        }

        m_cleared = false;
        return {};
    }

    bool SceneCleanupGuard::isCleared() const noexcept
    {
        return m_cleared;
    }

} // namespace GyanisScribe::App
