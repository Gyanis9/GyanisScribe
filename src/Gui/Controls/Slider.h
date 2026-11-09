/**
 * @file Slider.h
 * @brief 自绘连续曲率槽与手柄的水平滑块
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QSlider>

class QEnterEvent;

namespace GyanisScribe::Gui
{
    /**
     * @brief 自绘连续曲率槽与手柄的水平滑块
     * @details 槽、已走过的子页与手柄都由本类绘制，尺寸与颜色不再落在样式表里：
     *          浅深档槽给 1px 边、高对比度档 2px；禁用时槽与手柄落静默色；
     *          悬停时手柄换主色加深档；聚焦时手柄反转成底色芯加正文色圈，
     *          与纯键盘操作时的可见焦点一致。槽与手柄的圆角按细槽档与正圆档取。
     *          绘制按水平方向实现，标注工具栏的线宽滑块是当前的唯一用户。
     */
    class Slider : public QSlider
    {
        Q_OBJECT
    public:
        /**
         * @brief 构造滑块
         * @param orientation 方向
         * @param parent Qt 父对象
         */
        explicit Slider(Qt::Orientation orientation, QWidget *parent = nullptr);

        /**
         * @brief 滑块的建议尺寸
         * @return 宽 80、高 20 逻辑像素
         */
        [[nodiscard]] QSize sizeHint() const override;

    protected:
        /**
         * @brief 按当前主题与状态自绘槽、子页与手柄
         * @details 重写 QSlider::paintEvent()：先铺整条槽，再把从起点到手柄中心的一段画成主色子页，
         *          最后把手柄画在子页末端。手柄位置按 style()->sliderPositionFromValue 在
         *          控件宽度减手柄边长的区间上算，与拖动时的命中区同解
         * @param event 绘制事件，本类不使用
         */
        void paintEvent(QPaintEvent *event) override;

        /**
         * @brief 鼠标进入时重画悬停档的手柄
         * @details 重写 QWidget::enterEvent()：样式表撤掉 :hover 规则后，悬停态不再自动触发重绘，
         *          这里手动刷新一次，paintEvent 里的 underMouse() 才会落到主色加深档
         * @param event 进入事件
         */
        void enterEvent(QEnterEvent *event) override;

        /**
         * @brief 鼠标离开时退回静息档的手柄
         * @details 重写 QWidget::leaveEvent()：与进入时的刷新配对，悬停色不会留到鼠标移开之后
         * @param event 离开事件
         */
        void leaveEvent(QEvent *event) override;
    };
} // namespace GyanisScribe::Gui
