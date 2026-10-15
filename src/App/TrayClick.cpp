#include "App/TrayClick.h"

#include "App/TrayController.h"
#include "Base/Text.h"
#include "Platform/PlatformCapabilities.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"

#include <QString>

#include <algorithm>
#include <array>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <vector>

namespace GyanisScribe::App
{
    constexpr std::string_view trayActionToken(const TrayAction action) noexcept
    {
        // 一张开关把 20 个动作的标记逐个点名：漏一支就有一档在设置页上摆得出去、配置里却认不出来
        switch (action)
        {
            case TrayAction::CaptureRegion:
                return "CaptureRegion";
            case TrayAction::CaptureFullScreen:
                return "CaptureFullScreen";
            case TrayAction::CaptureLastRegion:
                return "CaptureLastRegion";
            case TrayAction::CaptureWindow:
                return "CaptureWindow";
            case TrayAction::ToggleRecord:
                return "ToggleRecord";
            case TrayAction::Recognize:
                return "Recognize";
            case TrayAction::PickColor:
                return "PickColor";
            case TrayAction::OpenSettings:
                return "OpenSettings";
            case TrayAction::OpenAbout:
                return "OpenAbout";
            case TrayAction::RecognizeFile:
                return "RecognizeFile";
            case TrayAction::Quit:
                return "Quit";
            case TrayAction::CloseAllPins:
                return "CloseAllPins";
            case TrayAction::CaptureLongRegion:
                return "CaptureLongRegion";
            case TrayAction::ClearPinsThrough:
                return "ClearPinsThrough";
            case TrayAction::TogglePinsVisible:
                return "TogglePinsVisible";
            case TrayAction::PasteClipboard:
                return "PasteClipboard";
            case TrayAction::NextPinGroup:
                return "NextPinGroup";
            case TrayAction::BlankCanvas:
                return "BlankCanvas";
            case TrayAction::RepinClosed:
                return "RepinClosed";
            case TrayAction::ManagePinGroups:
                return "ManagePinGroups";
        }
        // 开关已经覆盖全部取值，这一行只回答编译器「还有一条路没返回」：走不到，也不该静默给个别的标记
        return {};
    }

    namespace
    {
        // 两格哨兵不在动作词表里：它们说的不是「做菜单上的哪件事」，而是「这一下不做」与「这一下弹菜单」
        constexpr std::string_view kNoneToken = "none";
        constexpr std::string_view kMenuToken = "menu";

        // 默认那两下就是这一批要交付的形状：左键框选截图、中键收起 / 摆回全部贴图。
        // 这两下在本仓此前都不做事，所以默认值改在 README 里写明，要旧形状就在清单里显式选「什么都不做」
        constexpr std::array<TrayClickRoleInfo, 2> kRoleInfos = {
                TrayClickRoleInfo{GyanisScribe::Settings::kTrayLeftClick, "左键单击", TrayClickRole::Left, trayActionToken(TrayAction::CaptureRegion)},
                TrayClickRoleInfo{GyanisScribe::Settings::kTrayMiddleClick, "中键单击", TrayClickRole::Middle, trayActionToken(TrayAction::TogglePinsVisible)},
        };

        /**
         * @brief 拼一份可选清单
         * @param capabilities 平台能力表；空指针表示不按能力过滤（读配置时用，配置可能是从别的机器抄来的）
         * @return 首两格是哨兵，其后按托盘菜单的顺序排
         */
        [[nodiscard]] std::vector<TrayClickChoice> buildChoices(const Platform::PlatformCapabilities *capabilities)
        {
            std::vector<TrayClickChoice> choices;
            choices.push_back(TrayClickChoice{kNoneToken, GyanisScribe::Base::uiText("什么都不做"), TrayClickOutcome::Nothing, std::nullopt});
            choices.push_back(TrayClickChoice{kMenuToken, GyanisScribe::Base::uiText("弹出托盘菜单"), TrayClickOutcome::ShowMenu, std::nullopt});

            const auto entries = capabilities ? visibleTrayMenuEntries(*capabilities) : trayMenuEntries();
            for (const auto &entry: entries)
            {
                // 分隔线没有动作；退出也不给绑：手一滑就把程序带走，是比没绑更糟的入口，与全局热键那张表同一个判据
                if (!entry.action || *entry.action == TrayAction::Quit)
                {
                    continue;
                }
                choices.push_back(TrayClickChoice{trayActionToken(*entry.action), entry.label, TrayClickOutcome::RunAction, entry.action});
            }
            return choices;
        }
    } // namespace

    std::span<const TrayClickRoleInfo> trayClickRoles()
    {
        return kRoleInfos;
    }

    const TrayClickRoleInfo &trayClickRoleInfo(const TrayClickRole role)
    {
        switch (role)
        {
            case TrayClickRole::Left:
                return kRoleInfos[0];
            case TrayClickRole::Middle:
                return kRoleInfos[1];
        }
        // 开关覆盖了两档，这一行是给编译器的答复；走到这里说明调用方递来一个词表外的值
        return kRoleInfos[0];
    }

    std::optional<TrayClickRole> trayClickRoleForReason(const QSystemTrayIcon::ActivationReason reason)
    {
        switch (reason)
        {
            case QSystemTrayIcon::Trigger:
                return TrayClickRole::Left;
            case QSystemTrayIcon::MiddleClick:
                return TrayClickRole::Middle;
            default:
                // 右键那一下由平台自己弹菜单，双击在第一下松开时就报过单击（再算一档会把遮罩重铺一遍），
                // Unknown 不是鼠标：这三种都不归这两档管，返回空而不是「什么都不做」
                return std::nullopt;
        }
    }

    std::vector<TrayClickChoice> trayClickChoices(const Platform::PlatformCapabilities &capabilities)
    {
        return buildChoices(&capabilities);
    }

    TrayClickDecision resolveTrayClick(const Settings::ISettingsStore &settings, const TrayClickRole role, std::string &complaint)
    {
        complaint.clear();

        const auto &info    = trayClickRoleInfo(role);
        const auto  choices = buildChoices(nullptr);
        const auto  apply   = [&choices](const std::string_view token)
        {
            // 默认串与清单同源，找不回来说明两张表分了家：这种情况不吭声也不能崩，退成「什么都不做」
            const auto found = std::ranges::find(choices, token, &TrayClickChoice::token);
            if (found == choices.end())
            {
                return TrayClickDecision{};
            }
            return TrayClickDecision{found->outcome, found->action, found->token};
        };

        // 空文本按「没填」处理且不吭声：这一格没有「清空即关闭」的语义，要关掉这一下得显式选「什么都不做」
        const auto stored = settings.stringValue(info.key, QString::fromUtf8(info.defaultToken.data())).trimmed().toStdString();
        if (stored.empty())
        {
            return apply(info.defaultToken);
        }

        const auto found = std::ranges::find(choices, std::string_view{stored}, &TrayClickChoice::token);
        if (found != choices.end())
        {
            return TrayClickDecision{found->outcome, found->action, found->token};
        }

        // 填坏了落回默认并点名那串原文：夹成「看着最像的那一档」会把用户填错的东西读成一个他没选过的动作
        complaint = std::format("托盘{}那一下填的「{}」不是可用标记。可用的是 none、menu 与托盘菜单上那些动作的标记（比如 {}），这一档按默认的 {} 走", std::string(info.label),
                                stored, std::string(info.defaultToken), std::string(info.defaultToken));
        return apply(info.defaultToken);
    }

} // namespace GyanisScribe::App
