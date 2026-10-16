/**
 * @file Win32HotkeyBackend.h
 * @brief 基于 RegisterHotKey 的全局热键后端
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Platform/IHotkeyBackend.h"

#include <QAbstractNativeEventFilter>

#include <memory>
#include <unordered_set>

class QWindow;

namespace GyanisScribe::Platform
{

    /**
     * @brief Windows 全局热键后端
     * @details 用一个不显示的 QWindow 领 HWND：RegisterHotKey 需要一个能收 WM_HOTKEY 的窗口，
     *          而本应用可以完全没有可见窗口（托盘模式），所以自己养一个接收用的那一个。
     *          消息经 QCoreApplication 的原生事件过滤器交回，不额外起线程也不轮询。
     * @warning 必须在 QGuiApplication 存在之后构造：winId() 要建平台窗口，没有应用对象时拿不到 HWND。
     *          因此本类的实机登记/触发不在单元用例里跑（会真的抢走用户机器上的组合键），
     *          由「验收」那一轮在主机上逐条按下核对；纯映射部分见 Win32HotkeyMapping 的用例。
     */
    class Win32HotkeyBackend : public IHotkeyBackend, public QAbstractNativeEventFilter
    {
    public:
        /**
         * @brief 建出接收窗口并挂上原生事件过滤器
         */
        Win32HotkeyBackend();

        /**
         * @brief 析构：先摘过滤器再注销全部热键，避免过滤器回调打到已开始析构的对象上
         */
        ~Win32HotkeyBackend() override;

        Win32HotkeyBackend(const Win32HotkeyBackend &)            = delete;
        Win32HotkeyBackend &operator=(const Win32HotkeyBackend &) = delete;

        /**
         * @brief 登记一个热键，见 IHotkeyBackend::registerHotkey()
         * @param identifier 本后端内的编号
         * @param modifiers Qt 修饰键集合
         * @param key 按键本体
         * @return 成功返回空载荷
         * @return 失败返回中文原因：取值出不了 Win32 词表是 InvalidArgument，编号重复是 InvalidArgument，
         *         系统拒绝（多为已被占用）是 Busy 并带错误码
         * @details 重写基类接口：在这里做 Qt → Win32 的取值转换（见 Win32HotkeyMapping），
         *          转换不过一律拒绝而不是挑一个相近的键顶上——静默换键会让界面上写的组合与实际生效的组合不一致。
         */
        [[nodiscard]] Base::Expected<void> registerHotkey(int identifier, Qt::KeyboardModifiers modifiers, Qt::Key key) override;

        /**
         * @brief 注销一个热键，见 IHotkeyBackend::unregisterHotkey()
         * @param identifier 登记时那个编号
         * @return 成功返回空载荷；没登记过返回 InvalidArgument，系统拒绝返回 Busy 并带错误码
         */
        [[nodiscard]] Base::Expected<void> unregisterHotkey(int identifier) override;

        /**
         * @brief 注销全部已登记的热键，见 IHotkeyBackend::unregisterAll()
         * @details 逐条注销且不去重计数：残留一条就会一直占着那个组合键。
         */
        void unregisterAll() override;

        /**
         * @brief 原生消息过滤，把 WM_HOTKEY 换成 activated 信号
         * @param eventType 平台事件类型，Windows 上是 windows_generic_MSG
         * @param message 指向 MSG 的裸指针
         * @param result 交给系统的返回值，命中时置 0
         * @return true 表示消息已消费，Qt 不再继续派发
         * @return false 表示与本后端无关
         */
        bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

    private:
        /// 领 WM_HOTKEY 用的那个隐藏窗口
        std::unique_ptr<QWindow> m_sinkWindow;

        /// 已登记的编号集合，析构与重载时按它逐条注销
        std::unordered_set<int> m_registeredIdentifiers;
    };

} // namespace GyanisScribe::Platform
