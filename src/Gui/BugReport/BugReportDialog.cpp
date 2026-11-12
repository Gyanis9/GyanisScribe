#include "Gui/BugReport/BugReportDialog.h"

#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/Privacy/RedactImage.h"
#include "Gui/Controls/Button.h"
#include "Gui/Controls/CheckBox.h"
#include "Gui/Controls/LineEdit.h"
#include "Gui/Controls/TextEdit.h"
#include "Gui/Privacy/PrivacyReviewDialog.h"
#include "Gui/Theme/DesignTokens.h"

#include <QCheckBox>
#include <QClipboard>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <system_error>
#include <utility>

namespace
{
    using GyanisScribe::Base::uiFormat;
    using GyanisScribe::Base::uiText;
    using GyanisScribe::Base::uiTextUtf8;
    using GyanisScribe::Gui::applyTextRole;
    using GyanisScribe::Gui::TextRole;

    /**
     * @brief 表单里的小节标题
     * @param parent 归属控件
     * @param text 已经过 uiText 的标题
     * @return 归 parent 所有的标题标签
     */
    QLabel *makeSectionLabel(QWidget *parent, const QString &text)
    {
        auto *label = new QLabel{text, parent};
        applyTextRole(*label, TextRole::Body);
        auto font = label->font();
        font.setWeight(QFont::DemiBold);
        label->setFont(font);
        return label;
    }
} // namespace

namespace GyanisScribe::Gui
{

    BugReportDialog::BugReportDialog(QImage image, Core::BugReport::BugReportEnvironment environment, std::filesystem::path suggestedPath, QWidget *parent) :
        QDialog(parent), m_originalImage(std::move(image)), m_reportImage(m_originalImage), m_environment(std::move(environment)), m_suggestedPath(std::move(suggestedPath))
    {
        setWindowTitle(uiText("Bug 报告"));
        setWindowModality(Qt::WindowModal);
        // 顶层窗口自己铺底：样式表只给具名的根上底色
        setObjectName(QStringLiteral("appRoot"));
        setAttribute(Qt::WA_StyledBackground, true);

        buildUi();
        // 初始尺寸夹进当前屏幕的可用区域：小屏与高缩放桌面上固定 1000×760 会把窗底推出屏外
        const auto *screen    = parentWidget() != nullptr ? parentWidget()->screen() : QGuiApplication::primaryScreen();
        const auto  available = screen != nullptr ? screen->availableGeometry().size() : QSize{};
        resize(Core::BugReport::bugReportDialogInitialSize(available));
        setScanRunning();
    }

    void BugReportDialog::setScanRunning()
    {
        m_items.clear();
        m_selectedRows.clear();
        m_redaction   = Core::BugReport::BugReportRedaction{};
        m_reportImage = m_originalImage;
        m_redactionStatus->setText(uiText("正在检查敏感信息…"));
        // 上一轮的动作结果对新一轮不成立：留着会让用户以为这次已经存过了
        setStatus(QString{}, false);
        updatePreviewImage();
        refreshPreview();
        updateActionAvailability();
    }

    void BugReportDialog::setScanResult(std::vector<Core::Privacy::PiiItem> items)
    {
        if (items.empty())
        {
            m_items.clear();
            m_selectedRows.clear();
            m_redaction       = Core::BugReport::BugReportRedaction{};
            m_redaction.state = Core::BugReport::RedactionState::NoHits;
            m_reportImage     = m_originalImage;
            m_redactionStatus->setText(uiText("未检出可打码的敏感信息。请自行检查。"));
            updatePreviewImage();
            refreshPreview();
            updateActionAvailability();
            return;
        }

        m_items = std::move(items);
        m_selectedRows.resize(m_items.size());
        for (std::size_t index = 0; index < m_items.size(); ++index)
        {
            m_selectedRows[index] = static_cast<int>(index);
        }

        std::vector<QRect> boxes;
        boxes.reserve(m_items.size());
        for (const auto &item: m_items)
        {
            boxes.push_back(item.physicalBox);
        }

        if (const auto rendered = renderRedactions(boxes, Core::ToolKind::Mosaic, m_selectedRows); !rendered)
        {
            // setScanUnavailable 自己会加「自动打码不可用：」前缀：这里只交底层的失败原因
            setScanUnavailable(QString::fromStdString(rendered.error().message));
        }
    }

