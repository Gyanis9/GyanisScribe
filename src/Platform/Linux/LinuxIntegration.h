/**
 * @file LinuxIntegration.h
 * @brief Linux 平台集成（降级实现）
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Platform/IPlatformIntegration.h"

namespace GyanisScribe::Platform
{

    /**
     * @brief Linux 那一份实现
     * @details 目的是让容器里能编能测核心逻辑，而不是把桌面功能补齐：全局热键交桩后端，
     *          录屏与 OCR 走命令行工具那一路（能力位如实反映，缺的项在界面给中文原因）。
     */
    class LinuxIntegration : public IPlatformIntegration
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造集成对象
         */
        LinuxIntegration();

        /**
         * @brief 析构集成对象
         */
        ~LinuxIntegration() override;

        /**
         * @brief 取 Linux 能力表，见 IPlatformIntegration::capabilities()
         * @return 只有录屏采集为真（经命令行采集器），其余四项为假
         * @note X11 与 Wayland 的差异不在这张表里细分：屏幕采集由采集器自己探，探不到就报 DependencyMissing
         */
        [[nodiscard]] PlatformCapabilities capabilities() const noexcept override;

        /**
         * @brief 取平台显示名，见 IPlatformIntegration::displayName()
         * @return 「Linux（降级实现）」
         */
        [[nodiscard]] QString displayName() const override;

        /**
         * @brief 创建热键后端，见 IPlatformIntegration::createHotkeyBackend()
         * @return UnsupportedHotkeyBackend：本平台没有全局热键，登记一律回 PlatformUnsupported
         */
        [[nodiscard]] std::unique_ptr<IHotkeyBackend> createHotkeyBackend() override;
    };

} // namespace GyanisScribe::Platform
