/**
 * @file CheckBox.h
 * @brief 自绘指示器与弹性对勾的复选框
 * @author Gyanis
 * @date 2026-10-24
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Gui/Theme/Motion.h"

#include <QCheckBox>

class QEnterEvent;

namespace GyanisScribe::Gui
{
    /**
     * @brief 弹性对勾复选框
     * @details 指示器的填充、描边与连续曲率圆角都由本类自绘：静息取 controlFill，悬停取 hover，
     *          勾选取 accent，禁用取 mutedFill；高对比度档另给 2px 边，焦点、勾选与悬停用主色，
     *          静息与禁用用 borderStrong。样式表的 ::indicator 只保留尺寸与透明边框占位。
     *          样式表已没有 :hover 规则，悬停态由进入与离开事件主动请求重绘。
     *          勾选时勾按 kPopSpring 从 0.55 弹到 1.0，并按 onAccent 自绘；取消勾选不播动画，直接恢复满幅。
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

        /**
         * @brief 取指示器矩形，供用例核对自绘落点
         * @param box 目标复选框
         * @return 与 paintEvent 自绘时相同的指示器矩形
         */
        [[nodiscard]] static QRect indicatorRectForTesting(const CheckBox &box);

    protected:
        /**
         * @brief 绘制复选框
         * @details 重写 QCheckBox::paintEvent()：先按状态在指示器矩形里铺连续曲率底与描边，
         *          再让基类画文字与图标；勾选时最后按 checkScale 围绕方框中心缩放，用 QPainterPath 画勾。
         * @param event 重绘事件
         */
        void paintEvent(QPaintEvent *event) override;

        /**
         * @brief 鼠标进入时重画悬停档的指示器
         * @details 重写 QWidget::enterEvent()：样式表撤掉 :hover 规则后，悬停态不再自动触发重绘，
         *          这里手动刷新一次，paintEvent 里的 underMouse() 才会落到 hover 底与悬停边
         * @param event 进入事件
         */
        void enterEvent(QEnterEvent *event) override;

        /**
         * @brief 鼠标离开时退回静息档的指示器
         * @details 重写 QWidget::leaveEvent()：与进入时的刷新配对，悬停底与描边不会留到鼠标移开之后
         * @param event 离开事件
         */
        void leaveEvent(QEvent *event) override;

    private:
        /**
         * @brief 取勾选框指示器的矩形
         * @return 与样式表 ::indicator 同源的矩形，按当前布局与样式算
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
