/**
 * @file IconLabel.h
 * @brief 图标 + 文字的行标签：换主题时按图标名重取，不留下旧色
 * @author Gyanis
 * @date 2026-11-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Gui/Resources/IconProvider.h"

#include <QLabel>
#include <QSize>
#include <QString>

#include <string>
#include <string_view>

namespace GyanisScribe::Gui
{
    class ThemeManager;

    /**
     * @brief 带图标的文字标签
     * @details 表单行标签用它：QIcon 是取色那一刻的快照，换主题必须按名字重取；
     *          控件把这件事接在 themeChanged 与调色板变化上，调用方不必再管。
     *          图标与文字是内嵌横排里的两个子控件，因此尺寸提示也要按那一排算一遍：
     *          QLabel 自己的 sizeHint 看不见子布局，照原样报回去会让标签列把文字裁掉。
     */
    class IconLabel : public QLabel
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造
         * @param icons 图标来源
         * @param theme 主题来源，用来在换肤时重取
         * @param name 图标名
         * @param text 已经过 uiText 的文字
         * @param parent 父控件，可空
         */
        IconLabel(IconProvider &icons, ThemeManager &theme, std::string_view name, const QString &text, QWidget *parent = nullptr);

        /**
         * @brief 图标与文字并排所需的建议尺寸
         * @return 内嵌那一排的总尺寸
         */
        [[nodiscard]] QSize sizeHint() const override;

        /**
         * @brief 图标与文字并排所需的最小尺寸
         * @return 内嵌那一排的最小尺寸
         * @details 表单按标签的尺寸提示定标签列宽，两处只要有一处漏算，行标签就会被裁掉一截。
         */
        [[nodiscard]] QSize minimumSizeHint() const override;

    protected:
        /**
         * @brief 调色板变化时重取图标
         * @param event 变化事件
         * @details 重写 QLabel::changeEvent()：只处理调色板变化，其余交给基类。
         */
        void changeEvent(QEvent *event) override;

    private:
        /// 按当前 DPR 与主题色重取一次 pixmap
        void refresh();

        IconProvider      &m_icons; ///< 图标来源，不拥有
        const std::string  m_name;  ///< 图标名
        const QString      m_text;  ///< 标签文字
    };

} // namespace GyanisScribe::Gui
