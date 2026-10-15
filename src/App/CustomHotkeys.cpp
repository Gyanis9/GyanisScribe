#include "App/CustomHotkeys.h"

#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"

#include <array>

namespace GyanisScribe::App
{
    namespace
    {
        /// 标识的 QString 形状：词表里存的是 ASCII，配置与信号里走 QString
        [[nodiscard]] QString tokenText(const std::string_view token)
        {
            return QString::fromUtf8(token.data(), static_cast<qsizetype>(token.size()));
        }

        // 可绑的那批功能。固定那八个动作各有自己的键与行，这里再列一遍就会出现「同一件事两处都能改」；
        // 退出程序、打开设置与关于也不给绑：一条全局热键悄悄把程序带走，比没绑更糟——贴图还在，托盘也没了
        constexpr std::array<BindableAction, 10> kBindableActions{
                BindableAction{TrayAction::CaptureFullScreen, "CaptureFullScreen"},
                BindableAction{TrayAction::CaptureLastRegion, "CaptureLastRegion"},
                BindableAction{TrayAction::CaptureWindow, "CaptureWindow"},
                BindableAction{TrayAction::RecognizeFile, "RecognizeFile"},
                BindableAction{TrayAction::CloseAllPins, "CloseAllPins"},
                BindableAction{TrayAction::ClearPinsThrough, "ClearPinsThrough"},
                BindableAction{TrayAction::PasteClipboard, "PasteClipboard"},
                BindableAction{TrayAction::BlankCanvas, "BlankCanvas"},
                BindableAction{TrayAction::RepinClosed, "RepinClosed"},
                BindableAction{TrayAction::ManagePinGroups, "ManagePinGroups"},
        };
    } // namespace

    std::span<const BindableAction> bindableActions()
    {
        return kBindableActions;
    }

    QString customHotkeyKey(const std::string_view token)
    {
        // 前缀只有 SettingsKeys.h 那一处：设置页写键、这里算键，两处各写一份就会在改名那天分家
        return QString::fromUtf8(Core::kHotkeyCustomPrefix.data(), static_cast<qsizetype>(Core::kHotkeyCustomPrefix.size())) + tokenText(token);
    }

    std::optional<TrayAction> trayActionForToken(const QString &token)
    {
        for (const auto &entry: kBindableActions)
        {
            if (tokenText(entry.token) == token)
            {
                return entry.action;
            }
        }
        return {};
    }

    QString bindableActionLabel(const TrayAction action)
    {
        // 标签的出处只有托盘那张表：菜单上写什么，快捷键页上就叫什么
        for (const auto &entry: trayMenuEntries())
        {
            if (entry.action && *entry.action == action)
            {
                return entry.label;
            }
        }
        return {};
    }

    std::vector<Core::CustomHotkeyBinding> collectCustomBindings(const Core::ISettingsStore &settings)
    {
        std::vector<Core::CustomHotkeyBinding> bindings;
        for (const auto &entry: kBindableActions)
        {
            const auto text = settings.stringValue(customHotkeyKey(entry.token).toStdString(), QString{}).trimmed();
            if (text.isEmpty())
            {
                // 没绑的功能不登记也不报错：这一批是按「有没有填」来收的，空文本是正常档
                continue;
            }
            bindings.push_back(Core::CustomHotkeyBinding{tokenText(entry.token), text});
        }
        return bindings;
    }

} // namespace GyanisScribe::App