    void BugReportDialog::setScanUnavailable(const QString &reason)
    {
        m_items.clear();
        m_selectedRows.clear();
        m_redaction   = Core::BugReport::BugReportRedaction{Core::BugReport::RedactionState::Unavailable, 0, {}, reason};
        m_reportImage = m_originalImage;
        m_redactionStatus->setText(uiText("自动打码不可用：%1。本次不会保存截图，报告只含文字。").arg(reason));
        updatePreviewImage();
        refreshPreview();
        updateActionAvailability();
    }

    QString BugReportDialog::reportText() const
    {
        return Core::BugReport::buildBugReportMarkdown(m_environment, currentFields(), m_redaction, currentOptions(), QString{});
    }

    QString BugReportDialog::statusText() const
    {
        return m_status->text();
    }

    QImage BugReportDialog::reportImageForTesting() const
    {
        return m_reportImage;
    }

    Core::BugReport::BugReportRedaction BugReportDialog::redactionForTesting() const noexcept
    {
        return m_redaction;
    }

    const Core::BugReport::BugReportEnvironment &BugReportDialog::environmentForTesting() const noexcept
    {
        return m_environment;
    }

    Base::Expected<void> BugReportDialog::copyReport()
    {
        if (m_redaction.state == Core::BugReport::RedactionState::Pending)
        {
            const auto error = Base::Error{Base::ErrorCode::InvalidArgument, uiTextUtf8("正在检查敏感信息，请稍候再复制")};
            setStatus(QString::fromStdString(error.message), true);
            return std::unexpected(error);
        }

        QGuiApplication::clipboard()->setText(reportText());
        setStatus(uiText("已复制报告文字到剪贴板"), false);
        return {};
    }

