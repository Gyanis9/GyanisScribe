/**
 * @file HotkeyCombination.h
 * @brief 组合键的值类型：文本与按键取值之间的双向换算
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <Qt>

#include <string_view>

class QString;

namespace GyanisScribe::Hotkey
{

    /**
     * @brief 一个组合键
     * @details 值类型，不可变语义：改组合就是换一个对象。文本形态固定为「Ctrl+Alt+A」风格
     *          （修饰键按 Ctrl、Alt、Shift、Win 的固定顺序排），设置里存的、菜单上提示的、
     *          快捷键录入框显示的都取这一个形态，三处不许各写各的。
     */
    class HotkeyCombination
    {
    public:
        /**
         * @brief 默认构造出一个「未设」的组合
         * @note 未设的组合不能用于登记，checkUsableGlobally() 会给出原因
         */
        HotkeyCombination() = default;

        /**
         * @brief 由修饰键与按键组成一个组合
         * @param modifiers 修饰键集合
         * @param key 按键本体，不能是修饰键本身
         */
        HotkeyCombination(Qt::KeyboardModifiers modifiers, Qt::Key key);

        /**
         * @brief 从文本解析组合
         * @param text 形如 Ctrl+Alt+A 的文本，大小写与首尾空格不影响解析
         * @return 成功返回组合本体
         * @return 失败返回 InvalidArgument，文案点名是哪一段读不出来、以及支持的写法
         * @note 修饰键与按键都只认固定词表：字母取 A-Z、数字取 0-9、功能键取 F1-F24、再加 Space。
         *       「至少一个修饰键」也在这里判——一个单键的全局热键会把别的程序的按键吞掉
         */
        [[nodiscard]] static Base::Expected<HotkeyCombination> fromText(std::string_view text, bool allowBareKeys = false);

        /**
         * @brief 从文本解析组合，但不判「能不能拿去登记」
         * @param text 形如 Ctrl+Alt+A 或单键 A 的文本
         * @return 成功返回组合本体；失败返回 InvalidArgument 与点名那一段的文案
         * @details 与 fromText() 差在最后那道全局档检查：界面内的按键允许裸键，
         *          「至少要带一个修饰键」是登记到系统才需要的限制。
         */
        [[nodiscard]] static Base::Expected<HotkeyCombination> parse(std::string_view text);

        /**
         * @brief 检查能否用作全局热键
         * @param allowBareKeys 「允许单键当全局热键」那一格开着时传真：放行的只是「必须有修饰键」那一条，
         *        单键会吞掉别的程序的打字这个后果仍在，这里只是把选择权交回用户
         * @return 成功返回空载荷
         * @return 失败返回 InvalidArgument 与中文原因（缺修饰键、按键是修饰键本身、或按键不在允许集合内）
         */
        [[nodiscard]] Base::Expected<void> checkUsableGlobally(bool allowBareKeys = false) const;

        /**
         * @brief 检查能否用作本程序窗口里的按键
         * @return 成功返回空载荷
         * @return 失败返回 InvalidArgument 与中文原因（按键是修饰键本身，或不在允许集合内）
         * @note 裸键在这一档合法：窗口里的按键只在程序自己有焦点时才吃到，不会像全局单键那样
         *       把别的程序的打字吞掉，所以只少「至少一个修饰键」那一条。
         */
        [[nodiscard]] Base::Expected<void> checkUsableInWindow() const;

        /**
         * @brief 转成统一文本形态
         * @return 供设置项存储、菜单提示与工具提示使用的字符串；没有按键本体时返回空文本
         * @note 裸键（界面按键用的那一形）返回的就是那一颗键自己，例如「R」
         */
        [[nodiscard]] QString toText() const;

        /**
         * @brief 取修饰键集合
         * @return 组合里的修饰键
         */
        [[nodiscard]] Qt::KeyboardModifiers modifiers() const noexcept;

        /**
         * @brief 取按键本体
         * @return 组合里的按键，未设时为 Qt::Key_unknown
         */
        [[nodiscard]] Qt::Key key() const noexcept;

        [[nodiscard]] friend bool operator==(const HotkeyCombination &left, const HotkeyCombination &right) = default;

    private:
        Qt::KeyboardModifiers m_modifiers = Qt::NoModifier;  ///< 修饰键集合
        Qt::Key               m_key       = Qt::Key_unknown; ///< 按键本体
    };

} // namespace GyanisScribe::Hotkey
