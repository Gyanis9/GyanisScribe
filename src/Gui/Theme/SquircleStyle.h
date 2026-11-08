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
     *          输入类的自绘底同理：带 squircle 属性的控件（及其内部的子行编辑）不再让原生风格
     *          画输入面板、边框与下拉/微调框主体，否则自绘的连续曲率底会被一层方底盖掉。
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
         * @brief 置空菜单面板与菜单框，以及自绘输入类的原生输入面板与边框
         * @details 重写 QProxyStyle::drawPrimitive()：PE_PanelMenu 与 PE_FrameMenu 直接返回；
         *          带 squircle 属性的控件（或其子控件）的 PE_PanelLineEdit、PE_FrameLineEdit 与
         *          QAbstractScrollArea 的 PE_Frame 也直接返回，其余图元原样交给基类
         * @param element 待绘制的图元
         * @param option 绘制参数
         * @param painter 目标画家
         * @param widget 目标控件
         */
        void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override;

        /**
         * @brief 菜单项只画内容，不画原生底与空区底
         * @details 重写 QProxyStyle::drawControl()：CE_MenuEmptyArea 直接返回；CE_MenuItem 清掉
         *          选中态与 Button、Highlight 两种画刷后再交基类
         * @param element 待绘制的控件元素
         * @param option 绘制参数
         * @param painter 目标画家
         * @param widget 目标控件
         * @note 当前项高亮与面板由 Gui::Menu 自绘；原生风格在这里补的不透明底
         *       会盖住高亮，也会在菜单边缘露出一条与主题不合的色带
         */
        void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override;

        /**
         * @brief 自绘输入类不交给原生风格画下拉框与微调框主体
         * @details 重写 QProxyStyle::drawComplexControl()：带 squircle 属性的控件画 CC_ComboBox
         *          与 CC_SpinBox 时整支返回；当前项文字由 QComboBox::paintEvent 随后单独发起的
         *          CE_ComboBoxLabel 绘制，不受影响。原生下拉箭头属于 CC_ComboBox 这一支，
         *          随主体一起跳过；应用里的箭头由三份主题 QSS 的 QComboBox::down-arrow 提供，
         *          不套样式表的 QComboBox 不会自带箭头
         * @param control 待绘制的复杂控件
         * @param option 绘制参数
         * @param painter 目标画家
         * @param widget 目标控件
         */
        void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter, const QWidget *widget) const override;
    };
} // namespace GyanisScribe::Gui
