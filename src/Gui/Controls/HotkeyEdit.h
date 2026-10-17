/**
 * @file HotkeyEdit.h
 * @brief 可录制的快捷键输入框：按一下就把组合写进配置
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Hotkey/HotkeyCombination.h"
#include "Core/Settings/ISettingsStore.h"

#include <QString>
#include <QWidget>

#include <optional>
#include <string_view>

class QLabel;

namespace GyanisScribe::Gui
{

    /**
     * @brief 判断一次按键能不能当热键的「按键本体」
     * @param key 事件里的键值
     * @return 归一后的按键；按下的是修饰键本身时返回空
     * @details 小键盘的数字要归到主键盘数字，否则同一颗键在两台机器上会存成两种写法；
     *          只按到 Ctrl 这类修饰键不是「录好了」，返回空让界面继续等真正的按键。
     */
    [[nodiscard]] std::optional<Qt::Key> editableKeyFromEvent(int keyCode);

    /**
     * @brief 这一格录下来的键要拿去干什么
     * @details 校验的松紧与提示文案都由它决定：登记到系统的那一档必须是组合，
     *          界面里的那一档一颗裸键就够。
     */
    enum class HotkeyEditScope
    {
        Global,   ///< 全局热键：至少一个修饰键，登记到操作系统
        InWindow, ///< 界面按键：裸键合法，只在程序自己有焦点的窗口里认
    };

    /**
     * @brief 快捷键录制框
     * @details 认识配置键而不是认识某个具体设置：设置页只管把键名给它，
     *          读写与校验都走 ISettingsStore 与 HotkeyCombination 那两处真源。
     */
    class HotkeyEdit : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造录制框
         * @param settings 配置存储，生命周期必须长于本控件
         * @param parent Qt 父对象
         */
        explicit HotkeyEdit(Core::ISettingsStore &settings, QWidget *parent = nullptr);

        /**
         * @brief 绑定到某个配置键并载入现值
         * @param key 键名，例如 Hotkey/Capture
         * @param title 中文名，用于无障碍名称与提示
         * @param defaultText 键不存在时的默认取值文本
         * @param scope 这一格是全局热键还是界面按键：决定校验松紧与提示文案
         */
        void bindTo(std::string_view key, const QString &title, const QString &defaultText, HotkeyEditScope scope);

        /**
         * @brief 这一格绑着的配置键名
         * @return bindTo 传进来的那个键；没绑过时为空
         * @note 给「同一页上混着两档作用域」的读端用：设置页要按前缀挑出全局那一批
         */
        [[nodiscard]] std::string_view boundKey() const noexcept;

        /**
         * @brief 取消这一格的绑定：把键从配置里去掉，显示也清空
         * @details 固定那八个动作没有这一档（它们各有默认组合，去掉键等于把用户的改动悄悄还原）；
         *          只有「可绑可不绑」的那几行给这个出口。删键而不是存一个空串：空文本每次读都算「填坏了」。
         */
        void clearBinding();


        /**
         * @brief 校验并写入一个组合
         * @param combination 待写入的组合
         * @return 成功返回空载荷；不合格时返回原因且不写配置，界面把原因显示出来
         */
        [[nodiscard]] Base::Expected<void> applyCombination(const Core::HotkeyCombination &combination);

    protected:
        /**
         * @brief 录制状态下接住按键；不在录制状态时交回 Qt
         * @param event 按键事件
         */
        void keyPressEvent(QKeyEvent *event) override;

        /**
         * @brief 点一下就进入录制状态
         * @param event 鼠标事件
         */
        void mousePressEvent(QMouseEvent *event) override;

    private:
        void showPrompt(const QString &text);

        /**
         * @brief 把「正在录制」这一档同步给样式表
         * @param recording 进入/退出录制态
         * @details 样式表按 hotkeyDisplay[recording="true"] 换主色描边；属性改了要手动重抛光，
         *          否则动态属性选择器停在上一次的值。
         */
        void setRecordingState(bool recording);

        Core::ISettingsStore &m_settings;                            ///< 配置存储，不拥有
        std::string           m_key;                                 ///< 绑定的配置键名
        HotkeyEditScope       m_scope     = HotkeyEditScope::Global; ///< 这一格录下来的键归哪一档
        QLabel               *m_display   = nullptr;                 ///< 显示组合文本的标签，也是录制状态的提示位
        bool                  m_recording = false;                   ///< 是否正在等一次按键
    };

} // namespace GyanisScribe::Gui
