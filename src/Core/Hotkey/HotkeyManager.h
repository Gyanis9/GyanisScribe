/**
 * @file HotkeyManager.h
 * @brief 把设置里的组合键登记成全局热键，并把触发事件转成动作
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Hotkey/HotkeyCombination.h"
#include "Core/Hotkey/IHotkeyManager.h"

#include <array>
#include <map>
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

        /**
         * @brief 把本轮成功恢复的旧组合写回配置，见 IHotkeyManager::persistRestoredBindings()
         * @param report 同一轮 reload() 的结果
         * @details 写回期间压住 valueChanged 触发的重登：写回的值就是此刻已经生效的组合，
         *          再登一遍只会把同一批登记翻来覆去地重建。写不进去的那条只记日志，不弹提示。
         */
        void persistRestoredBindings(const HotkeyReloadReport &report) override;

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

        /// 一项目前真正登记在册的记录
        struct ActiveBinding
        {
            std::optional<QString> token;       ///< 自定义绑定的功能标识；固定动作没有这一格
            HotkeyCombination      combination; ///< 后端此刻收下的组合
        };

        /// 恢复旧组合的结果
        enum class RestoreOutcome
        {
            NoPrevious, ///< 上一轮没有这一条，或这个编号上已经换成别的功能
            Restored,   ///< 旧组合重新登记成功
            Failed,     ///< 这个编号上还是同一个功能，但后端再次拒绝
        };

        /// 恢复旧组合时交回的结果
        struct RestoreResult
        {
            RestoreOutcome    outcome = RestoreOutcome::NoPrevious; ///< 恢复结果
            HotkeyCombination previous;                             ///< 同一功能的旧组合；NoPrevious 时没有意义
        };

        /**
         * @brief 新组合没能登记时，把同一编号、同一功能上一轮生效的组合重新登记回去
         * @param identifier 要恢复的编号
         * @param token 这个编号此刻对应的自定义功能标识；固定动作传空
         * @param previous 上一轮真正在册的登记
         * @param next 本轮在册的登记，恢复成功时写入
         * @return 恢复结果与同一功能的旧组合
         * @details 重登先全注销：不恢复的话，改坏一格就让那颗键彻底失效。
         *          自定义的编号按下标算，增删前一项会让后一项换号，所以只有编号与功能都对得上才恢复，
         *          否则会把上一任功能的组合装到这个编号上。恢复失败不写进在册表——表里的每一条
         *          都必须是后端此刻真收下的组合。
         */
        [[nodiscard]] RestoreResult restorePreviousBinding(int identifier, const std::optional<QString> &token, const std::map<int, ActiveBinding> &previous,
                                                           std::map<int, ActiveBinding> &next);

        Core::ISettingsStore                     &m_settings;       ///< 组合文本与变更通知的来源
        Platform::IPlatformIntegration           &m_integration;    ///< 后端工厂
        std::unique_ptr<Platform::IHotkeyBackend> m_backend;        ///< 当前后端，第一次 reload() 时建立
        std::vector<CustomHotkeyBinding>          m_customBindings; ///< 这一批自定义绑定，顺序就是登记顺序（编号按下标算）
        std::map<int, ActiveBinding>              m_activeBindings; ///< 当前真正登记在册的记录（编号 → 功能标识与组合），重登失败时按它恢复

        bool m_writingRestoredBindings = false; ///< 正在把恢复的旧组合写回配置；此时 valueChanged 不再触发重登
    };

} // namespace GyanisScribe::Core
