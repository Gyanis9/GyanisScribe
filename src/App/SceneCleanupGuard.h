/**
 * @file SceneCleanupGuard.h
 * @brief 清场与恢复的唯一出口：标记、调用平台接口、析构兜底
 * @author Gyanis
 * @date 2026-11-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

namespace GyanisScribe::Core
{
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::App
{

    /**
     * @brief 清场守卫
     * @details 清场前写标记、恢复成功后清掉；标记残留说明上次没收尾，启动时由控制器补一次恢复。
     *          析构只为本对象清过的那一场兜底；只有标记残留的临时守卫已经显式恢复过，
     *          失败后析构不再重试，免得把平台上的恢复翻成最小化。restore() 自身仍幂等：
     *          未清场且没有标记时直接返回成功。
     */
    class SceneCleanupGuard
    {
    public:
        /**
         * @brief 绑定依赖
         * @param integration 平台能力来源
         * @param settings 配置来源，存清场标记
         */
        SceneCleanupGuard(GyanisScribe::Platform::IPlatformIntegration &integration, GyanisScribe::Core::ISettingsStore &settings);

        /// 析构兜底：还处于清场态就恢复一次
        ~SceneCleanupGuard();

        SceneCleanupGuard(const SceneCleanupGuard &)            = delete;
        SceneCleanupGuard &operator=(const SceneCleanupGuard &) = delete;

        /**
         * @brief 进入清场
         * @return 成功返回空；平台不支持或拿不到桌面句柄时返回中文原因
         * @details 失败时补发一次恢复把半边动作撤掉，并且不写标记。
         */
        [[nodiscard]] Base::Expected<void> clear();

        /**
         * @brief 恢复
         * @return 成功返回空；失败时保留标记供下次启动再试
         * @details 未处于清场态时直接返回成功：重复调用没有副作用。
         */
        [[nodiscard]] Base::Expected<void> restore();

        /// 当前是否处于清场态
        [[nodiscard]] bool isCleared() const noexcept;

    private:
        GyanisScribe::Platform::IPlatformIntegration &m_integration;     ///< 平台能力来源，不拥有
        GyanisScribe::Core::ISettingsStore           &m_settings;        ///< 配置来源，不拥有
        bool                                          m_cleared = false; ///< 是否已清场
    };

} // namespace GyanisScribe::App
