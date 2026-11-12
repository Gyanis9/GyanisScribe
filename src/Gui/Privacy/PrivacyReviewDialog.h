/**
 * @file PrivacyReviewDialog.h
 * @brief 隐私打码审核对话框：逐项确认命中项并选择打码方式
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/AnnotationTool.h"
#include "Core/Privacy/PiiTypes.h"

#include <QDialog>

#include <vector>

class QComboBox;
class QListWidget;
class QPushButton;

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
         * @brief 按调用方给定的初始勾选与打码方式构造审核对话框
         * @details 报告窗再次调整同一批命中项时用它重开，不能把用户先前的选择重置成全选与马赛克。
         * @param items 检测器给出的命中项，下标与列表行一一对应
         * @param selectedRows 初始勾选的行号；负数与越界行号被忽略
         * @param tool 初始打码方式，只认马赛克与高斯模糊，其余一律按马赛克
         * @param parent 父窗口，可空
         */
        PrivacyReviewDialog(std::vector<Core::Privacy::PiiItem> items, const std::vector<int> &selectedRows, Core::ToolKind tool, QWidget *parent = nullptr);

        /**
         * @brief 取当前勾选的行号
         * @return 按列表顺序排列的行号；全不选时为空
         */
        [[nodiscard]] std::vector<int> selectedRows() const;

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
         * @brief 按给定初始勾选与打码方式搭建界面
         * @param selectedRows 初始勾选的行号；负数与越界行号被忽略
         * @param tool 初始打码方式
         */
        void initializeUi(const std::vector<int> &selectedRows, Core::ToolKind tool);

        /**
         * @brief 把清单每一行设成给定勾选状态
         * @param checkState 目标状态，全选与全不选共用同一条路径
         */
        void setAllCheckStates(Qt::CheckState checkState);

        /// 一项都没勾时把「应用打码」灰掉：空批次落笔只会白白关一次对话框
        void updateApplyEnabled();

        Q_DISABLE_COPY(PrivacyReviewDialog)

        std::vector<Core::Privacy::PiiItem> m_items;           ///< 命中项，下标与清单行一一对应
        QListWidget                        *m_list  = nullptr; ///< 逐项勾选清单
        QComboBox                          *m_style = nullptr; ///< 打码方式下拉
        QPushButton                        *m_apply = nullptr; ///< 应用打码按钮，随勾选数启用或灰掉
    };

} // namespace GyanisScribe::Gui
