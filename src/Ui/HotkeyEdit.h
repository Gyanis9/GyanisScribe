/**
 * @file HotkeyEdit.h
 * @brief 可录制的快捷键输入框：按一下就把组合写进配置
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Hotkey/HotkeyCombination.h"
#include "Settings/ISettingsStore.h"

#include <QString>
#include <QWidget>

#include <optional>
#include <string_view>

class QLabel;

namespace GyanisScribe::Ui
{

    /**
     * @brief 判断一次按键能不能当热键的「按键本体」
     * @param key 事件里的键值
     * @return 归一后的按键；按下的是修饰键本身时返回空
     * @details 小键盘的数字要归到主键盘数字，否则同一颗键在两台机器上会存成两种写法；
     *          只按到 Ctrl 这类修饰键不是「录好了」，返回空让界面继续等真正的按键。
     */
    [[nodiscard]] std::optional<Qt::Key> editableKeyFromEvent(Qt::Key key);

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
        explicit HotkeyEdit(Settings::ISettingsStore &settings, QWidget *parent = nullptr);

        /**
         * @brief 绑定到某个配置键并载入现值
         * @param key 键名，例如 Hotkey/Capture
         * @param title 中文名，用于无障碍名称与提示
         * @param defaultText 键不存在时的默认取值文本
         */
        void bindTo(std::string_view key, const QString &title, const QString &defaultText);

        /// 当前显示的组合文本
        [[nodiscard]] QString combinationText() const;

        /**
         * @brief 校验并写入一个组合
         * @param combination 待写入的组合
         * @return 成功返回空载荷；不合格时返回原因且不写配置，界面把原因显示出来
         */
        [[nodiscard]] Core::Expected<void> applyCombination(const Hotkey::HotkeyCombination &combination);

    signals:
        /// 组合已经写进配置
        void combinationChanged(const QString &text);

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

        Settings::ISettingsStore &m_settings;            ///< 配置存储，不拥有
        std::string               m_key;                 ///< 绑定的配置键名
        QLabel                   *m_display   = nullptr; ///< 显示组合文本的标签，也是录制状态的提示位
        bool                      m_recording = false;   ///< 是否正在等一次按键
    };

} // namespace GyanisScribe::Ui
