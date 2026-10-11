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
         * @return 四项均为假：全局热键、鼠标穿透、「取前台窗口」与「枚举可见窗口」都要与会话协议打交道，本仓没接
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

        /**
         * @brief 取前台窗口，见 IPlatformIntegration::foregroundWindow()
         * @return 一律返回 PlatformUnsupported：X11 要读 _NET_ACTIVE_WINDOW、Wayland 要走会话协议，两样都没接
         * @details 文案里给的是替代做法（在托盘改用「框选截图」把目标窗口框出来），不是只报一句「不支持」
         */
        [[nodiscard]] Core::Expected<ForegroundWindow> foregroundWindow() const override;

        /**
         * @brief 枚举屏上可见窗口，见 IPlatformIntegration::screenWindows()
         * @return 一律返回 PlatformUnsupported：X11 要查窗口树并逐扇窗换算全局坐标，Wayland 不让客户端问别的窗口
         * @note 降级文案给的是「直接框出来」这条替代做法，遮罩上不会摆一枚认不出来的悬停选项
         */
        [[nodiscard]] Core::Expected<std::vector<ScreenWindow>> screenWindows() const override;

        /**
         * @brief 查开机启动，见 IPlatformIntegration::autostartEnabled()
         * @return 本平台一律 PlatformUnsupported，并给出自己加启动项的替代做法
         */
        [[nodiscard]] Core::Expected<bool> autostartEnabled() const override;

        /**
         * @brief 开关开机启动，见 IPlatformIntegration::setAutostart()
         * @param enabled 本平台不使用
         * @return 本平台一律 PlatformUnsupported，原因与上一条同源
         */
        [[nodiscard]] Core::Expected<void> setAutostart(bool enabled) override;
    };

} // namespace GyanisScribe::Platform
