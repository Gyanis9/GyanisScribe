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
     *          QSS 只留透明边框占位、内边距与箭头图，弹层仍走样式表。
     */
    class ComboBox : public QComboBox
    {
    public:
        /**
         * @brief 构造下拉框
         * @param parent Qt 父对象
         */
        explicit ComboBox(QWidget *parent = nullptr);

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
     * @details 与 ComboBox 共用同一套底色与描边，字体列表与预览字符仍由基类提供。
     */
    class FontComboBox : public QFontComboBox
    {
    public:
        /**
         * @brief 构造字体下拉框
         * @param parent Qt 父对象
         */
        explicit FontComboBox(QWidget *parent = nullptr);

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
