/**
 * @file MemoryExcludeDialog.h
 * @brief 排除应用对话框：列出索引里出现过的应用与已排除项，勾选后返回完整名单
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Memory/MemoryTypes.h"

#include <QDialog>
#include <QStringList>

#include <vector>

class QListWidget;

namespace GyanisScribe::Gui
{
    /**
     * @brief 排除应用对话框
     * @details 行来自索引的应用分布与设置里已排除的名单；进程名为空的导入条目不可排除，不列出来。
     *          对话框只收勾选结果，写设置与删旧行由持有协调器的控制器执行。
     */
    class MemoryExcludeDialog : public QDialog
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造排除对话框
         * @param apps 索引里的应用分布
         * @param excludedProcesses 设置里已排除的名单，决定初始勾选
         * @param parent 父窗口
         */
        MemoryExcludeDialog(std::vector<Core::MemoryAppCount> apps, const QStringList &excludedProcesses, QWidget *parent = nullptr);

        /**
         * @brief 取用户勾选的应用
         * @return 勾选行的进程名，按列表顺序排列；全不选时为空
         */
        [[nodiscard]] QStringList selectedProcesses() const;

    private:
        /// 确认勾选并关闭对话框
        void confirmSelection();

        std::vector<Core::MemoryAppCount> m_apps;           ///< 索引里的应用分布
        QStringList                       m_excluded;       ///< 初始勾选名单
        QStringList                       m_selected;       ///< 确认时记下的勾选结果
        QListWidget                      *m_list = nullptr; ///< 勾选清单

        Q_DISABLE_COPY(MemoryExcludeDialog)
    };

} // namespace GyanisScribe::Gui
