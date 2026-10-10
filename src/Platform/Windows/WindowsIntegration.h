/**
 * @file WindowsIntegration.h
 * @brief Windows 平台集成
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
     * @brief Windows 那一份实现
     * @details 能力表按操作系统给得出什么来填，与本仓实现进度无关：缺实现的项由对应模块自己报
     *          「尚未适配」，不在这里假装平台没有。
     */
    class WindowsIntegration : public IPlatformIntegration
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造集成对象
         */
        WindowsIntegration();

        /**
         * @brief 析构集成对象
         */
        ~WindowsIntegration() override;

        /**
         * @brief 取 Windows 能力表，见 IPlatformIntegration::capabilities()
         * @return 全局热键、鼠标穿透、取前台窗口与枚举可见窗口四项均为真
         */
        [[nodiscard]] PlatformCapabilities capabilities() const noexcept override;

        /**
         * @brief 取平台显示名，见 IPlatformIntegration::displayName()
         * @return 「Windows」
         */
        [[nodiscard]] QString displayName() const override;

        /**
         * @brief 创建 RegisterHotKey 那一份后端，见 IPlatformIntegration::createHotkeyBackend()
         * @return 新的 Win32 后端，所有权交调用方
         * @warning 返回的对象要在 QGuiApplication 之后使用
         */
        [[nodiscard]] std::unique_ptr<IHotkeyBackend> createHotkeyBackend() override;

        /**
         * @brief 取前台窗口，见 IPlatformIntegration::foregroundWindow()
         * @return GetForegroundWindow 给得出的句柄与标题栏文字（拿不到标题时为空串，仍算成功）
         * @details 没有前台窗口（会话锁屏、开机后还没人点过窗口）时返回 ScreenUnavailable，
         *          文案给的是「请先点一下要截的那个窗口」这类可操作的下一步
         */
        [[nodiscard]] Core::Expected<ForegroundWindow> foregroundWindow() const override;

        /**
         * @brief 枚举屏上可见窗口，见 IPlatformIntegration::screenWindows()
         * @return 按 z 序（从上到下）排好的窗口快照，坐标已是逻辑全局读数
         * @details EnumWindows 给的矩形带几条看不见的可调边框，这里优先取 DWM 的实际可见边界；
         *          被 DWM 藏起来（切到别的虚拟桌面）与工具窗都不进候选。
         * @note 一块屏都配不上名时返回 ScreenUnavailable：宁可不给候选，也不拿另一块屏的缩放因子硬算
         */
        [[nodiscard]] Core::Expected<std::vector<ScreenWindow>> screenWindows() const override;
    };

} // namespace GyanisScribe::Platform