    Base::Expected<void> BugReportDialog::saveReport(const std::filesystem::path &chosenPath)
    {
        if (m_redaction.state == Core::BugReport::RedactionState::Pending)
        {
            const auto error = Base::Error{Base::ErrorCode::InvalidArgument, uiTextUtf8("正在检查敏感信息，请稍候再保存")};
            setStatus(QString::fromStdString(error.message), true);
            return std::unexpected(error);
        }

        const auto fail = [this](const Base::Error &error)
        {
            setStatus(QString::fromStdString(error.message), true);
            return std::unexpected(error);
        };

        const auto paths        = Core::BugReport::bugReportPathsForDialog(chosenPath);
        const bool includeImage = m_redaction.state == Core::BugReport::RedactionState::Masked || m_redaction.state == Core::BugReport::RedactionState::NoHits;

        // 同名的 PNG 已在盘上时整次保存都不做：写图会把旧图盖掉，而报告写失败后的回收又可能删掉它。
        // 先要求用户换一个名字，盘上任何文件都不会被本功能改写或删除。
        std::error_code existsError;
        if (std::filesystem::exists(paths.screenshot, existsError))
        {
            const auto error =
                    Base::Error{Base::ErrorCode::FileWriteFailed, uiText("同名图片已存在：%1。请换一个名字后重试").arg(Base::Paths::toDisplayText(paths.screenshot)).toStdString()};
            return fail(error);
        }

        bool screenshotWritten = false;
        if (includeImage)
        {
            const auto screenshotPathText = Base::Paths::toDisplayText(paths.screenshot);
            const auto writeFailure =
                    Base::Error{Base::ErrorCode::FileWriteFailed, uiFormat(uiTextUtf8("「{}」无法写入。请检查该目录是否可写。"), screenshotPathText.toStdString())};
            QSaveFile file{screenshotPathText};
            if (!file.open(QIODevice::WriteOnly))
            {
                return fail(writeFailure);
            }
            if (!m_reportImage.save(&file, "PNG"))
            {
                file.cancelWriting();
                return fail(writeFailure);
            }
            if (!file.commit())
            {
                return fail(writeFailure);
            }
            screenshotWritten = true;
        }

        const auto screenshotFileName = includeImage ? QString::fromStdWString(paths.screenshot.filename().wstring()) : QString{};
        const auto markdown           = Core::BugReport::buildBugReportMarkdown(m_environment, currentFields(), m_redaction, currentOptions(), screenshotFileName);

        const auto markdownPathText = Base::Paths::toDisplayText(paths.markdown);
        const auto markdownFailure  = Base::Error{Base::ErrorCode::FileWriteFailed, uiFormat(uiTextUtf8("「{}」无法写入。请检查该目录是否可写。"), markdownPathText.toStdString())};
        // 前面已确认同名 PNG 不存在，所以这里删掉的就是本次刚写出的那张，不会动到旧图
        const auto removeWrittenScreenshot = [screenshotWritten, &paths]
        {
            if (!screenshotWritten)
            {
                return;
            }
            std::error_code ignored;
            std::filesystem::remove(paths.screenshot, ignored);
        };

        QSaveFile file{markdownPathText};
        if (!file.open(QIODevice::WriteOnly))
        {
            removeWrittenScreenshot();
            return fail(markdownFailure);
        }
        const auto bytes = markdown.toUtf8();
        if (file.write(bytes) != bytes.size())
        {
            file.cancelWriting();
            removeWrittenScreenshot();
            return fail(markdownFailure);
        }
        if (!file.commit())
        {
            removeWrittenScreenshot();
            return fail(markdownFailure);
        }

        setStatus(includeImage ? uiText("已保存报告与截图：%1").arg(markdownPathText) : uiText("已保存报告文字（未包含截图）：%1").arg(markdownPathText), false);
        return {};
    }

    QString BugReportDialog::suggestedPath() const
    {
        return Base::Paths::toDisplayText(m_suggestedPath);
    }

    void BugReportDialog::adjustRedactions()
    {
        if (m_redaction.state != Core::BugReport::RedactionState::Masked || m_items.empty())
        {
            return;
        }

        PrivacyReviewDialog review{m_items, m_selectedRows, m_tool, this};
        if (review.exec() != QDialog::Accepted)
        {
            return;
        }

        const auto rows = review.selectedRows();
        if (rows.empty())
        {
            return;
        }

        std::vector<QRect> boxes;
        boxes.reserve(rows.size());
        for (const int row: rows)
        {
            boxes.push_back(m_items[static_cast<std::size_t>(row)].physicalBox);
        }

        if (const auto rendered = renderRedactions(boxes, review.redactionTool(), rows); !rendered)
        {
            setScanUnavailable(QString::fromStdString(rendered.error().message));
        }
    }

