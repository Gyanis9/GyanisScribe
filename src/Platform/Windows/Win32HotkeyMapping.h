/**
 * @file Win32HotkeyMapping.h
 * @brief Qt 按键枚举到 Win32 热键取值域的映射
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <Qt>

namespace GyanisScribe::Platform
{

    /**
     * @brief 把 Qt 的修饰键换成 RegisterHotKey 的掩码位
     * @param modifiers Qt 修饰键集合，允许为空集（空集返回 0，「至少一个修饰键」那条规则归 Hotkey 层判）
     * @return 成功返回 MOD_ALT / MOD_CONTROL / MOD_SHIFT / MOD_WIN 的按位或
     * @return 失败返回 InvalidArgument，文案点名是哪一种修饰键不支持
     * @note 判据取值域：Control/Alt/Shift/Win 四种，Keypad* 与 GroupSwitch 这类一律拒绝，
     *       因为 RegisterHotKey 不认它们，静默丢掉就会让用户设的组合与实际生效的组合不一致
     */
    [[nodiscard]] Base::Expected<quint32> toWin32ModifierMask(Qt::KeyboardModifiers modifiers);

    /**
     * @brief 把 Qt 按键换成 Win32 虚拟键码
     * @param key 单个按键，不含修饰键
     * @return 成功返回虚拟键码：字母与数字沿用其大写字母表点位，F1-F24 落在 0x70 起的那一段，空格 0x20
     * @return 失败返回 InvalidArgument，文案点名是哪个按键
     * @note 只放开这三段是因为它们在两种键盘布局上的物理位置一致；
     *       符号键（如 Ctrl+Alt+2 在欧洲布局里是别的东西）随布局变，登记了也不是用户按的那个
     */
    [[nodiscard]] Base::Expected<quint32> toWin32VirtualKey(Qt::Key key);

} // namespace GyanisScribe::Platform
