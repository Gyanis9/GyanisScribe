/**
 * @file LineEdit.h
 * @brief 自绘连续曲率底的单行输入框
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QLineEdit>

namespace GyanisScribe::Gui
{
    /**
     * @brief 自绘连续曲率底与状态边界的单行输入框
     * @details 填充与描边由 ControlPalette::inputColors 按启用、只读与焦点状态映射；
     *          QSS 只留透明边框占位、内边距与文字色，圆角与可见描边都由本类画。
     */
    class LineEdit : public QLineEdit
    {
    public:
        /**
         * @brief 构造输入框
         * @param parent Qt 父对象
         */
        explicit LineEdit(QWidget *parent = nullptr);

    protected:
        /**
         * @brief 先画连续曲率底，再交给基类画文字、光标与选区
         * @details 重写 QLineEdit::paintEvent()：填充与描边由 inputColors 给出，
         *          文字、占位、光标与选区仍由基类处理
         * @param event 绘制事件
         */
        void paintEvent(QPaintEvent *event) override;
    };
} // namespace GyanisScribe::Gui
