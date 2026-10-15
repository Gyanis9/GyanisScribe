/**
 * @file TrayClick.h
 * @brief 托盘图标上的鼠标动作：哪一下算哪一档、那一档在配置里存什么、读出来落成什么
 * @author Gyanis
 * @date 2026-10-14
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "App/TrayAction.h"

#include <QSystemTrayIcon>

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace GyanisScribe::Platform
{
    struct PlatformCapabilities;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::Core
{
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::App
{

    /**
     * @brief 托盘图标上能各自设一档的那几下鼠标
     * @details 右键不在这里：那是平台自己弹菜单的那一下，做成一档会让用户以为换了它就不出菜单。
     *          双击也不做一档：一次双击在第一下松开时已经报过单击，第二下落在谁的窗口上要看法术顺序，
     *          两档并存只会互相打断（截图这一档会把刚铺好的遮罩重铺一遍）。
     */
    enum class TrayClickRole
    {
        Left,   ///< 左键单击
        Middle, ///< 中键单击（按下滚轮）
    };

    /**
     * @brief 一档落下来到底做什么
     */
    enum class TrayClickOutcome
    {
        Nothing,   ///< 什么都不做：本仓此前的形状
        ShowMenu,  ///< 把托盘菜单弹出来
        RunAction, ///< 走与菜单项、全局热键同一个出口
    };

    /**
     * @brief 一档的静态描述：配置键、界面名与默认那串标记
     */
    struct TrayClickRoleInfo
    {
        std::string_view key;          ///< 配置键，形如 Tray/LeftClick
        std::string_view label;        ///< 设置页那一行的名字
        TrayClickRole    role;         ///< 哪一下鼠标
        std::string_view defaultToken; ///< 没填或填坏时落回的那一串
    };

    /**
     * @brief 设置页上可选的一格
     */
    struct TrayClickChoice
    {
        std::string_view          token;   ///< 存进配置的那一串
        QString                   label;   ///< 界面上的那一格文字
        TrayClickOutcome          outcome; ///< 选了它就做什么
        std::optional<TrayAction> action;  ///< 只有 RunAction 那几档带动作，两格哨兵没有
    };

    /**
     * @brief 从配置里读出的一档决定
     */
    struct TrayClickDecision
    {
        TrayClickOutcome          outcome = TrayClickOutcome::Nothing; ///< 做什么
        std::optional<TrayAction> action;                              ///< 只有 RunAction 带动作
        std::string_view          token{};                             ///< 最终作数的那一串标记
    };

    /**
     * @brief 全部可设档，顺序就是设置页上那几行的顺序
     * @return 两档：左键单击与中键单击
     */
    [[nodiscard]] std::span<const TrayClickRoleInfo> trayClickRoles();

    /**
     * @brief 按档找它的静态描述
     * @param role 哪一下鼠标
     * @return 那一档的键名、界面名与默认标记
     */
    [[nodiscard]] const TrayClickRoleInfo &trayClickRoleInfo(TrayClickRole role);

    /**
     * @brief 托盘报回的激活原因算哪一档
     * @param reason Qt 的激活原因
     * @return 左键单击与中键单击之外（含右键、双击与未知）返回空
     * @details 空就是「这一下不归本仓管」，不是「这一下什么都不做」：右键那一档由平台自己接管，
     *          把两种含义并成一格，将来加一档时就分不出该不该发信号。
     */
    [[nodiscard]] std::optional<TrayClickRole> trayClickRoleForReason(QSystemTrayIcon::ActivationReason reason);

    /**
     * @brief 托盘动作的标记词表：与 TrayAction 的枚举名同串
     * @param action 哪个动作
     * @return 存进配置的那一串
     * @details 与 CustomHotkeys 那张表同一个口径（那边的 token 就是枚举名），同一件事在两处各起一个别名，
     *          复制配置到另一台机器时就会有一处认不出另一处写的值。
     */
    [[nodiscard]] constexpr std::string_view trayActionToken(TrayAction action) noexcept;

    /**
     * @brief 设置页上那一格能选的清单
     * @param capabilities 平台能力表，做不出来的动作不列进来
     * @return 首两格是「什么都不做」与「弹出托盘菜单」，其后按托盘菜单的顺序排
     * @details 退出不在清单里：手一滑就把程序带走，是比没绑更糟的入口，与全局热键那张表同一个判据。
     */
    [[nodiscard]] std::vector<TrayClickChoice> trayClickChoices(const Platform::PlatformCapabilities &capabilities);

    /**
     * @brief 读出这一档现在该做什么
     * @param settings 配置存储
     * @param role 哪一下鼠标
     * @param complaint 出参：读坏时写进原因，没读坏时留空
     * @return 做什么、哪个动作、最终作数的那串标记
     * @details 空文本（没填）与填坏了两重含义分开：没填按默认档走且不吭声，填坏了要回一句原因并把这一档
     *          落回默认，而不是悄悄当成没填——填坏的用户需要知道自己填的那一串为什么没生效。
     */
    [[nodiscard]] TrayClickDecision resolveTrayClick(const Core::ISettingsStore &settings, TrayClickRole role, std::string &complaint);

} // namespace GyanisScribe::App
