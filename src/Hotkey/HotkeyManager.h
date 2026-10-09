/**
 * @file HotkeyManager.h
 * @brief 把设置里的组合键登记成全局热键，并把触发事件转成动作
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Hotkey/IHotkeyManager.h"

#include <array>
#include <span>
#include <memory>
#include <optional>
#include <string_view>

namespace GyanisScribe::Platform
{
    class IHotkeyBackend;
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Hotkey
{

    /**
     * @brief 热键管理器
     * @details 本类不碰任何操作系统 API：组合键的文本解析在这里，取值转换关在平台后端里，
     *          因此容器里能用一份桩后端把整条登记逻辑跑通。
     * @note 后端在第一次 reload() 时才创建——Windows 那份要建一个隐藏窗口领 HWND，
     *       而那一步要求 QGuiApplication 已经在场；构造管理器本身不做任何带平台副作用的事。
     */
    class HotkeyManager : public IHotkeyManager
    {
        Q_OBJECT

    public:
        /**
         * @brief 绑定设置与平台集成
         * @param settings 配置来源，生命周期必须长于本管理器：热键文本与变更通知都从它取
         * @param integration 平台集成，生命周期必须长于本管理器：后端由它创建
         * @param parent QObject 父对象，默认无
         */
        HotkeyManager(Settings::ISettingsStore &settings, Platform::IPlatformIntegration &integration, QObject *parent = nullptr);

        /**
         * @brief 析构：注销全部热键并交出后端对象
         */
        ~HotkeyManager() override;

        HotkeyManager(const HotkeyManager &)            = delete;
        HotkeyManager &operator=(const HotkeyManager &) = delete;

        /**
         * @brief 按当前设置重登全部热键，见 IHotkeyManager::reload()
         * @return 本轮结果；后端不支持或个别组合被占用都会体现在 failures 里，不提前返回
         * @details 重写基类接口：逐项独立登记——一个动作失败不影响其余动作，
         *          早退会让用户看到「另一些热键也没反应」这种更难查的现场。
         */
        [[nodiscard]] Core::Expected<HotkeyReloadReport> reload() override;

        /**
         * @brief 取动作当前的组合文本，见 IHotkeyManager::bindingText()
         * @param action 动作
         * @return 设置里的文本；没设过、或设的不是合法组合时回落到默认组合的文本
         */
        [[nodiscard]] QString bindingText(HotkeyAction action) const override;

    private:
        /// 全部受管动作，顺序与登记顺序一致，是「有几项」的唯一出处
        [[nodiscard]] static std::span<const HotkeyAction> managedActions();

        /// 动作对应的设置键
        [[nodiscard]] static std::string_view settingsKeyFor(HotkeyAction action);

        /// 动作对应的默认组合文本
        [[nodiscard]] static std::string_view defaultCombinationTextFor(HotkeyAction action);

        /// 动作对应的后端编号
        [[nodiscard]] static int identifierFor(HotkeyAction action);

        /// 编号反查动作，不在册时返回空
        [[nodiscard]] static std::optional<HotkeyAction> actionForIdentifier(int identifier);

        Settings::ISettingsStore                 &m_settings;    ///< 组合文本与变更通知的来源
        Platform::IPlatformIntegration           &m_integration; ///< 后端工厂
        std::unique_ptr<Platform::IHotkeyBackend> m_backend;     ///< 当前后端，第一次 reload() 时建立
    };

} // namespace GyanisScribe::Hotkey
