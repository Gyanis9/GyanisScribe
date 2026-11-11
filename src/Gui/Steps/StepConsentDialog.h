/**
 * @file StepConsentDialog.h
 * @brief 第一次录制步骤前的说明与同意
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QDialog>
#include <QString>

namespace GyanisScribe::Gui
{

    /**
     * @brief 首次录制的说明对话框
     * @details 正文逐项写明会记录什么、只留在本机；默认按钮是取消，
     *          误按回车或 Esc 都不会开始录制。是否开录由调用方按 exec() 的返回值判断。
     */
    class StepConsentDialog : public QDialog
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造说明对话框
         * @param parent 父窗口，默认无
         */
        explicit StepConsentDialog(QWidget *parent = nullptr);

        /**
         * @brief 析构说明对话框
         */
        ~StepConsentDialog() override;

        /**
         * @brief 正文原文，供用例核对记录范围是否说全
         * @return 与对话框正文同源的一段文字
         */
        [[nodiscard]] static QString consentTextForTesting();

    private:
        Q_DISABLE_COPY(StepConsentDialog)
    };

} // namespace GyanisScribe::Gui
