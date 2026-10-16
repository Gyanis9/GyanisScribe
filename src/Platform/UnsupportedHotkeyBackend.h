/**
 * @file UnsupportedHotkeyBackend.h
 * @brief 没有全局热键能力的平台所用的桩后端
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Platform/IHotkeyBackend.h"

namespace GyanisScribe::Platform
{

    /**
     * @brief 「本平台不提供全局热键」的那一份后端
     * @details 登记一律返回 PlatformUnsupported 并带中文原因，而不是安静成功：
     *          安静成功会让用户以为按了没反应是自己记错了组合键。设置页据此把快捷键一栏标灰并给替代做法
     *          （在窗口内用普通快捷键，或等平台适配）。
     * @note 将来适配的平台在落地前用这一形状。它在任何平台都参与编译，便于在 Windows 上直接测到桩的契约。
     */
    class UnsupportedHotkeyBackend : public IHotkeyBackend
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造桩后端
         */
        UnsupportedHotkeyBackend();

        /**
         * @brief 析构桩后端
         */
        ~UnsupportedHotkeyBackend() override;

        /**
         * @brief 登记热键，见 IHotkeyBackend::registerHotkey()
         * @param identifier 编号，桩不使用
         * @param modifiers 修饰键集合，桩不使用
         * @param key 按键，桩不使用
         * @return 恒为失败：ErrorCode::PlatformUnsupported，文案写明本平台没有全局热键与替代做法
         */
        [[nodiscard]] Base::Expected<void> registerHotkey(int identifier, Qt::KeyboardModifiers modifiers, Qt::Key key) override;

        /**
         * @brief 注销热键，见 IHotkeyBackend::unregisterHotkey()
         * @param identifier 编号
         * @return 恒为失败，原因与登记一致；本来就没有登记过，不能报成功
         */
        [[nodiscard]] Base::Expected<void> unregisterHotkey(int identifier) override;

        /**
         * @brief 注销全部，见 IHotkeyBackend::unregisterAll()
         * @details 空实现：这里从来没有登记过任何东西。
         */
        void unregisterAll() override;
    };

} // namespace GyanisScribe::Platform
