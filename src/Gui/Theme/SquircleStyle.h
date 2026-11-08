/**
 * @file SquircleStyle.h
 * @brief 应用级代理样式：让原生风格不画菜单面板与菜单框
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QProxyStyle>

namespace GyanisScribe::Gui
{
    /**
     * @brief 连续曲率控件共用的代理样式
     * @details 菜单面板与菜单框由 Gui::Menu 自绘：原生风格若照常画一层方形面板，
     *          自绘的连续曲率高亮会被它盖住，角上的透明区也会露出方底。
     */
    class SquircleStyle : public QProxyStyle
    {
    public:
        /**
         * @brief 把本样式装成应用样式
         * @note 必须在 QApplication 构造之后、第一次 setStyleSheet 之前调用；
         *       进程内只装一次，重复调用不会再造第二份样式
         */
        static void install();

    protected:
        /**
         * @brief 置空菜单面板与菜单框，其余交回代理的桌面风格
         * @param element 待绘制的图元
         * @param option 绘制参数
         * @param painter 目标画家
         * @param widget 目标控件
         */
        void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override;

        /**
         * @brief 菜单项只画内容，不画原生底与空区底
         * @param element 待绘制的控件元素
         * @param option 绘制参数
         * @param painter 目标画家
         * @param widget 目标控件
         * @note 当前项高亮与面板由 Gui::Menu 自绘；原生风格在这里补的不透明底
         *       会盖住高亮，也会在菜单边缘露出一条与主题不合的色带
         */
        void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override;
    };
} // namespace GyanisScribe::Gui
