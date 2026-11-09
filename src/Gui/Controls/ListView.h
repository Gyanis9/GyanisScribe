/**
 * @file ListView.h
 * @brief 自绘连续曲率容器底的下拉弹层视图
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QListView>

namespace GyanisScribe::Gui
{
    /**
     * @brief 自绘连续曲率容器底的下拉弹层视图
     * @details 容器底与描边由本类按 Control 档画，项底与高亮由 ItemDelegate 画；
     *          视口的悬停属性与鼠标跟踪也在构造里打开，项悬停态不依赖样式表的 :hover 规则；
     *          省略模式与 Qt 默认弹层一致取中间省略。
     *          弹层不是独立字段，无障碍名只取通用的「下拉列表」，具体字段由所属组合框的名字说明；
     *          滚动、选择模式与模型接口都留在 QListView 上。
     */
    class ListView : public QListView
    {
    public:
        /**
         * @brief 构造弹层视图
         * @param parent Qt 父对象
         */
        explicit ListView(QWidget *parent = nullptr);

    protected:
        /**
         * @brief 在控件自己的绘制事件里铺容器底
         * @details 重写 QListView::event()：滚动区的 Paint 事件被 QAbstractScrollArea 换成
         *          QFrame::paintEvent，只画原生框，虚函数 paintEvent 收到的是视口事件、画不到整块容器；
         *          容器底因此在这里落笔，位置在视口与滚动条之前。高对比度档补 borderStrong 2px 边
         * @param event 事件
         * @return 与基类同解，其余事件原样交回 QListView
         */
        bool event(QEvent *event) override;

    private:
        /// 按当前主题铺容器底与描边
        void paintSurface();
    };
} // namespace GyanisScribe::Gui
