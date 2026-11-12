/**
 * @file BugReportDialog.h
 * @brief Bug 报告对话框：截图与打码状态、可编辑正文、Markdown 预览与复制/保存
 * @author Gyanis
 * @date 2026-11-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Annotation/AnnotationTool.h"
#include "Core/BugReport/BugReport.h"
#include "Core/Privacy/PiiTypes.h"

#include <QDialog>
#include <QImage>
#include <QString>

#include <filesystem>
#include <span>
#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;

namespace GyanisScribe::Gui
{

    /**
     * @brief Bug 报告对话框
     * @details 左侧显示将随报告保存的截图副本与自动打码状态，右侧是可编辑的正文与两段环境开关，
     *          底部实时预览保存后的样子：会落图的两档带图片链接，剪贴板里只放文字。本窗不跑文字识别：检查状态由宿主通过
     *          setScanRunning / setScanResult / setScanUnavailable 推进。
     *          自动打码只改内存里的副本，只有用户点保存时才按选定路径落盘；
     *          后端不可用时报告只含文字，不写截图。
     */
    class BugReportDialog : public QDialog
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造报告对话框
         * @param image 截图成品，物理像素；自动打码就在它的副本上做
         * @param environment 随报告写入的环境与窗口信息
         * @param suggestedPath 保存对话框的默认路径
         * @param parent 父窗口，可空
         * @details 构造后停在检查中：复制、保存与调整打码都不可用，直到宿主交回检查结果。
         */
        BugReportDialog(QImage image, Core::BugReport::BugReportEnvironment environment, std::filesystem::path suggestedPath, QWidget *parent = nullptr);

        /**
         * @brief 回到检查中状态
         * @details 清空命中项与选择、还原未打码的截图，并禁用复制、保存与调整打码；
         *          重复发起检查时也走这一入口，窗口不会留着上一轮的打码结果。
         */
        void setScanRunning();

        /**
         * @brief 交回自动检查结果
         * @param items 命中项；空表示检查成功但没检出
         * @details 非空时默认全选并用马赛克打码；打码失败会按「不可用」处理，
         *          报告里只保留文字。报告正文只写处数与打码方式，不写命中项的类型与样例。
         */
        void setScanResult(std::vector<Core::Privacy::PiiItem> items);

        /**
         * @brief 交回「自动打码不可用」的结果
         * @param reason 给用户看的中文原因
         * @details 截图恢复为未打码的副本，保存时只写 Markdown，不写 PNG。
         */
        void setScanUnavailable(const QString &reason);

        /**
         * @brief 取复制用的报告 Markdown
         * @return 整份报告但截图段恒为「未附带」；剪贴板里只有文字，不带截图
         */
        [[nodiscard]] QString reportText() const;

        /**
         * @brief 取状态行当前文字
         * @return 最近一次复制或保存的结果及失败原因
         */
        [[nodiscard]] QString statusText() const;

        /**
         * @brief 取将随报告保存的截图副本
         * @return 已应用自动打码的图；不可用时是原图
         */
        [[nodiscard]] QImage reportImageForTesting() const;

        /**
         * @brief 取当前打码状态
         * @return 状态、处数、打码方式与不可用原因
         */
        [[nodiscard]] Core::BugReport::BugReportRedaction redactionForTesting() const noexcept;

        /**
         * @brief 取构造时收下的环境信息
         * @return 报告使用的环境与窗口字段
         */
        [[nodiscard]] const Core::BugReport::BugReportEnvironment &environmentForTesting() const noexcept;

        /**
         * @brief 把报告文字放进系统剪贴板
         * @return 成功返回空载荷；检查还没回来时返回原因并点亮状态行
         */
        [[nodiscard]] Base::Expected<void> copyReport();

        /**
         * @brief 把报告与截图保存到用户选定的路径
         * @param chosenPath 保存对话框交回的路径；报告固定为 .md，截图固定为同目录同名的 .png
         * @return 成功返回空载荷；检查还没回来、同名 .png 已存在或任一文件写不进去时返回原因并点亮状态行
         * @details 自动打码可用时先写截图再写报告；报告写失败会尽力删掉本次刚写的截图，
         *          不留下没有正文的孤立图片。不可用时只写报告。
         *          同名 .png 已存在时整次保存直接失败，既不覆盖旧图，也不写报告。
         */
        [[nodiscard]] Base::Expected<void> saveReport(const std::filesystem::path &chosenPath);

        /**
         * @brief 给「保存报告…」对话框准备默认路径
         * @return 构造时收下的建议路径的显示文本
         */
        [[nodiscard]] QString suggestedPath() const;

        /**
         * @brief 打开审核窗调整打码范围与方式
         * @details 只在已有命中项时可用；审核窗按当前勾选与打码方式重开，
         *          确认后按新选择重渲染截图副本并刷新报告。
         */
        void adjustRedactions();

    private:
        Q_DISABLE_COPY(BugReportDialog)

        /// 搭出说明、左预览、右表单、底部预览与动作行
        void buildUi();
        /// 把整份报告写进只读预览
        void refreshPreview();
        /// 按「现在保存会得到什么」组装预览：会落图的两档填上建议路径的 PNG 文件名
        [[nodiscard]] QString previewText() const;
        /// 按打码状态启用或禁用复制、保存与调整打码
        void updateActionAvailability();
        /// 把截图副本按 420×320 等比缩进预览标签
        void updatePreviewImage();
        /// 设状态行文字并按结果切换无障碍名称
        void setStatus(const QString &text, bool error);
        /// 收回标题与四段正文
        [[nodiscard]] Core::BugReport::BugReportFields currentFields() const;
        /// 收回两个包含开关
        [[nodiscard]] Core::BugReport::BugReportOptions currentOptions() const;
        /// 按给定框与工具重渲染截图副本并刷新全部状态
        [[nodiscard]] Base::Expected<void> renderRedactions(std::span<const QRect> boxes, Core::ToolKind tool, const std::vector<int> &selectedRows);

        QImage                                m_originalImage;                 ///< 原始截图，物理像素；每次都从这里重渲染
        QImage                                m_reportImage;                   ///< 当前将随报告保存的截图副本
        Core::BugReport::BugReportEnvironment m_environment;                   ///< 随报告写入的环境与窗口信息
        Core::BugReport::BugReportRedaction   m_redaction;                     ///< 当前打码状态
        std::filesystem::path                 m_suggestedPath;                 ///< 保存对话框的默认路径
        std::vector<Core::Privacy::PiiItem>   m_items;                         ///< 命中项，下标与审核窗行号一一对应
        std::vector<int>                      m_selectedRows;                  ///< 当前勾选并已打码的行号
        Core::ToolKind                        m_tool = Core::ToolKind::Mosaic; ///< 当前打码方式

        QLabel      *m_previewLabel    = nullptr; ///< 左侧截图预览
        QLabel      *m_redactionStatus = nullptr; ///< 预览下方的打码状态
        QLabel      *m_status          = nullptr; ///< 动作结果状态行
        QLineEdit   *m_title           = nullptr; ///< 报告标题
        QTextEdit   *m_steps           = nullptr; ///< 复现步骤
        QTextEdit   *m_expected        = nullptr; ///< 期望结果
        QTextEdit   *m_actual          = nullptr; ///< 实际结果
        QTextEdit   *m_notes           = nullptr; ///< 补充说明
        QTextEdit   *m_preview         = nullptr; ///< 整份 Markdown 预览，只读
        QCheckBox   *m_systemToggle    = nullptr; ///< 包含「环境」段
        QCheckBox   *m_windowToggle    = nullptr; ///< 包含「窗口」段
        QPushButton *m_adjustButton    = nullptr; ///< 调整打码
        QPushButton *m_copyButton      = nullptr; ///< 复制报告文字
        QPushButton *m_saveButton      = nullptr; ///< 保存报告
        QPushButton *m_closeButton     = nullptr; ///< 关闭对话框
    };

} // namespace GyanisScribe::Gui
