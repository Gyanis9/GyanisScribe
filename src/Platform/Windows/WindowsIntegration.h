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

struct IUIAutomation;

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
         * @return 全局热键、鼠标穿透、取前台窗口、枚举可见窗口与界面元素检测五项均为真
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
        [[nodiscard]] Base::Expected<ForegroundWindow> foregroundWindow() const override;

        /**
         * @brief 枚举屏上可见窗口，见 IPlatformIntegration::screenWindows()
         * @return 按 z 序（从上到下）排好的窗口快照，坐标已是逻辑全局读数
         * @details EnumWindows 给的矩形带几条看不见的可调边框，这里优先取 DWM 的实际可见边界；
         *          被 DWM 藏起来（切到别的虚拟桌面）与工具窗都不进候选。
         * @note 一块屏都配不上名时返回 ScreenUnavailable：宁可不给候选，也不拿另一块屏的缩放因子硬算
         */
        [[nodiscard]] Base::Expected<std::vector<ScreenWindow>> screenWindows() const override;

        /**
         * @brief 光标处的界面元素，见 IPlatformIntegration::elementChainAtPoint()
         * @param globalLogicalPoint 全局逻辑坐标
         * @param excludeWindowHandles 命中到这几扇窗就直接跳过，往下找下一扇
         * @return 从深到浅的元素矩形（逻辑全局）；光标下没有可认的窗口、或那扇窗没有 UI Automation 提供器时交空一份
         * @details 先用 EnumWindows 的 z 序认「第一扇含住光标、不在排除名单里、看得见的窗」——
         *          UIA 的 ElementFromPoint 穿不过置顶窗（本机探针实测），这一步是唯一能拿到遮罩底下的走法；
         *          再从这扇窗的 UIA 根元素按「子元素含住点」逐级下钻。工具窗在这一轮里不排除（与窗口候选不同）：
         *          这里问的是「光标下看得见的是什么」，浮动面板、贴图这类工具窗就摆在屏幕上。
         */
        [[nodiscard]] Base::Expected<std::vector<QRect>> elementChainAtPoint(const QPoint &globalLogicalPoint, const std::vector<quintptr> &excludeWindowHandles) const override;

        /**
         * @brief 查开机启动，见 IPlatformIntegration::autostartEnabled()
         * @return 成功返回有没有为「当前这一份可执行体」登记开机启动
         * @details 重写接口：写的是 HKCU 的 Run 键，值指着别的副本路径时按「本份没开」算，
         *          因为这一格问的是「这台机器开机时会不会起我这一份」。
         */
        [[nodiscard]] Base::Expected<bool> autostartEnabled() const override;

        /**
         * @brief 开关开机启动，见 IPlatformIntegration::setAutostart()
         * @param enabled true 写入 Run 键，false 摘掉那一条
         * @return 成功返回空载荷；取不到自身路径或注册表写不进去时返回中文原因
         */
        [[nodiscard]] Base::Expected<void> setAutostart(bool enabled) override;

    private:
        /**
         * @brief 惰性取 UI Automation 入口对象
         * @return 拿到返回该对象（本类持有，生命周期随对象）；起不来返回空指针
         * @details 悬停每动一格都会问一次，COM 对象不能每次重建；GUI 线程是 STA，
         *          缓存给同一个线程用是安全的。
         */
        [[nodiscard]] IUIAutomation *uiAutomation() const;

        mutable IUIAutomation *m_uia = nullptr; ///< UI Automation 入口，按需创建、析构时放掉
    };

} // namespace GyanisScribe::Platform
