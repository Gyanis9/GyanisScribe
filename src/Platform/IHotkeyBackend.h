/**
 * @file IHotkeyBackend.h
 * @brief 全局热键后端的契约，把操作系统差异收在这一层
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <Qt>

#include <QObject>

namespace GyanisScribe::Platform
{

    /**
     * @brief 全局热键后端
     * @details 进来的是 Qt 的按键域（修饰键集合 + 按键），平台取值转换由每一份实现自己做：
     *          这样 Hotkey 模块与设置页完全不认识 Win32 的掩码与虚拟键码，容器里也就编不出依赖 Windows 的头。
     * @note 激活事件通过 activated 信号交回，必须在事件循环线程上发出——订阅者会直接驱动界面。
     */
    class IHotkeyBackend : public QObject
    {
        Q_OBJECT

    public:
        using QObject::QObject;

        /**
         * @brief 析构后端，实现方要在析构里注销自己登记的全部热键
         */
        ~IHotkeyBackend() override = default;

        /**
         * @brief 登记一个热键
         * @param identifier 本后端内的编号，由调用方分配且当前未被占用
         * @param modifiers Qt 修饰键集合，至少一个（那条规则由 Hotkey 层先把关，后端不重复判）
         * @param key 按键本体，不含修饰键
         * @return 成功返回空载荷；失败返回中文原因，取值域不合或组合已被占用都算
         * @note 不支持该能力的平台返回 ErrorCode::PlatformUnsupported，绝不静默当成注册成功
         */
        [[nodiscard]] virtual Base::Expected<void> registerHotkey(int identifier, Qt::KeyboardModifiers modifiers, Qt::Key key) = 0;

        /**
         * @brief 注销一个热键
         * @param identifier 登记时用过的编号
         * @return 成功返回空载荷；编号没登记过或平台不支持时返回原因
         */
        [[nodiscard]] virtual Base::Expected<void> unregisterHotkey(int identifier) = 0;

        /**
         * @brief 注销本后端登记的全部热键，退出与重载配置前调用
         */
        virtual void unregisterAll() = 0;

    signals:
        /**
         * @brief 某个已登记的热键被按下
         * @param identifier 登记时那个编号
         */
        void activated(int identifier);
    };

} // namespace GyanisScribe::Platform
