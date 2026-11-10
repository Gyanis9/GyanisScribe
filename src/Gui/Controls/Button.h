/**
 * @file Button.h
 * @brief 自绘连续曲率底与状态的按钮与工具按钮
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QColor>
#include <QPushButton>
#include <QToolButton>

namespace GyanisScribe::Gui
{
    /**
     * @brief 自绘连续曲率底与状态的按钮
     * @details 填充与描边由 ControlPalette 按状态映射；QSS 只留透明边框与内边距占位，
     *          文字色仍由样式表给出。构造时打的 squircle 属性把样式表规则限定到本类，
     *          系统对话框里的原生 QPushButton 因此保留原生外观。
     */
    class Button : public QPushButton
    {
    public:
        /**
         * @brief 构造无文字的按钮
         * @param parent Qt 父对象
         */
        explicit Button(QWidget *parent = nullptr);

        /**
         * @brief 构造带文字的按钮
         * @param text 按钮文字
         * @param parent Qt 父对象
         */
        explicit Button(const QString &text, QWidget *parent = nullptr);

        /**
         * @brief 让按钮以指定颜色作底，供色板一类「按钮本身就是色块」的用法
         * @param color 底色；传无效色回到主题给出的填充
         * @details 底色有效时文字按底色亮度取深色或白色，禁用时回到主题填充；
         *          描边取主题的强描边色，白底上也能看出边界。
         */
        void setSwatchColor(const QColor &color);

        /**
         * @brief 取当前底色
         * @return 色板底色；没设过时为无效色
         */
        [[nodiscard]] QColor swatchColor() const;

    private:
        QColor m_swatchColor; ///< 色板底色；无效色表示按主题填充

    protected:
        /**
         * @brief 先画连续曲率底，再只画标签
         * @details 重写 QPushButton::paintEvent()：不调基类，只让样式引擎画 CE_PushButtonLabel；
         *          没有样式表时原生风格会整块补一层不透明底，把自绘填充盖掉
         * @param event 绘制事件
         */
        void paintEvent(QPaintEvent *event) override;
    };

    /**
     * @brief 自绘连续曲率底与状态的工具按钮
     * @details 未选中且无状态时填充透明，给标注工具栏这类密集排布保留可视留白。
     *          构造时打的 squircle 属性把样式表规则限定到本类，原生对话窗里的 QToolButton
     *          因此保留原生外观。
     */
    class ToolButton : public QToolButton
    {
    public:
        /**
         * @brief 构造工具按钮
         * @param parent Qt 父对象
         */
        explicit ToolButton(QWidget *parent = nullptr);

    protected:
        /**
         * @brief 先画连续曲率底，再画标签与菜单指示器
         * @details 重写 QToolButton::paintEvent()：不调基类，填充与描边由 toolButtonColors 按选中、悬停、
         *          按下与焦点状态给出，标签只让样式引擎画 CE_ToolButtonLabel；带菜单时再把面板、焦点边、
         *          文字与图标关掉，借 CC_ToolButton 补右下角的指示箭头——指示器由样式引擎按 ::menu-indicator
         *          规则决定画不画，样式表里的 image: none 才管得住它
         * @param event 绘制事件
         */
        void paintEvent(QPaintEvent *event) override;
    };
} // namespace GyanisScribe::Gui
