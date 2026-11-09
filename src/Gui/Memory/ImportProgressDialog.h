/**
 * @file ImportProgressDialog.h
 * @brief 导入旧截图的进度对话框：扫描与入库读数，加一个取消出口
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Memory/MemoryTypes.h"

#include <QDialog>

class QLabel;
class QPushButton;

namespace GyanisScribe::Gui
{
    /**
     * @brief 导入进度对话框
     * @details 模型无关、无模态：导入在后台分块跑，这里只显示读数；
     *          取消只发信号，停止扫描由持有导入任务的协调器执行。
     */
    class ImportProgressDialog : public QDialog
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造进度对话框
         * @param parent 父控件
         */
        explicit ImportProgressDialog(QWidget *parent = nullptr);

        /**
         * @brief 换一组进度读数并重写正文
         * @param progress 已扫描、已入库、跳过、失败与取消状态
         */
        void setProgress(const Core::MemoryImportProgress &progress);

        /**
         * @brief 更新待识别条数
         * @param count 待识别条数；负数表示统计读不出，正文不显示这一行
         */
        void setPendingOcr(int count);

    signals:
        /// 用户点了取消
        void cancelRequested();

    private:
        /// 按当前读数重写正文
        void updateText();

        Core::MemoryImportProgress m_progress;             ///< 最近一次进度
        int                        m_pendingOcr = -1;      ///< 待识别条数，负数表示未知
        QLabel                    *m_text       = nullptr; ///< 进度正文
        QPushButton               *m_cancel     = nullptr; ///< 取消按钮

        Q_DISABLE_COPY(ImportProgressDialog)
    };

} // namespace GyanisScribe::Gui
