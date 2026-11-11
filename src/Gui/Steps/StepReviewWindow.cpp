#include "Gui/Steps/StepReviewWindow.h"

#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Steps/StepHtmlBuilder.h"
#include "Core/Steps/StepSession.h"
#include "Gui/Controls/Button.h"
#include "Gui/Controls/LineEdit.h"
#include "Gui/Controls/ListWidget.h"
#include "Gui/Steps/StepPdfExporter.h"
#include "Gui/Theme/DesignTokens.h"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace GyanisScribe::Gui
{
    namespace
    {
        /// 步骤类型的中文标签
        [[nodiscard]] QString stepKindLabel(const Core::StepKind kind)
        {
            switch (kind)
            {
                case Core::StepKind::Click:
                    return Base::uiText("点击");
                case Core::StepKind::WindowChange:
                    return Base::uiText("切换窗口");
                case Core::StepKind::Manual:
                    return Core::manualStepDescription();
            }
            return QString();
        }

        /// 列表项的说明：空说明只写类型标签，不把元素名或窗口标题补回来
        [[nodiscard]] QString stepTitle(const Core::RecordedStep &step)
        {
            const QString description = step.description.trimmed();
            return description.isEmpty() ? stepKindLabel(step.kind) : description;
        }

        /// 列表行的「序号. 类型 · 说明」；说明为空或与类型标签相同时只写类型
        [[nodiscard]] QString stepRowText(const Core::RecordedStep &step, const int displayNumber)
        {
            const QString type        = stepKindLabel(step.kind);
            const QString description = step.description.trimmed();
            if (description.isEmpty() || description == type)
            {
                return Base::uiText("%1. %2").arg(displayNumber).arg(type);
            }
            return Base::uiText("%1. %2 · %3").arg(displayNumber).arg(type, description);
        }

        /// 文件对话框交回的名字没有后缀时补一个，用户只改了主名也不会导出成无后缀文件
        [[nodiscard]] std::filesystem::path withSuffix(const std::filesystem::path &path, const QString &extension)
        {
            if (path.extension().empty())
            {
                auto corrected = path;
                corrected += extension.toStdWString();
                return corrected;
            }
            return path;
        }
    } // namespace

    StepReviewWindow::StepReviewWindow(Core::StepSession &session, Core::ISettingsStore &settings, std::function<Base::Expected<QImage>()> captureForManualStep, QWidget *parent) :
        QDialog(parent), m_session(session), m_settings(settings), m_captureForManualStep(std::move(captureForManualStep))
    {
        setWindowTitle(Base::uiText("步骤记录"));
        setObjectName(QStringLiteral("stepsReview"));
        setMinimumSize(920, 560);
        setSizeGripEnabled(true);

        buildUi();
        refresh();
    }

    StepReviewWindow::~StepReviewWindow() = default;

    void StepReviewWindow::buildUi()
    {
        // 顶层窗口的底色由 appRoot 那条样式表规则铺；本窗对象名留给调用方与用例定位
        auto *outer = new QVBoxLayout{this};
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        auto *panel = new QWidget{this};
        panel->setObjectName(QStringLiteral("appRoot"));
        outer->addWidget(panel);

        auto *root = new QVBoxLayout{panel};
        root->setContentsMargins(kSpacingLarge, kSpacingLarge, kSpacingLarge, kSpacingLarge);
        root->setSpacing(kSpacingMedium);

        auto *hint = new QLabel{Base::uiText("核对每一步的说明；可以补一步、调序或删除，确认后导出教程。"), panel};
        hint->setWordWrap(true);
        root->addWidget(hint);

        auto *content = new QHBoxLayout;
        content->setSpacing(kSpacingLarge);
        root->addLayout(content, 1);

        m_list = new ListWidget{panel};
        m_list->setObjectName(QStringLiteral("stepList"));
        m_list->setAccessibleName(Base::uiText("步骤列表"));
        m_list->setSelectionMode(QAbstractItemView::SingleSelection);
        m_list->setIconSize(QSize{Core::kStepThumbnailWidth, Core::kStepThumbnailWidth * 9 / 16});
        m_list->setTextElideMode(Qt::ElideRight);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_list->setMinimumWidth(Core::kStepThumbnailWidth + 2 * kSpacingLarge);
        content->addWidget(m_list, 1);

        auto *editor = new QVBoxLayout;
        editor->setSpacing(kSpacingSmall);
        content->addLayout(editor, 1);

        auto *descriptionLabel = new QLabel{Base::uiText("步骤说明"), panel};
        editor->addWidget(descriptionLabel);

        m_description = new LineEdit{panel};
        m_description->setObjectName(QStringLiteral("stepDescription"));
        m_description->setAccessibleName(Base::uiText("步骤说明"));
        m_description->setMaxLength(Core::kMaximumStepDescriptionLength);
        m_description->setPlaceholderText(Base::uiText("用一句话说明这一步做了什么"));
        editor->addWidget(m_description);

        auto *orderRow = new QHBoxLayout;
        orderRow->setSpacing(kSpacingSmall);
        editor->addLayout(orderRow);

        m_delete = new Button{Base::uiText("删除"), panel};
        m_delete->setObjectName(QStringLiteral("stepDelete"));
        m_delete->setToolTip(Base::uiText("删除当前步骤；后面的序号会自动跟上。"));
        orderRow->addWidget(m_delete);

        m_moveUp = new Button{Base::uiText("上移"), panel};
        m_moveUp->setObjectName(QStringLiteral("stepMoveUp"));
        orderRow->addWidget(m_moveUp);

        m_moveDown = new Button{Base::uiText("下移"), panel};
        m_moveDown->setObjectName(QStringLiteral("stepMoveDown"));
        orderRow->addWidget(m_moveDown);
        orderRow->addStretch(1);

        m_addManual = new Button{Base::uiText("补充一步"), panel};
        m_addManual->setObjectName(QStringLiteral("stepAddManual"));
        m_addManual->setToolTip(Base::uiText("立即抓一张整屏画面，作为新的一步追加到末尾。"));
        editor->addWidget(m_addManual);
        editor->addStretch(1);

        m_exportHtml = new Button{Base::uiText("导出 HTML…"), panel};
        m_exportHtml->setObjectName(QStringLiteral("stepExportHtml"));
        m_exportHtml->setToolTip(Base::uiText("把步骤与截图写成单个 HTML 文件，图片内嵌其中。"));
        editor->addWidget(m_exportHtml);

        m_exportPdf = new Button{Base::uiText("导出 PDF…"), panel};
        m_exportPdf->setObjectName(QStringLiteral("stepExportPdf"));
        m_exportPdf->setToolTip(Base::uiText("把步骤与截图写成 A4 版式的 PDF。"));
        editor->addWidget(m_exportPdf);

        auto *bottom = new QHBoxLayout;
        bottom->setSpacing(kSpacingSmall);
        root->addLayout(bottom);

        m_status = new QLabel{panel};
        m_status->setObjectName(QStringLiteral("stepStatus"));
        m_status->setAccessibleName(Base::uiText("步骤状态"));
        m_status->setWordWrap(true);
        m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        bottom->addWidget(m_status, 1);

        m_discard = new Button{Base::uiText("放弃记录"), panel};
        m_discard->setObjectName(QStringLiteral("stepDiscard"));
        m_discard->setToolTip(Base::uiText("删除本次记录与全部临时截图。"));
        bottom->addWidget(m_discard);

        QObject::connect(m_list, &QListWidget::currentRowChanged, this,
                         [this](int)
                         {
                             // 选中项换走时先把上一行尚未落库的编辑写回它自己那一行
                             if (m_editIndex)
                             {
                                 applyDescription();
                             }
                             loadSelectedDescription();
                             updateActions();
                         });
        QObject::connect(m_description, &QLineEdit::textEdited, this,
                         [this]
                         {
                             if (!m_editIndex)
                             {
                                 m_editIndex = selectedIndex();
                             }
                         });
        QObject::connect(m_description, &QLineEdit::editingFinished, this, &StepReviewWindow::applyDescription);
        QObject::connect(m_delete, &QPushButton::clicked, this, &StepReviewWindow::deleteSelected);
        QObject::connect(m_moveUp, &QPushButton::clicked, this, [this] { moveSelected(-1); });
        QObject::connect(m_moveDown, &QPushButton::clicked, this, [this] { moveSelected(1); });
        QObject::connect(m_addManual, &QPushButton::clicked, this, &StepReviewWindow::addManualStep);
        QObject::connect(m_exportHtml, &QPushButton::clicked, this, &StepReviewWindow::chooseAndExportHtml);
        QObject::connect(m_exportPdf, &QPushButton::clicked, this, &StepReviewWindow::chooseAndExportPdf);
        QObject::connect(m_discard, &QPushButton::clicked, this,
                         [this]
                         {
                             if (confirmDiscard())
                             {
                                 discardNow();
                             }
                         });
    }

    void StepReviewWindow::refresh()
    {
        const int preferredRow = m_list->currentRow();
        m_list->clear();

        for (std::size_t position = 0; position < m_session.count(); ++position)
        {
            const Core::RecordedStep &step          = m_session.steps()[position];
            const int                 displayNumber = static_cast<int>(position) + 1;

            auto *item = new QListWidgetItem{m_list};
            item->setText(stepRowText(step, displayNumber));
            item->setToolTip(step.windowTitle.isEmpty() ? stepTitle(step) : Base::uiText("窗口：%1").arg(step.windowTitle));

            // 列表图标只用录制时留在内存里的缩略图：刷新不读盘、不现画标记，
            // 删除或调序一步不会把整批原图重新解码一遍
            if (!step.thumbnail.isNull())
            {
                const int width = std::min(Core::kStepThumbnailWidth, step.thumbnail.width());
                item->setIcon(QIcon{QPixmap::fromImage(step.thumbnail.scaledToWidth(width, Qt::SmoothTransformation))});
            }
        }

        if (m_list->count() > 0)
        {
            m_list->setCurrentRow(std::clamp(preferredRow, 0, m_list->count() - 1));
        }
        loadSelectedDescription();
        updateActions();
    }

    std::optional<std::size_t> StepReviewWindow::selectedIndex() const
    {
        const int row = m_list->currentRow();
        if (row < 0 || static_cast<std::size_t>(row) >= m_session.count())
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(row);
    }

    void StepReviewWindow::loadSelectedDescription()
    {
        const auto index = selectedIndex();
        if (!index)
        {
            m_description->clear();
            m_description->setEnabled(false);
            return;
        }

        m_description->setEnabled(true);
        const QString &description = m_session.steps()[*index].description;
        if (m_description->text() != description)
        {
            m_description->setText(description);
        }
    }

    void StepReviewWindow::updateActions()
    {
        const auto index    = selectedIndex();
        const bool hasSteps = m_session.count() > 0;

        m_delete->setEnabled(index.has_value());
        m_moveUp->setEnabled(index.has_value() && *index > 0);
        m_moveDown->setEnabled(index.has_value() && *index + 1 < m_session.count());
        m_exportHtml->setEnabled(hasSteps);
        m_exportPdf->setEnabled(hasSteps);
        m_addManual->setEnabled(m_session.count() < static_cast<std::size_t>(Core::kMaximumSteps));
        m_description->setEnabled(index.has_value());
    }

    void StepReviewWindow::applyDescription()
    {
        // 写回开始编辑时记下的那一行：期间选中项换了，这段文字也不会落到别的步骤上
        const auto index = m_editIndex ? m_editIndex : selectedIndex();
        m_editIndex.reset();
        if (!index || *index >= m_session.count())
        {
            return;
        }

        const auto written = m_session.setDescription(*index, m_description->text());
        if (!written)
        {
            setStatus(QString::fromStdString(written.error().message), true);
            return;
        }

        const Core::RecordedStep &step = m_session.steps()[*index];
        if (auto *item = m_list->item(static_cast<int>(*index)))
        {
            item->setText(stepRowText(step, static_cast<int>(*index) + 1));
        }

        const auto current = selectedIndex();
        if (!current || *current != *index)
        {
            // 选中项已经换走：编辑框该回到新选中项的说明上，不能留着上一行的文字
            loadSelectedDescription();
            return;
        }
        if (m_description->text() != step.description)
        {
            m_description->setText(step.description);
        }
    }

    void StepReviewWindow::deleteSelected()
    {
        const auto index = selectedIndex();
        if (!index)
        {
            return;
        }
        if (!m_session.remove(*index))
        {
            return;
        }
        refresh();
        setStatus(Base::uiText("已删除一步"), false);
    }

    void StepReviewWindow::moveSelected(const int offset)
    {
        const auto index = selectedIndex();
        if (!index)
        {
            return;
        }

        const bool moved = offset < 0 ? m_session.moveUp(*index) : m_session.moveDown(*index);
        if (!moved)
        {
            return;
        }

        const int targetRow = static_cast<int>(*index) + offset;
        refresh();
        m_list->setCurrentRow(targetRow);
    }

    void StepReviewWindow::addManualStep()
    {
        if (!m_captureForManualStep)
        {
            setStatus(Base::uiText("无法补充步骤：没有可用的抓屏回调"), true);
            return;
        }

        // 抓屏时审核窗必须离开屏幕：隐藏与恢复都由这里负责，注入的抓屏回调只管抓光标所在整屏
        const bool wasVisible = isVisible();
        if (wasVisible)
        {
            hide();
            // 让平台窗口真正收走，抓屏不会拍到刚隐藏的审核窗
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        auto captured = m_captureForManualStep();

        if (wasVisible)
        {
            show();
        }

        if (!captured)
        {
            setStatus(QString::fromStdString(captured.error().message), true);
            return;
        }
        if (captured->isNull())
        {
            setStatus(Base::uiText("没有抓到画面，无法补充这一步"), true);
            return;
        }

        Core::RecordedStep step;
        step.kind        = Core::StepKind::Manual;
        step.at          = QDateTime::currentDateTime();
        step.description = Core::manualStepDescription();

        const auto appended = m_session.append(std::move(step));
        if (!appended)
        {
            setStatus(QString::fromStdString(appended.error().message), true);
            return;
        }

        const std::size_t index = m_session.count() - 1;
        if (const auto attached = m_session.attachImage(index, *captured); !attached)
        {
            setStatus(QString::fromStdString(attached.error().message), true);
            refresh();
            m_list->setCurrentRow(static_cast<int>(index));
            return;
        }

        refresh();
        m_list->setCurrentRow(static_cast<int>(index));
        setStatus(Base::uiText("已补充第 %1 步").arg(static_cast<int>(index) + 1), false);
    }

    void StepReviewWindow::addManualStepForTesting()
    {
        addManualStep();
    }

    Base::Expected<void> StepReviewWindow::exportHtml(const std::filesystem::path &filePath)
    {
        const auto written = Core::writeStepHtml(filePath, m_session.steps(), QDateTime::currentDateTime());
        if (!written)
        {
            setStatus(QString::fromStdString(written.error().message), true);
            return written;
        }

        setStatus(Base::uiText("已导出 HTML：%1").arg(Base::Paths::toDisplayText(filePath)), false);
        return {};
    }

    Base::Expected<void> StepReviewWindow::exportPdf(const std::filesystem::path &filePath)
    {
        const auto written = writeStepPdf(filePath, m_session.steps(), QDateTime::currentDateTime());
        if (!written)
        {
            setStatus(QString::fromStdString(written.error().message), true);
            return written;
        }

        setStatus(Base::uiText("已导出 PDF：%1").arg(Base::Paths::toDisplayText(filePath)), false);
        return {};
    }

    void StepReviewWindow::chooseAndExportHtml()
    {
        const auto suggested = Base::Paths::toDisplayText(suggestedStepExportPath(m_settings, QDateTime::currentDateTime(), QStringLiteral("html")));
        const auto chosen    = QFileDialog::getSaveFileName(this, Base::uiText("导出 HTML 教程"), suggested, Base::uiText("HTML 文件 (*.html)"));
        if (chosen.isEmpty())
        {
            return;
        }
        const auto path = withSuffix(Base::Paths::fromDisplayText(chosen), QStringLiteral(".html"));
        (void) exportHtml(path);
    }

    void StepReviewWindow::chooseAndExportPdf()
    {
        const auto suggested = Base::Paths::toDisplayText(suggestedStepExportPath(m_settings, QDateTime::currentDateTime(), QStringLiteral("pdf")));
        const auto chosen    = QFileDialog::getSaveFileName(this, Base::uiText("导出 PDF 教程"), suggested, Base::uiText("PDF 文件 (*.pdf)"));
        if (chosen.isEmpty())
        {
            return;
        }
        const auto path = withSuffix(Base::Paths::fromDisplayText(chosen), QStringLiteral(".pdf"));
        (void) exportPdf(path);
    }

    QString StepReviewWindow::statusText() const
    {
        return m_status == nullptr ? QString{} : m_status->text();
    }

    bool StepReviewWindow::confirmDiscard()
    {
        // 一步都不剩时没有可确认的内容，直接清理临时目录，也不惊动注入的确认回调
        if (m_session.count() == 0)
        {
            return true;
        }

        const int stepCount = static_cast<int>(m_session.count());
        if (m_confirmDiscard)
        {
            return m_confirmDiscard(stepCount);
        }

        const auto answer = QMessageBox::question(this, Base::uiText("放弃步骤记录"), Base::uiText("确定放弃这次记录的 %1 步吗？临时截图会被删除，操作无法撤销。").arg(stepCount),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        return answer == QMessageBox::Yes;
    }

    void StepReviewWindow::setDiscardConfirmation(std::function<bool(int)> confirmation)
    {
        m_confirmDiscard = std::move(confirmation);
    }

    void StepReviewWindow::discardNow()
    {
        m_session.cleanup();
        emit discardConfirmed();
        accept();
    }

    void StepReviewWindow::discardWithoutConfirmationForTesting()
    {
        discardNow();
    }

    void StepReviewWindow::closeEvent(QCloseEvent *event)
    {
        if (!confirmDiscard())
        {
            event->ignore();
            return;
        }

        discardNow();
        event->accept();
    }

    void StepReviewWindow::reject()
    {
        if (!confirmDiscard())
        {
            return;
        }
        discardNow();
    }

    void StepReviewWindow::setStatus(const QString &text, const bool error)
    {
        m_status->setText(text);
        m_status->setToolTip(text);
        // 出错时加粗：状态行在同一位置轮换成功与失败，字重是唯一不引入主题色的区分
        QFont statusFont = m_status->font();
        statusFont.setBold(error);
        m_status->setFont(statusFont);
    }

    std::filesystem::path suggestedStepExportPath(Core::ISettingsStore &settings, const QDateTime &moment, const QString &extension)
    {
        const auto configured = settings.stringValue(Core::kPathScreenshot, QString{});
        const auto directory  = configured.isEmpty() ? Base::Paths::defaultScreenshotDirectory() : Base::Paths::fromDisplayText(configured);
        const auto name       = Base::Paths::imageFileName(moment, extension.toStdString(), QStringLiteral("GyanisScribe_教程_%date%_%time%"));
        return Base::Paths::uniqueFilePath(directory, name);
    }

} // namespace GyanisScribe::Gui
