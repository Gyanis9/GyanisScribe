/**
 * @file CheckBox.h
 * @brief 带弹性对勾的复选框：方框仍由样式表绘制，勾由本类自绘
 * @author Gyanis
 * @date 2026-10-24
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Gui/Theme/Motion.h"

#include <QCheckBox>

namespace GyanisScribe::Gui
{
    /**
     * @brief 弹性对勾复选框
     * @details 方框的填充、描边与圆角仍由 QSS 的 ::indicator 规则绘制；本类通过 animatedIndicator
     *          动态属性让 QSS 关掉静态勾图，勾选时用 kPopSpring 从 0.55 弹到 1.0，并按 onAccent 自绘勾。
     *          取消勾选不播动画，直接恢复满幅。
     */
    class CheckBox final : public QCheckBox
    {
        Q_OBJECT
        Q_PROPERTY(qreal checkScale READ checkScale WRITE setCheckScale)

    public:
        /**
         * @brief 构造复选框
         * @param parent Qt 父控件
         */
        explicit CheckBox(QWidget *parent = nullptr);

        /**
         * @brief 开关本控件的勾选动画
         * @param enabled false 时进行中的弹簧立即落到目标，之后勾选直接满幅
         */
        void setAnimationsEnabled(bool enabled);

        /**
         * @brief 当前勾的绘制缩放
         * @return 1.0 为满幅，勾选动画的起点是 0.55
         */
        [[nodiscard]] qreal checkScale() const noexcept;

        /**
         * @brief 属性写入口：直接改绘制缩放
         * @param scale 新的缩放值
         * @note 不碰弹簧；动画进行中时下一帧会覆盖这里写入的值
         */
        void setCheckScale(qreal scale);

        /**
         * @brief 把勾选动画向前推指定毫秒
         * @param milliseconds 本次推进的毫秒数，负值按 0 处理
         * @note 只给用例用：不依赖真实时钟，推进到弹簧稳定时长即收在 1.0
         */
        void advanceCheckAnimationForTesting(int milliseconds);

    protected:
        /**
         * @brief 绘制复选框
         * @details 重写 QCheckBox::paintEvent()：先让基类画方框、文字与图标，
         *          勾选时再按 checkScale 围绕方框中心缩放，用 QPainterPath 画勾。
         * @param event 重绘事件
         */
        void paintEvent(QPaintEvent *event) override;

    private:
        /**
         * @brief 取勾选框指示器的矩形
         * @return 与 QSS 的 ::indicator 同源的矩形，按当前布局与样式算
         */
        [[nodiscard]] QRect indicatorRect() const;

        /**
         * @brief 勾选状态变化的接线
         * @param checked 新的勾选状态；取消勾选时不播动画
         */
        void onToggled(bool checked);

        SpringValue m_spring;           ///< 勾的弹性缩放，满幅为 1.0
        qreal       m_checkScale = 1.0; ///< 当前绘制缩放，paintEvent 读它
    };
} // namespace GyanisScribe::Gui
