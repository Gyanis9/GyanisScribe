/**
 * @file ListWidget.h
 * @brief 自绘连续曲率容器底的列表
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QListWidget>

namespace GyanisScribe::Gui
{
    /**
     * @brief 自绘连续曲率容器底的列表
     * @details 容器底与描边由本类按 Panel 档画，项底与高亮由 ItemDelegate 画；
     *          滚动、选择模式与模型接口都留在 QListWidget 上。
     */
    class ListWidget : public QListWidget
    {
    public:
        /**
         * @brief 构造列表
         * @param parent Qt 父对象
         */
        explicit ListWidget(QWidget *parent = nullptr);

        /**
         * @brief 设容器底是否可见
         * @param visible true 表示本类画容器底；导航这类压在别的面板上的列表传 false
         * @note 关掉之后列表自身完全透明，四周露出宿主的面板
         */
        void setSurfaceVisible(bool visible);

    protected:
        /**
         * @brief 在控件自己的绘制事件里铺容器底
         * @details 重写 QListWidget::event()：滚动区的 Paint 事件被 QAbstractScrollArea 换成
         *          QFrame::paintEvent，只画原生框，虚函数 paintEvent 收到的是视口事件、画不到整块容器；
         *          容器底因此在这里落笔，位置在视口与滚动条之前。高对比度档补 borderStrong 2px 边；
         *          关掉 setSurfaceVisible 时不铺底
         * @param event 事件
         * @return 与基类同解，其余事件原样交回 QListWidget
         */
        bool event(QEvent *event) override;

    private:
        /// 按当前主题铺容器底与描边
        void paintSurface();

        bool m_surfaceVisible = true; ///< 是否由本类画容器底
    };
} // namespace GyanisScribe::Gui
