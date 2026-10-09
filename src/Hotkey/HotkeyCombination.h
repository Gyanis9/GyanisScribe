/**
 * @file HotkeyCombination.h
 * @brief 组合键的值类型：文本与按键取值之间的双向换算
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

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
        [[nodiscard]] static Core::Expected<HotkeyCombination> fromText(std::string_view text);

        /**
         * @brief 检查能否用作全局热键
         * @return 成功返回空载荷
         * @return 失败返回 InvalidArgument 与中文原因（缺修饰键、按键是修饰键本身、或按键不在允许集合内）
         */
        [[nodiscard]] Core::Expected<void> checkUsableGlobally() const;

        /**
         * @brief 转成统一文本形态
         * @return 供设置项存储、菜单提示与工具提示使用的字符串；未设的组合返回空文本
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