    void BugReportDialog::buildUi()
    {
        auto *root = new QVBoxLayout{this};
        root->setContentsMargins(kSpacingLarge, kSpacingLarge, kSpacingLarge, kSpacingLarge);
        root->setSpacing(kSpacingMedium);

        auto *privacyNote = new QLabel{uiText("只在本机生成，不会上传；自动打码只处理识别到的敏感信息，发送前请自行检查。"), this};
        privacyNote->setObjectName(QStringLiteral("bugReportPrivacyNote"));
        privacyNote->setWordWrap(true);
        privacyNote->setAccessibleName(uiText("Bug 报告说明"));
        applyTextRole(*privacyNote, TextRole::Body);
        root->addWidget(privacyNote);

        auto *mainRow = new QHBoxLayout;
        mainRow->setSpacing(kSpacingLarge);
        root->addLayout(mainRow);

        auto *leftColumn = new QVBoxLayout;
        leftColumn->setSpacing(kSpacingSmall);
        mainRow->addLayout(leftColumn, 1);

        m_previewLabel = new QLabel{this};
        m_previewLabel->setObjectName(QStringLiteral("bugReportPreviewImage"));
        m_previewLabel->setMinimumSize(420, 320);
        m_previewLabel->setScaledContents(false);
        m_previewLabel->setAlignment(Qt::AlignCenter);
        // 缩放后的图会反过来影响标签的建议尺寸：忽略它，版面大小只由左栏剩余的宽度决定
        m_previewLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        m_previewLabel->setAccessibleName(uiText("截图预览"));
        m_previewLabel->setToolTip(uiText("此处显示将随报告保存的截图副本；自动打码直接改在这张副本上。"));
        leftColumn->addWidget(m_previewLabel, 1);

        m_redactionStatus = new QLabel{this};
        m_redactionStatus->setObjectName(QStringLiteral("bugReportRedactionStatus"));
        m_redactionStatus->setWordWrap(true);
        m_redactionStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_redactionStatus->setAccessibleName(uiText("打码状态"));
        applyTextRole(*m_redactionStatus, TextRole::Body);
        leftColumn->addWidget(m_redactionStatus);

        auto *form = new QFormLayout;
        form->setContentsMargins(0, 0, 0, 0);
        form->setVerticalSpacing(kSpacingSmall);
        form->setHorizontalSpacing(kSpacingSmall);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        mainRow->addLayout(form, 1);

        m_title = new LineEdit{this};
        m_title->setObjectName(QStringLiteral("bugReportTitle"));
        m_title->setAccessibleName(uiText("报告标题"));
        m_title->setPlaceholderText(uiText("一句话概括问题"));
        form->addRow(uiText("标题"), m_title);

        const auto makeEditor = [this](const QString &objectName, const QString &accessibleName, const QString &placeholder)
        {
            auto *editor = new TextEdit{this};
            editor->setObjectName(objectName);
            editor->setAccessibleName(accessibleName);
            editor->setPlaceholderText(placeholder);
            editor->setAcceptRichText(false);
            editor->setMinimumHeight(54);
            editor->setMaximumHeight(88);
            return editor;
        };

        m_steps    = makeEditor(QStringLiteral("bugReportSteps"), uiText("复现步骤"), uiText("按顺序写清每一步操作"));
        m_expected = makeEditor(QStringLiteral("bugReportExpected"), uiText("期望结果"), uiText("原本应该发生什么"));
        m_actual   = makeEditor(QStringLiteral("bugReportActual"), uiText("实际结果"), uiText("实际发生了什么"));
        m_notes    = makeEditor(QStringLiteral("bugReportNotes"), uiText("补充说明"), uiText("可选：出现频率、影响范围等"));
        form->addRow(uiText("复现步骤"), m_steps);
        form->addRow(uiText("期望结果"), m_expected);
        form->addRow(uiText("实际结果"), m_actual);
        form->addRow(uiText("补充说明"), m_notes);

        form->addRow(makeSectionLabel(this, uiText("包含内容")));
        auto *toggleRow    = new QWidget{this};
        auto *toggleLayout = new QHBoxLayout{toggleRow};
        toggleLayout->setContentsMargins(0, 0, 0, 0);
        toggleLayout->setSpacing(kSpacingMedium);
        m_systemToggle = new CheckBox{toggleRow};
        m_systemToggle->setObjectName(QStringLiteral("bugReportSystemToggle"));
        m_systemToggle->setText(uiText("包含系统与环境信息"));
        m_systemToggle->setAccessibleName(uiText("包含系统与环境信息"));
        m_systemToggle->setToolTip(uiText("报告里是否带上系统、Qt、屏幕与截图尺寸；关闭后这一段整段省略。"));
        m_windowToggle = new CheckBox{toggleRow};
        m_windowToggle->setObjectName(QStringLiteral("bugReportWindowToggle"));
        m_windowToggle->setText(uiText("包含前台窗口信息"));
        m_windowToggle->setAccessibleName(uiText("包含前台窗口信息"));
        m_windowToggle->setToolTip(uiText("报告里是否带上截图时的前台窗口标题与进程名；截图本身可能暴露它们。"));
        toggleLayout->addWidget(m_systemToggle);
        toggleLayout->addWidget(m_windowToggle);
        toggleLayout->addStretch(1);
        form->addRow(toggleRow);

        const bool hasWindowInfo = !m_environment.windowTitle.isEmpty() || !m_environment.processName.isEmpty();
        m_systemToggle->setChecked(true);
        m_windowToggle->setChecked(hasWindowInfo);
        m_windowToggle->setEnabled(hasWindowInfo);

        root->addWidget(makeSectionLabel(this, uiText("报告预览（Markdown）")));
        m_preview = new TextEdit{this};
        m_preview->setObjectName(QStringLiteral("bugReportPreview"));
        m_preview->setAccessibleName(uiText("报告预览"));
        m_preview->setReadOnly(true);
        m_preview->setMinimumHeight(140);
        root->addWidget(m_preview, 1);

        auto *buttonRow = new QHBoxLayout;
        buttonRow->setSpacing(kSpacingSmall);
        m_adjustButton = new Button{uiText("调整打码…"), this};
        m_adjustButton->setObjectName(QStringLiteral("bugReportAdjust"));
        m_adjustButton->setAccessibleName(uiText("调整打码"));
        m_adjustButton->setToolTip(uiText("重新勾选要打码的命中项，或改用高斯模糊；确认后报告随之更新。"));
        // 调整打码改的是左边这张预览：摆在打码状态行下面，用户看完状态就能就地改
        leftColumn->addWidget(m_adjustButton);
        m_copyButton = new Button{uiText("复制报告"), this};
        m_copyButton->setObjectName(QStringLiteral("bugReportCopy"));
        m_copyButton->setAccessibleName(uiText("复制报告"));
        m_copyButton->setToolTip(uiText("把预览里的报告文字复制到剪贴板；只含文字，不含截图。"));
        m_saveButton = new Button{uiText("保存报告…"), this};
        m_saveButton->setObjectName(QStringLiteral("bugReportSave"));
        m_saveButton->setAccessibleName(uiText("保存报告"));
        m_saveButton->setToolTip(uiText("把报告保存为 Markdown；自动打码可用时会在同目录另存一张 PNG 截图。"));
        m_closeButton = new Button{uiText("关闭"), this};
        m_closeButton->setObjectName(QStringLiteral("bugReportClose"));
        m_closeButton->setAccessibleName(uiText("关闭报告"));
        m_closeButton->setToolTip(uiText("关闭报告对话框；未保存的内容不会保留。"));
        buttonRow->addWidget(m_copyButton);
        buttonRow->addWidget(m_saveButton);
        buttonRow->addStretch(1);
        buttonRow->addWidget(m_closeButton);
        root->addLayout(buttonRow);

        m_status = new QLabel{this};
        m_status->setObjectName(QStringLiteral("bugReportStatus"));
        m_status->setWordWrap(true);
        m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_status->setAccessibleName(uiText("操作状态"));
        root->addWidget(m_status);

        QObject::connect(m_title, &QLineEdit::textChanged, this, [this](const QString &) { refreshPreview(); });
        QObject::connect(m_steps, &QTextEdit::textChanged, this, [this] { refreshPreview(); });
        QObject::connect(m_expected, &QTextEdit::textChanged, this, [this] { refreshPreview(); });
        QObject::connect(m_actual, &QTextEdit::textChanged, this, [this] { refreshPreview(); });
        QObject::connect(m_notes, &QTextEdit::textChanged, this, [this] { refreshPreview(); });
        QObject::connect(m_systemToggle, &QCheckBox::toggled, this, [this](bool) { refreshPreview(); });
        QObject::connect(m_windowToggle, &QCheckBox::toggled, this, [this](bool) { refreshPreview(); });

        QObject::connect(m_adjustButton, &QPushButton::clicked, this, &BugReportDialog::adjustRedactions);
        QObject::connect(m_copyButton, &QPushButton::clicked, this, [this] { (void) copyReport(); });
        QObject::connect(m_saveButton, &QPushButton::clicked, this,
                         [this]
                         {
                             const auto chosen = QFileDialog::getSaveFileName(this, uiText("保存 Bug 报告"), suggestedPath(), uiText("Markdown 文件 (*.md)"));
                             if (chosen.isEmpty())
                             {
                                 return;
                             }
                             (void) saveReport(std::filesystem::path{Base::Paths::fromDisplayText(chosen)});
                         });
        QObject::connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    }

