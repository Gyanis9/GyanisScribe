/**
 * @file ItemDelegate.h
 * @brief 按连续曲率画悬停与选中底的列表项委托
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QStyledItemDelegate>

namespace GyanisScribe::Gui
{
    /**
     * @brief 按连续曲率画悬停与选中底的列表项委托
     * @details 项底与高亮由本类按 Compact 档画；文字、图标与省略仍交回 QStyledItemDelegate，
     *          样式表因此只需保留内边距与文字色，不再画项的底色。
     */
    class ItemDelegate : public QStyledItemDelegate
    {
    public:
        /**
         * @brief 构造委托
         * @param parent Qt 父对象
         */
        explicit ItemDelegate(QObject *parent = nullptr);

        /**
         * @brief 先画项底与高亮，再让基类画项内容
         * @details 重写 QStyledItemDelegate::paint()：选中与悬停时按 Compact 档画一块连续曲率底，
         *          随后把调色板里的 Highlight 画刷清空，基类只落文字与图标，不再叠一层方角选中底；
         *          未选中也未悬停时不铺底，列表容器或宿主底原样露出来
         * @param painter 目标画家
         * @param option 项的绘制参数，含矩形与状态
         * @param index 项在模型里的下标
         */
        void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    };
} // namespace GyanisScribe::Gui
