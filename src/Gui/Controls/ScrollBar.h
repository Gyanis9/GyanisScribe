/**
 * @file ScrollBar.h
 * @brief 自绘连续曲率手柄的细滚动条
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QRectF>
#include <QScrollBar>

class QEnterEvent;
class QStyleOptionSlider;

namespace GyanisScribe::Gui
{
    /// 滚动条的粗细，逻辑像素：自绘轨道、样式度量与命中几何共用这一档
    inline constexpr int kScrollBarThickness = 10;

    /// 手柄的最短长度，逻辑像素：自绘手柄与命中测试共用这一档
    inline constexpr int kScrollBarMinimumHandle = 24;

    /**
     * @brief 滚动条内轨道的矩形
     * @param option 滚动条的样式参数，只取方向
     * @param widget 滚动条本体，长度与粗细取自它的尺寸
     * @return 去掉四周留白后的轨道矩形；该方向上放不下两档留白时返回空矩形
     * @details 轨道与手柄共用一组粗细 10px、留白 2px 的常量；绘制、命中与拖动换算都从这里取几何，
     *          样式表不再定义滚动条尺寸
     */
    [[nodiscard]] QRectF scrollBarTrackRect(const QStyleOptionSlider &option, const QWidget &widget);

    /**
     * @brief 滚动条手柄的矩形
     * @param option 滚动条的样式参数：方向、范围、页面步长与当前滑动位置
     * @param widget 滚动条本体，长度与粗细取自它的尺寸
     * @return 手柄矩形；轨道长度不足时返回空矩形
     * @details 手柄嵌在内轨道里，最短 24 逻辑像素；自绘与命中测试调同一份公式，
     *          画出来的手柄边缘不会落进步进区，拖动时手柄也不会落后于指针
     */
    [[nodiscard]] QRectF scrollBarSliderRect(const QStyleOptionSlider &option, const QWidget &widget);

    /**
     * @brief 自绘连续曲率手柄的细滚动条
     * @details 轨道完全透明，只画一段按细槽档取圆角的手柄，粗细与颜色都不在样式表里：
     *          浅深档手柄是带透明度的一档中性色，悬停时加深；高对比度档用纯黑不透明度。
     *          手柄最短 24 逻辑像素，长度按页面步长与总量算；命中矩形与绘制矩形同源，
     *          由 SquircleStyle 的 subControlRect 返回同一份结果。
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
         * @details 重写 QScrollBar::paintEvent()：不铺轨道底，只按 scrollBarSliderRect() 给出的
         *          手柄矩形画一段连续曲率圆角，该矩形同时供命中测试与拖动换算使用
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
