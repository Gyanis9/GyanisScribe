/**
 * @file HotkeyManager.h
 * @brief 把设置里的组合键登记成全局热键，并把触发事件转成动作
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Hotkey/IHotkeyManager.h"

#include <array>
#include <span>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace GyanisScribe::Platform
{
    class IHotkeyBackend;
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::Core
{
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Core
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
        HotkeyManager(Core::ISettingsStore &settings, Platform::IPlatformIntegration &integration, QObject *parent = nullptr);

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
        [[nodiscard]] Base::Expected<HotkeyReloadReport> reload() override;

        /**
         * @brief 取动作当前的组合文本，见 IHotkeyManager::bindingText()
         * @param action 动作
         * @return 设置里的文本；没设过、或设的不是合法组合时回落到默认组合的文本
         */
        [[nodiscard]] QString bindingText(HotkeyAction action) const override;

        /**
         * @brief 配置键对应的默认组合文本
         * @param settingsKey 配置键，例如 Hotkey/CaptureLong
         * @return 默认组合文本；键不在受管动作里时返回空串
         * @details 菜单这类不持有热键管理器的界面要显示「没设过时该按什么」：
         *          默认表只有本类那一处真源，界面自己按动作类型兜底就会把长截图、
         *          取色、收起贴图与切组四行都显示成截图那颗键。
         */
        [[nodiscard]] static QString defaultCombinationTextForSettingsKey(std::string_view settingsKey);

        /**
         * @brief 换一批自定义绑定，见 IHotkeyManager::setCustomBindings()
         * @param bindings 要登记的那一批（组合非空、标识由调用方自己认领）
         */
        void setCustomBindings(std::vector<CustomHotkeyBinding> bindings) override;

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

        /// 自定义绑定的编号起点：固定动作按「枚举序号 + 1」只占到个位数，这一段留给自定义，两边不会撞号
        static constexpr int kCustomIdentifierBase = 1000;

        /// 编号反查自定义绑定的序号（表内下标），不在这批里时返回空
        [[nodiscard]] std::optional<std::size_t> customIndexForIdentifier(int identifier) const;

        Core::ISettingsStore                     &m_settings;       ///< 组合文本与变更通知的来源
        Platform::IPlatformIntegration           &m_integration;    ///< 后端工厂
        std::unique_ptr<Platform::IHotkeyBackend> m_backend;        ///< 当前后端，第一次 reload() 时建立
        std::vector<CustomHotkeyBinding>          m_customBindings; ///< 这一批自定义绑定，顺序就是登记顺序（编号按下标算）
    };

} // namespace GyanisScribe::Core
