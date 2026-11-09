/**
 * @file PrivacyReviewDialog.h
 * @brief 隐私打码审核对话框：逐项确认命中项并选择打码方式
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Privacy/PiiTypes.h"

#include <QDialog>

#include <vector>

class QComboBox;
class QListWidget;

namespace GyanisScribe::Core
{
    // 本类只按值传出工具种类，不持有工具对象；前向声明就够，不必把整个标注工具头带进界面层。
    // 限定作用域枚举不带底层类型时默认 int，与 AnnotationTool.h 里的定义相容。
    enum class ToolKind;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{

    /**
     * @brief 隐私打码审核对话框
     * @details 列表只展示检测器给出的类型与脱敏样例，不持有原图与识别原文；
     *          用户未点“应用打码”就关闭时不留任何已接受结果。
     */
    class PrivacyReviewDialog : public QDialog
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造审核对话框
         * @param items 检测器给出的命中项，列表默认全部勾选；空清单也能构造，但不会自动确认
         * @param parent 父窗口，可空
         */
        PrivacyReviewDialog(std::vector<Core::Privacy::PiiItem> items, QWidget *parent = nullptr);

        /**
         * @brief 取用户勾选的命中项
         * @return 按列表顺序排列的勾选项；全不选时为空
         */
        [[nodiscard]] std::vector<Core::Privacy::PiiItem> selectedItems() const;

        /**
         * @brief 取用户选定的打码方式
         * @return 马赛克或高斯模糊
         */
        [[nodiscard]] Core::ToolKind redactionTool() const noexcept;

    private:
        /**
         * @brief 把清单每一行设成给定勾选状态
         * @param checkState 目标状态，全选与全不选共用同一条路径
         */
        void setAllCheckStates(Qt::CheckState checkState);

        Q_DISABLE_COPY(PrivacyReviewDialog)

        std::vector<Core::Privacy::PiiItem> m_items;           ///< 命中项，下标与清单行一一对应
        QListWidget                        *m_list  = nullptr; ///< 逐项勾选清单
        QComboBox                          *m_style = nullptr; ///< 打码方式下拉
    };

} // namespace GyanisScribe::Gui
