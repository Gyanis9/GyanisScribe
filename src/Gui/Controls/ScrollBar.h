/**
 * @file ScrollBar.h
 * @brief 自绘连续曲率手柄的细滚动条
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QScrollBar>

class QEnterEvent;

namespace GyanisScribe::Gui
{
    /**
     * @brief 自绘连续曲率手柄的细滚动条
     * @details 轨道完全透明，只画一段按细槽档取圆角的手柄，粗细与颜色都不在样式表里：
     *          浅深档手柄是带透明度的一档中性色，悬停时加深；高对比度档用纯黑不透明度。
     *          手柄最短 24 逻辑像素，长度按页面步长与总量算。
     */
    class ScrollBar : public QScrollBar
    {
        Q_OBJECT
    public:
        /**
         * @brief 构造滚动条
         * @param orientation 方向
         * @param parent Qt 父对象
         */
        explicit ScrollBar(Qt::Orientation orientation, QWidget *parent = nullptr);

        /**
         * @brief 滚动条的建议尺寸
         * @return 竖直方向 10x120，水平方向 120x10 逻辑像素
         */
        [[nodiscard]] QSize sizeHint() const override;

    protected:
        /**
         * @brief 自绘圆角手柄，轨道留空
         * @details 重写 QScrollBar::paintEvent()：不铺轨道底，只按当前值、页面步长与总量
         *          算出手柄位置与长度，再按细槽档画一段连续曲率圆角
         * @param event 绘制事件，本类不使用
         */
        void paintEvent(QPaintEvent *event) override;

        /**
         * @brief 鼠标进入时重画一档更重的手柄
         * @details 重写 QWidget::enterEvent()：样式表撤掉 :hover 规则后，悬停态不再自动触发重绘，
         *          这里手动刷新一次，paintEvent 里的 underMouse() 才会落到新颜色上
         * @param event 进入事件
         */
        void enterEvent(QEnterEvent *event) override;

        /**
         * @brief 鼠标离开时退回淡一档的手柄
         * @details 重写 QWidget::leaveEvent()：与进入时的刷新配对，
         *          悬停颜色不会留到鼠标移开之后
         * @param event 离开事件
         */
        void leaveEvent(QEvent *event) override;
    };
} // namespace GyanisScribe::Gui
