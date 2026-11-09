/**
 * @file Menu.h
 * @brief 自绘连续曲率面板与当前项高亮的菜单
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QMenu>

namespace GyanisScribe::Gui
{
    /**
     * @brief 自绘连续曲率面板与当前项高亮的菜单
     * @details 面板、边框与当前项高亮都由本类画；菜单项文字、图标、勾与子菜单箭头
     *          仍交回 QMenu 画，键盘导航与动作触发的行为不变。构造时打的 squircle 属性
     *          把样式表规则与代理样式的菜单拦截限定到本类，系统文本控件的标准右键菜单
     *          因此保留原生面板与高亮。
     */
    class Menu : public QMenu
    {
    public:
        /**
         * @brief 构造空菜单
         * @param parent Qt 父对象
         */
        explicit Menu(QWidget *parent = nullptr);

        /**
         * @brief 构造带标题的菜单
         * @param title 菜单标题
         * @param parent Qt 父对象
         */
        explicit Menu(const QString &title, QWidget *parent = nullptr);

    protected:
        /**
         * @brief 先画面板与当前项高亮，再让 QMenu 画菜单项内容
         * @details 重写 QMenu::paintEvent()：面板、边框与当前项高亮由本类画，
         *          菜单项文字、图标、勾与子菜单箭头仍交回基类
         * @param event 绘制事件
         */
        void paintEvent(QPaintEvent *event) override;
    };
} // namespace GyanisScribe::Gui
