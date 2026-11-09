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
     *          高对比度档的选中项再补一圈主色边，其余档只铺底。
     */
    class ItemDelegate : public QStyledItemDelegate
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造委托
         * @param parent Qt 父对象
         */
        explicit ItemDelegate(QObject *parent = nullptr);

        /**
         * @brief 先画项底与高亮，再让基类画项内容
         * @details 重写 QStyledItemDelegate::paint()：选中与悬停时按 Compact 档画一块连续曲率底，
         *          高对比度档的选中项再描一圈主色边；随后把调色板里的 Highlight 画刷清空，
         *          并把 HighlightedText 对齐正文色，基类只落文字与图标，不再叠一层方角选中底，
         *          样式表不在时也不会用浅色选中文字压在 accentSoft 上；
         *          未选中也未悬停时不铺底，列表容器或宿主底原样露出来
         * @param painter 目标画家
         * @param option 项的绘制参数，含矩形与状态
         * @param index 项在模型里的下标
         */
        void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    protected:
        /**
         * @brief 在画高亮与文字之前调整绘制参数
         * @details paint() 在 initStyleOption 之后调用本钩子；派生类据此换字体等，
         *          不必复制高亮与选中逻辑
         * @param option 已由 initStyleOption 填好的绘制参数
         * @param index 项在模型里的下标
         */
        virtual void prepareOption(QStyleOptionViewItem &option, const QModelIndex &index) const;
    };
} // namespace GyanisScribe::Gui
