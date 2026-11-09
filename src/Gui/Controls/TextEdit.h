/**
 * @file TextEdit.h
 * @brief 自绘连续曲率底的多行文本区
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Geometry/ContinuousCorner.h"

#include <QTextEdit>

namespace GyanisScribe::Gui
{
    /**
     * @brief 自绘连续曲率底的多行文本区
     * @details 识别结果区是 Panel 档、标注窗的行内编辑框是 Control 档，圆角角色因此可配；
     *          视口关掉自动填底，避免调色板的 Base 在自绘面板上再铺一层方底。
     */
    class TextEdit : public QTextEdit
    {
    public:
        /**
         * @brief 构造文本区
         * @param role 圆角档位：识别结果区用 Panel，标注行内编辑框用 Control
         * @param parent Qt 父对象
         */
        explicit TextEdit(Core::CornerRole role = Core::CornerRole::Panel, QWidget *parent = nullptr);

        /**
         * @brief 按默认的 Panel 档构造文本区
         * @param parent Qt 父对象
         * @note 等价于 TextEdit(Core::CornerRole::Panel, parent)，给识别结果区这类只差父对象的调用点用
         */
        explicit TextEdit(QWidget *parent);

    protected:
        /**
         * @brief 在控件自身的绘制事件上先铺连续曲率底
         * @details 重写 QTextEdit::event()：多行文本区把视口绘制与控件绘制分成了两条路，
         *          视口由 QTextEdit::paintEvent 单独画，控件自身的 Paint 才轮到这一层底；
         *          填充与描边由 inputColors 给出，正文、光标、选区与滚动条仍由基类处理
         * @param event 事件对象
         * @return 基类是否消费了该事件
         */
        bool event(QEvent *event) override;

    private:
        Core::CornerRole m_role; ///< 圆角档位，构造时固定
    };
} // namespace GyanisScribe::Gui
