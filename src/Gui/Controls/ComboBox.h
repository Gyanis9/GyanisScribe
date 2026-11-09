/**
 * @file ComboBox.h
 * @brief 自绘连续曲率底的下拉框与字体下拉框
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QComboBox>
#include <QFontComboBox>

namespace GyanisScribe::Gui
{
    /**
     * @brief 自绘连续曲率底的下拉框
     * @details 填充与描边由 ControlPalette::inputColors 按启用与焦点状态映射；
     *          QSS 只留透明边框占位、内边距与箭头图，弹层改由 Gui::ListView 与 ItemDelegate 自绘。
     */
    class ComboBox : public QComboBox
    {
    public:
        /**
         * @brief 构造下拉框
         * @param parent Qt 父对象
         */
        explicit ComboBox(QWidget *parent = nullptr);

        /**
         * @brief 弹出下拉列表，并确认弹层委托是 ItemDelegate
         * @details 重写 QComboBox::showPopup()：委托在弹层视图构造时就已装好，Qt 打开弹层时不会改回来；
         *          这里只在委托不是 ItemDelegate 时补装一次，正常打开不再新建委托
         */
        void showPopup() override;

    protected:
        /**
         * @brief 先画连续曲率底，再交给基类画文字与下拉箭头
         * @details 重写 QComboBox::paintEvent()：填充与描边由 inputColors 给出，
         *          当前项文字与 ::down-arrow 仍由样式引擎绘制
         * @param event 绘制事件
         */
        void paintEvent(QPaintEvent *event) override;
    };

    /**
     * @brief 自绘连续曲率底的字体下拉框
     * @details 与 ComboBox 共用同一套底色与描边，字体列表仍由基类提供；
     *          弹层由 Gui::ListView 与 FontItemDelegate 自绘：项底与高亮按连续曲率画，
     *          字体名按各自的族名预览。
     * @note QFontComboBox::setWritingSystem() 与 writingSystem() 依赖基类的 QFontFamilyDelegate，
     *       换上 FontItemDelegate 后二者不再生效；应用当前没有调用
     */
    class FontComboBox : public QFontComboBox
    {
    public:
        /**
         * @brief 构造字体下拉框
         * @param parent Qt 父对象
         */
        explicit FontComboBox(QWidget *parent = nullptr);

        /**
         * @brief 弹出字体列表，并确认弹层委托是 FontItemDelegate
         * @details 重写 QFontComboBox::showPopup()：委托在弹层视图构造时就已装好，Qt 打开弹层时不会改回来；
         *          这里只在委托不是 FontItemDelegate 时补装一次，正常打开不再新建委托，字体预览不会丢
         */
        void showPopup() override;

    protected:
        /**
         * @brief 先画连续曲率底，再交给基类画文字与下拉箭头
         * @details 重写 QFontComboBox::paintEvent()：填充与描边由 inputColors 给出，
         *          字体预览、当前项文字与箭头仍由样式引擎绘制
         * @param event 绘制事件
         */
        void paintEvent(QPaintEvent *event) override;
    };
} // namespace GyanisScribe::Gui
