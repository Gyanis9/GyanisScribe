/**
 * @file FontItemDelegate.h
 * @brief 按字体族预览字形、同时画连续曲率高亮的字体项委托
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Gui/Controls/ItemDelegate.h"

namespace GyanisScribe::Gui
{
    /**
     * @brief 按字体族预览字形、同时画连续曲率高亮的字体项委托
     * @details 字体下拉框的模型只给族名，常规委托会用界面字体画所有项；
     *          本类把每一项的字体换成它自己的族名，悬停、选中与高对比度描边
     *          仍由 ItemDelegate::paint() 统一画。
     */
    class FontItemDelegate : public ItemDelegate
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造字体项委托
         * @param parent Qt 父对象
         */
        explicit FontItemDelegate(QObject *parent = nullptr);

    protected:
        /**
         * @brief 把项的字体换成它自己的字体族
         * @details 重写 ItemDelegate::prepareOption()：模型带 Qt::FontRole 时用该字体，
         *          否则取显示文本作族名；空族名保持基类字体不变。
         * @param option 已由 initStyleOption 填好的绘制参数
         * @param index 项在模型里的下标
         */
        void prepareOption(QStyleOptionViewItem &option, const QModelIndex &index) const override;
    };
} // namespace GyanisScribe::Gui