    void BugReportDialog::refreshPreview()
    {
        const QSignalBlocker blocker{m_preview};
        m_preview->setPlainText(reportText());
    }

    void BugReportDialog::updateActionAvailability()
    {
        const bool canPlace = m_redaction.state != Core::BugReport::RedactionState::Pending;
        m_copyButton->setEnabled(canPlace);
        m_saveButton->setEnabled(canPlace);
        m_adjustButton->setEnabled(m_redaction.state == Core::BugReport::RedactionState::Masked && !m_items.empty());
    }

    void BugReportDialog::updatePreviewImage()
    {
        if (m_reportImage.isNull())
        {
            m_previewLabel->clear();
            return;
        }
        m_previewLabel->setPixmap(QPixmap::fromImage(m_reportImage).scaled(QSize{420, 320}, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    void BugReportDialog::setStatus(const QString &text, const bool error)
    {
        m_status->setText(text);
        m_status->setAccessibleName(error ? uiText("操作失败原因") : uiText("操作状态"));
    }

    Core::BugReport::BugReportFields BugReportDialog::currentFields() const
    {
        Core::BugReport::BugReportFields fields;
        fields.title    = m_title->text();
        fields.steps    = m_steps->toPlainText();
        fields.expected = m_expected->toPlainText();
        fields.actual   = m_actual->toPlainText();
        fields.notes    = m_notes->toPlainText();
        return fields;
    }

    Core::BugReport::BugReportOptions BugReportDialog::currentOptions() const
    {
        Core::BugReport::BugReportOptions options;
        options.includeSystem = m_systemToggle->isChecked();
        options.includeWindow = m_windowToggle->isChecked();
        return options;
    }

    Base::Expected<void> BugReportDialog::renderRedactions(const std::span<const QRect> boxes, const Core::ToolKind tool, const std::vector<int> &selectedRows)
    {
        const auto result = Core::Privacy::redactImage(m_originalImage, boxes, tool, m_environment.logicalSize, Core::kMosaicBlockSizes[Core::kDefaultMosaicBlockStep]);
        if (!result)
        {
            return std::unexpected(result.error());
        }

        m_reportImage  = result->image;
        m_selectedRows = selectedRows;
        m_tool         = tool;
        m_redaction    = Core::BugReport::BugReportRedaction{
                Core::BugReport::RedactionState::Masked, result->appliedCount, tool == Core::ToolKind::Blur ? uiText("高斯模糊") : uiText("马赛克"), {}};
        m_redactionStatus->setText(uiText("已自动打码 %1 处敏感信息（%2）；请自行检查。").arg(result->appliedCount).arg(m_redaction.toolLabel));
        updatePreviewImage();
        refreshPreview();
        updateActionAvailability();
        return {};
    }

} // namespace GyanisScribe::Gui
