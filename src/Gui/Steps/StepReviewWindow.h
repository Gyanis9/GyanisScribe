/**
 * @file StepReviewWindow.h
 * @brief 停止录制后的步骤审核窗：改说明、补步、调序、导出与放弃
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Steps/StepTypes.h"

#include <QDialog>
#include <QImage>
#include <QString>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QListWidget;

namespace GyanisScribe::Core
{
    class ISettingsStore;
    class StepSession;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    class Button;

    /**
     * @brief 步骤审核窗
     * @details 左侧列出带最终序号的缩略图，右侧编辑当前一步的说明。
     *          补步走构造时注入的抓屏回调，成功后才追加到会话；导出与放弃都不写历史、
     *          不跑外部命令。文件对话框只出现在导出按钮的槽里，确认框只出现在放弃与关闭路径，
     *          其余公开方法可以直接由协调器与用例调用。
     */
    class StepReviewWindow : public QDialog
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造审核窗
         * @param session 本次会话，步骤表与临时截图都从这里读写
         * @param settings 配置存储，默认导出目录读截图目录设置
         * @param captureForManualStep 补一步时调用一次的抓屏回调，返回整屏图或失败原因
         * @param parent 父窗口，可空
         */
        StepReviewWindow(Core::StepSession &session, Core::ISettingsStore &settings, std::function<Base::Expected<QImage>()> captureForManualStep, QWidget *parent = nullptr);

        /**
         * @brief 析构审核窗
         */
        ~StepReviewWindow() override;

        /**
         * @brief 按会话当前步骤重建列表并恢复选中项
         * @details 录制方在停止后调用；缩略图按最终显示序号现画点击标记，删除或调序后编号跟上。
         */
        void refresh();

        /**
         * @brief 导出单文件 HTML 教程
         * @param filePath 目标文件
         * @return 成功返回空载荷，状态行写已导出路径；失败返回原因，状态行写同一句话
         */
        [[nodiscard]] Base::Expected<void> exportHtml(const std::filesystem::path &filePath);

        /**
         * @brief 导出 PDF 教程
         * @param filePath 目标文件
         * @return 成功返回空载荷，状态行写已导出路径；失败返回原因，状态行写同一句话
         */
        [[nodiscard]] Base::Expected<void> exportPdf(const std::filesystem::path &filePath);

        /**
         * @brief 取状态行当前文字
         * @return 最近一次操作的结果或错误原因
         */
        [[nodiscard]] QString statusText() const;

        /**
         * @brief 走抓屏回调补一步，等同于点「补充一步」
         */
        void addManualStepForTesting();

        /**
         * @brief 跳过确认直接清理并关闭，等同于在确认框里选「是」
         */
        void discardWithoutConfirmationForTesting();

    signals:
        /**
         * @brief 会话已清理、窗口即将关闭
         */
        void discardConfirmed();

    protected:
        /**
         * @brief 关闭窗口前走放弃确认
         * @param event 关闭事件
         * @details 确认后清理临时目录并发 discardConfirmed，再关窗；拒绝时保留窗口与步骤。
         */
        void closeEvent(QCloseEvent *event) override;

        /**
         * @brief Esc 与「放弃记录」同一条确认路径
         */
        void reject() override;

    private:
        Q_DISABLE_COPY(StepReviewWindow)

        /// 搭出列表、说明编辑与按钮
        void buildUi();
        /// 当前选中项的数组下标；没有选中时为空
        [[nodiscard]] std::optional<std::size_t> selectedIndex() const;
        /// 把选中项的说明灌进编辑框
        void loadSelectedDescription();
        /// 按步骤数与选中位置更新按钮可用状态
        void updateActions();
        /// 把编辑框内容写回当前步骤
        void applyDescription();
        /// 删除当前步骤
        void deleteSelected();
        /// 把当前步骤移动 offset 位，负数为上移
        void moveSelected(int offset);
        /// 抓屏并追加一步手动记录
        void addManualStep();
        /// 弹文件对话框并导出 HTML
        void chooseAndExportHtml();
        /// 弹文件对话框并导出 PDF
        void chooseAndExportPdf();
        /// 放弃前向用户确认；没有可放弃的内容时直接放行
        [[nodiscard]] bool confirmDiscard();
        /// 清理会话、发信号并关闭
        void discardNow();
        /// 设状态行文字
        void setStatus(const QString &text, bool error);
        /// 按截图目录设置与当前时刻拼出默认导出路径
        [[nodiscard]] QString suggestedExportPath(const QString &extension) const;

        Core::StepSession                      &m_session;              ///< 本次记录的会话
        Core::ISettingsStore                   &m_settings;             ///< 配置存储，默认导出目录走它
        std::function<Base::Expected<QImage>()> m_captureForManualStep; ///< 补一步用的抓屏回调

        QListWidget *m_list        = nullptr; ///< 左侧步骤列表
        QLineEdit   *m_description = nullptr; ///< 当前步骤说明
        Button      *m_addManual   = nullptr; ///< 补充一步
        Button      *m_delete      = nullptr; ///< 删除当前步骤
        Button      *m_moveUp      = nullptr; ///< 当前步骤上移
        Button      *m_moveDown    = nullptr; ///< 当前步骤下移
        Button      *m_exportHtml  = nullptr; ///< 导出 HTML
        Button      *m_exportPdf   = nullptr; ///< 导出 PDF
        Button      *m_discard     = nullptr; ///< 放弃整次记录
        QLabel      *m_status      = nullptr; ///< 状态行
    };

} // namespace GyanisScribe::Gui
