/**
 * @file WindowsStepEventSource.h
 * @brief 基于低级鼠标钩子与前台窗口事件的步骤事件源
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Platform/IStepEventSource.h"

#include <QPoint>

#include <windows.h>

namespace GyanisScribe::Platform
{

    /**
     * @brief Windows 的步骤事件源：全局鼠标点击与前台窗口切换
     * @details 鼠标走 WH_MOUSE_LL 低级钩子，窗口切换走 SetWinEventHook 的 EVENT_SYSTEM_FOREGROUND。
     *          两个回调都只做过滤并把事件排队投递回本对象，抓屏与 UI Automation 由消费方在 GUI 线程另做。
     * @note 同一线程只允许一份运行中的实例：回调是静态函数，归属靠静态指针区分。
     *       安装、摘除与 isRunning() 都只在 GUI 线程调用。
     */
    class WindowsStepEventSource : public IStepEventSource
    {
    public:
        /**
         * @brief 构造事件源，钩子要等 start() 才装
         */
        WindowsStepEventSource() = default;

        /**
         * @brief 析构时确保钩子已摘除
         * @details 调用方漏了 stop() 也不能让钩子活过本对象：回调会打到已释放的实例上
         */
        ~WindowsStepEventSource() override;

        WindowsStepEventSource(const WindowsStepEventSource &)            = delete;
        WindowsStepEventSource &operator=(const WindowsStepEventSource &) = delete;

        /**
         * @brief 装上全局鼠标钩子与前台窗口事件钩子，见 IStepEventSource::start()
         * @return 成功返回空载荷；本线程已有运行中的事件源时返回 Busy，系统拒绝安装钩子时返回 InternalError
         * @details 重写基类接口：鼠标钩子把左/右键的按下与抬起配成一次点击，窗口钩子只报前台窗口变化。
         */
        [[nodiscard]] Base::Expected<void> start() override;

        /**
         * @brief 摘下两个钩子并清空归属，见 IStepEventSource::stop()
         * @details 重写基类接口：幂等，重复调用不碰任何句柄。
         */
        void stop() override;

        /**
         * @brief 两个钩子是否都装上了，见 IStepEventSource::isRunning()
         * @return 正在监听返回 true
         */
        [[nodiscard]] bool isRunning() const noexcept override;

    private:
        /**
         * @brief WH_MOUSE_LL 的回调
         * @param nCode 钩子码；HC_ACTION 之外的取值必须原样交回系统
         * @param wParam 鼠标消息，只处理左/右键的按下与抬起
         * @param lParam 指向 MSLLHOOKSTRUCT 的裸指针
         * @return 始终交回 CallNextHookEx：本钩子只旁听，不消费输入
         * @details 注入事件与落在本进程窗口上的点击直接跳过；抬起时与按下点相差不超过 6 个逻辑像素才算点击。
         *          回调里不抓屏、不调 UI Automation，只把事件排队投递回 GUI 线程。
         */
        static LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam);

        /**
         * @brief SetWinEventHook 的回调
         * @param hook 钩子句柄，本实现不用
         * @param event 事件编号，只关心 EVENT_SYSTEM_FOREGROUND
         * @param window 事件落点窗口；空句柄不算
         * @param objectId 对象编号，本实现不用
         * @param childId 子元素编号，本实现不用
         * @param eventThread 事件所属线程，本实现不用
         * @param eventTime 事件时刻，本实现不用
         * @details 本进程自己的窗口切到前台（记录面板、HUD）直接跳过；其余排队投递，坐标只作占位。
         */
        static void CALLBACK winEventProc(HWINEVENTHOOK hook, DWORD event, HWND window, LONG objectId, LONG childId, DWORD eventThread, DWORD eventTime);

        HHOOK         m_mouseHook    = nullptr; ///< WH_MOUSE_LL 钩子句柄，未装时为 nullptr
        HWINEVENTHOOK m_winEventHook = nullptr; ///< 前台窗口事件钩子句柄，未装时为 nullptr
        bool          m_running      = false;   ///< 两个钩子是否都装上了

        QPoint m_pressedPoint;         ///< 最近一次按下时的全局逻辑坐标
        bool   m_pressedRight = false; ///< 最近一次按下的是不是右键
        bool   m_pressedValid = false; ///< 有没有等待抬起的按下事件

        static WindowsStepEventSource *s_mouseOwner;  ///< 当前线程里 WH_MOUSE_LL 的归属实例
        static WindowsStepEventSource *s_windowOwner; ///< 当前线程里前台窗口事件的归属实例
    };

} // namespace GyanisScribe::Platform
