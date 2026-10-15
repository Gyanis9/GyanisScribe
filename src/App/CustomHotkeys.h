/**
 * @file CustomHotkeys.h
 * @brief 固定动作之外那批功能的可绑清册：标识、配置键与从配置收集出来的一批绑定
 * @author Gyanis
 * @date 2026-10-14
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "App/TrayController.h"
#include "Hotkey/IHotkeyManager.h"

#include <QString>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace GyanisScribe::Core
{
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::App
{

    /**
     * @brief 一个能再绑一条全局热键的功能
     * @details 只带标识与动作两格：名字从托盘那张表上找（同一件事在两处各写一遍，改一处就会分家）。
     */
    struct BindableAction
    {
        TrayAction       action; ///< 点了这个热键要走的托盘动作
        std::string_view token;  ///< 词表里的标识，进配置键名、也进热键层的报回
    };

    /**
     * @brief 全部可绑的功能，顺序就是快捷键页上那一段的顺序
     * @details 固定那八个动作不在这里：它们各有自己的键与行，再列一遍会出现「同一件事两处都能改」。
     *          退出与打开设置/关于也不在这里——一条全局热键悄悄把程序带走，是比没绑更糟的入口。
     */
    [[nodiscard]] std::span<const BindableAction> bindableActions();

    /**
     * @brief 功能标识对应的配置键
     * @param token 标识
     * @return 形如 Hotkey/Custom/ManagePinGroups；组合文本就存这一格，空文本即「不绑」
     */
    [[nodiscard]] QString customHotkeyKey(std::string_view token);

    /**
     * @brief 标识反查功能
     * @param token 热键层报回来的标识
     * @return 对应的托盘动作；词表里没有（旧配置留下的）返回空
     */
    [[nodiscard]] std::optional<TrayAction> trayActionForToken(const QString &token);

    /**
     * @brief 功能在界面上的名字（与托盘菜单那一项同一句）
     * @param action 哪个功能
     * @return 现成的标签；托盘表上没有这一项时返回空串
     */
    [[nodiscard]] QString bindableActionLabel(TrayAction action);

    /**
     * @brief 从配置里收集这一批要登记的自定义绑定
     * @param settings 配置存储
     * @return 组合非空的那些；顺序就是 bindableActions() 的顺序（编号按下标算，两边必须同序）
     */
    [[nodiscard]] std::vector<Hotkey::CustomHotkeyBinding> collectCustomBindings(const Core::ISettingsStore &settings);

} // namespace GyanisScribe::App
