#include "App/SceneCleanupGuard.h"

#include "Base/Logger.h"
#include "Base/Text.h"
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
        // 析构再发一次会重复撤销，还可能清掉要留给下次启动的标记
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

        // 标记先落盘再动平台：平台动作一开始桌面就可能被改了样，清场中途被强杀时
        // 只有已经写下的标记能让下次启动知道要恢复
        const auto written = m_settings.setValue(GyanisScribe::Core::kSceneClearMarker, QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        if (!written)
        {
            // 写不进去就不碰平台：没有标记的清场态没人认领
            return std::unexpected(written.error());
        }
        m_settings.sync();

        if (const auto cleared = m_integration.setSceneCleared(true); !cleared)
        {
            // 平台可能已经动了一半：补一次恢复把桌面撤回去，原错误交回给调用方说明原因
            if (const auto undone = m_integration.setSceneCleared(false); !undone)
            {
                // 撤销也没成，桌面可能停在半清场态：标记必须留着，下次启动才有人回收这一场
                GyanisScribe::Base::Logger::warning(std::format("清场失败后撤销也没成，保留标记等下次启动恢复：{}", undone.error().message));
                return std::unexpected(cleared.error());
            }
            // 撤回去了才算这次没清成：标记不能留给下次启动去恢复一场没发生的清场
            if (const auto dropped = m_settings.setValue(GyanisScribe::Core::kSceneClearMarker, QVariant{}); !dropped)
            {
                GyanisScribe::Base::Logger::warning(std::format("清场没成，标记也没清掉：{}", dropped.error().message));
            }
            m_settings.sync();
            return std::unexpected(cleared.error());
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

        // 平台已经还原，桌面回到原样；标记清不掉只影响下次启动多试一次恢复，
        // 撤销最小化全部对已还原的窗口没有副作用
        m_cleared = false;
        if (const auto dropped = m_settings.setValue(GyanisScribe::Core::kSceneClearMarker, QVariant{}); !dropped)
        {
            // 桌面已经还原，只有标记没写掉：文案要说清这一点，协调器加上前缀才不会把已还原说成没恢复
            return std::unexpected(GyanisScribe::Base::Error{dropped.error().code, GyanisScribe::Base::uiTextUtf8("桌面已还原，但清场标记没有清掉；下次启动会再试一次恢复")});
        }
        m_settings.sync();
        return {};
    }

    bool SceneCleanupGuard::isCleared() const noexcept
    {
        return m_cleared;
    }

} // namespace GyanisScribe::App
