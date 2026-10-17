/**
 * @file SpinBox.h
 * @brief 与圆角输入框同形的数字微调框：关掉原生按钮，右侧自绘两枚 chevron
 * @author Gyanis
 * @date 2026-10-17
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QSpinBox>

class QAbstractButton;
class QResizeEvent;

namespace GyanisScribe::Gui
{

    /**
     * @brief 自绘微调按钮的数字框
     * @details 原生 QSpinBox 的上下按钮由 Windows 风格绘制，方形灰底与圆角输入框割裂。
     *          本类保留 QSpinBox 的全部范围/后缀/键盘语义，只把右侧 22 逻辑像素交给两枚
     *          透明小按钮；按钮自绘 chevron，悬停给一档 mutedFill 底。
     */
    class SpinBox final : public QSpinBox
    {
    public:
        /**
         * @brief 构造数字框
         * @param parent Qt 父对象
         */
        explicit SpinBox(QWidget *parent = nullptr);

    protected:
        /**
         * @brief 窗口尺寸变化时把两枚按钮重新贴到右缘
         * @param event 尺寸事件
         */
        void resizeEvent(QResizeEvent *event) override;

    private:
        /// 右缘竖条的宽度，逻辑像素：装得下两枚 16px 高的按钮
        static constexpr int kStepperWidth = 22;

        QAbstractButton *m_up   = nullptr; ///< 增加按钮，对象名 spinUp
        QAbstractButton *m_down = nullptr; ///< 减少按钮，对象名 spinDown
    };

} // namespace GyanisScribe::Gui
