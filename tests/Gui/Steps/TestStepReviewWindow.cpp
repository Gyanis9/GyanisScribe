// 审核窗的四条主路径：改写说明与调序、补一步、导出、放弃；按钮可用状态跟着步骤数走。
// 用例绕开文件对话框与确认框，只走公开接口，原生弹窗留给按钮槽。

#include "Gui/Steps/StepReviewWindow.h"

#include "Base/Types.h"
#include "Core/Settings/SettingsStore.h"
#include "Core/Steps/StepSession.h"
#include "Core/Steps/StepTypes.h"

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>

using GyanisScribe::Base::Error;
using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Base::Expected;
using GyanisScribe::Core::RecordedStep;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Core::StepKind;
using GyanisScribe::Core::StepSession;
using GuiStepReviewWindow = GyanisScribe::Gui::StepReviewWindow;

namespace
{
    class StepReviewWindow : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const auto name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            m_ini           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-StepsReview-{}.ini", name);
            std::error_code ignored;
            std::filesystem::remove(m_ini, ignored);
            m_settings = std::make_unique<SettingsStore>(m_ini);
            m_session  = std::make_unique<StepSession>();
            m_root     = std::make_unique<QTemporaryDir>();
            ASSERT_TRUE(m_root->isValid());

            const auto begun = m_session->begin(std::filesystem::path{m_root->path().toStdWString()} / L"steps");
            ASSERT_TRUE(begun.has_value()) << begun.error().message;
        }

        void TearDown() override
        {
            m_session->cleanup();
            m_session.reset();
            m_settings.reset();
            m_root.reset();
            std::error_code ignored;
            std::filesystem::remove(m_ini, ignored);
        }

        void appendStep(const QString &description, const QColor &color = QColor{190, 40, 40})
        {
            RecordedStep step;
            step.kind           = StepKind::Manual;
            step.at             = QDateTime::currentDateTime();
            step.description    = description;
            const auto appended = m_session->append(step);
            ASSERT_TRUE(appended.has_value()) << appended.error().message;

            QImage image{64, 36, QImage::Format_ARGB32_Premultiplied};
            image.fill(color);
            const auto attached = m_session->attachImage(m_session->count() - 1, image);
            ASSERT_TRUE(attached.has_value()) << attached.error().message;
        }

        std::unique_ptr<GuiStepReviewWindow> makeWindow(int *captureCalls = nullptr)
        {
            return std::make_unique<GuiStepReviewWindow>(*m_session, *m_settings,
                                                         [captureCalls]
                                                         {
                                                             if (captureCalls != nullptr)
                                                             {
                                                                 ++*captureCalls;
                                                             }
                                                             QImage image{200, 100, QImage::Format_ARGB32_Premultiplied};
                                                             image.fill(Qt::red);
                                                             return Expected<QImage>{image};
                                                         });
        }

        std::filesystem::path          m_ini;
        std::unique_ptr<SettingsStore> m_settings;
        std::unique_ptr<StepSession>   m_session;
        std::unique_ptr<QTemporaryDir> m_root;
    };
} // namespace

/// 说明编辑落到会话，删除与补步重建列表，上移下移带着选中项一起走
TEST_F(StepReviewWindow, EditsDescriptionDeleteMoveAndManual)
{
    appendStep(QStringLiteral("第一步"));
    appendStep(QStringLiteral("第二步"));
    auto window = makeWindow();

    auto *list        = window->findChild<QListWidget *>(QStringLiteral("stepList"));
    auto *description = window->findChild<QLineEdit *>(QStringLiteral("stepDescription"));
    auto *addManual   = window->findChild<QPushButton *>(QStringLiteral("stepAddManual"));
    auto *remove      = window->findChild<QPushButton *>(QStringLiteral("stepDelete"));
    auto *moveUp      = window->findChild<QPushButton *>(QStringLiteral("stepMoveUp"));
    auto *moveDown    = window->findChild<QPushButton *>(QStringLiteral("stepMoveDown"));
    ASSERT_NE(list, nullptr);
    ASSERT_NE(description, nullptr);
    ASSERT_NE(addManual, nullptr);
    ASSERT_NE(remove, nullptr);
    ASSERT_NE(moveUp, nullptr);
    ASSERT_NE(moveDown, nullptr);

    ASSERT_EQ(list->count(), 2);
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("1. 第一步"));
    EXPECT_EQ(list->item(1)->text(), QStringLiteral("2. 第二步"));
    EXPECT_FALSE(list->item(0)->icon().isNull()) << "列表项没有缩略图";
    EXPECT_EQ(list->currentRow(), 0);

    description->setText(QStringLiteral("改过的说明"));
    description->editingFinished();
    ASSERT_EQ(m_session->steps()[0].description, QStringLiteral("改过的说明"));
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("1. 改过的说明"));

    remove->click();
    ASSERT_EQ(m_session->count(), 1u);
    ASSERT_EQ(list->count(), 1);
    EXPECT_EQ(m_session->steps()[0].description, QStringLiteral("第二步"));
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("1. 第二步"));
    EXPECT_FALSE(list->item(0)->icon().isNull()) << "删除前一步后剩下的缩略图没了";

    addManual->click();
    ASSERT_EQ(m_session->count(), 2u);
    ASSERT_EQ(list->count(), 2);
    EXPECT_EQ(m_session->steps()[1].description, QStringLiteral("手动补充"));
    EXPECT_EQ(list->item(1)->text(), QStringLiteral("2. 手动补充"));
    EXPECT_EQ(list->currentRow(), 1);

    moveUp->click();
    ASSERT_EQ(m_session->count(), 2u);
    EXPECT_EQ(m_session->steps()[0].description, QStringLiteral("手动补充"));
    EXPECT_EQ(m_session->steps()[1].description, QStringLiteral("第二步"));
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("1. 手动补充"));
    EXPECT_EQ(list->currentRow(), 0) << "上移后选中项没有跟着这一步";

    moveDown->click();
    ASSERT_EQ(m_session->count(), 2u);
    EXPECT_EQ(m_session->steps()[0].description, QStringLiteral("第二步"));
    EXPECT_EQ(m_session->steps()[1].description, QStringLiteral("手动补充"));
    EXPECT_EQ(list->currentRow(), 1) << "下移后选中项没有跟着这一步";
}

/// 导出两条出口都落出真实文件，状态行给出结果
TEST_F(StepReviewWindow, ExportsHtmlAndPdf)
{
    appendStep(QStringLiteral("第一步"));
    auto window = makeWindow();

    const auto htmlPath = std::filesystem::path{m_root->path().toStdWString()} / L"tutorial.html";
    const auto html     = window->exportHtml(htmlPath);
    ASSERT_TRUE(html.has_value()) << html.error().message;
    ASSERT_TRUE(std::filesystem::exists(htmlPath));
    EXPECT_GT(std::filesystem::file_size(htmlPath), 0u);
    EXPECT_TRUE(window->statusText().contains(QStringLiteral("HTML"))) << window->statusText().toStdString();

    const auto pdfPath = std::filesystem::path{m_root->path().toStdWString()} / L"tutorial.pdf";
    const auto pdf     = window->exportPdf(pdfPath);
    ASSERT_TRUE(pdf.has_value()) << pdf.error().message;
    ASSERT_TRUE(std::filesystem::exists(pdfPath));
    EXPECT_GT(std::filesystem::file_size(pdfPath), 2048u);
    EXPECT_TRUE(window->statusText().contains(QStringLiteral("PDF"))) << window->statusText().toStdString();
}

/// 放弃后临时目录与步骤都没了，并发出一次 discardConfirmed
TEST_F(StepReviewWindow, DiscardCleansTemp)
{
    appendStep(QStringLiteral("第一步"));
    auto window = makeWindow();

    QSignalSpy confirmed{window.get(), &GuiStepReviewWindow::discardConfirmed};
    ASSERT_TRUE(confirmed.isValid());
    const auto temp = m_session->tempDirectory();
    ASSERT_FALSE(temp.empty());
    ASSERT_TRUE(std::filesystem::exists(temp));

    window->discardWithoutConfirmationForTesting();
    EXPECT_TRUE(m_session->tempDirectory().empty());
    EXPECT_EQ(m_session->count(), 0u);
    EXPECT_FALSE(std::filesystem::exists(temp));
    EXPECT_EQ(confirmed.count(), 1);
    EXPECT_EQ(window->result(), static_cast<int>(QDialog::Accepted));
}

/// 空会话只能补步；一旦有步，删除、调序与两条导出跟着解禁
TEST_F(StepReviewWindow, ButtonsFollowStepCount)
{
    auto window = makeWindow();

    auto *addManual   = window->findChild<QPushButton *>(QStringLiteral("stepAddManual"));
    auto *remove      = window->findChild<QPushButton *>(QStringLiteral("stepDelete"));
    auto *moveUp      = window->findChild<QPushButton *>(QStringLiteral("stepMoveUp"));
    auto *moveDown    = window->findChild<QPushButton *>(QStringLiteral("stepMoveDown"));
    auto *exportHtml  = window->findChild<QPushButton *>(QStringLiteral("stepExportHtml"));
    auto *exportPdf   = window->findChild<QPushButton *>(QStringLiteral("stepExportPdf"));
    auto *description = window->findChild<QLineEdit *>(QStringLiteral("stepDescription"));
    auto *discard     = window->findChild<QPushButton *>(QStringLiteral("stepDiscard"));
    auto *status      = window->findChild<QLabel *>(QStringLiteral("stepStatus"));
    ASSERT_NE(addManual, nullptr);
    ASSERT_NE(remove, nullptr);
    ASSERT_NE(moveUp, nullptr);
    ASSERT_NE(moveDown, nullptr);
    ASSERT_NE(exportHtml, nullptr);
    ASSERT_NE(exportPdf, nullptr);
    ASSERT_NE(description, nullptr);
    ASSERT_NE(discard, nullptr);
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->text(), window->statusText());

    EXPECT_TRUE(addManual->isEnabled()) << "空会话也要能补一步";
    EXPECT_FALSE(remove->isEnabled());
    EXPECT_FALSE(moveUp->isEnabled());
    EXPECT_FALSE(moveDown->isEnabled());
    EXPECT_FALSE(exportHtml->isEnabled());
    EXPECT_FALSE(exportPdf->isEnabled());
    EXPECT_FALSE(description->isEnabled());

    addManual->click();
    ASSERT_EQ(m_session->count(), 1u);
    EXPECT_TRUE(remove->isEnabled());
    EXPECT_FALSE(moveUp->isEnabled()) << "只有一步时上移没有去处";
    EXPECT_FALSE(moveDown->isEnabled()) << "只有一步时下移没有去处";
    EXPECT_TRUE(exportHtml->isEnabled());
    EXPECT_TRUE(exportPdf->isEnabled());
    EXPECT_TRUE(description->isEnabled());
}

/// 抓屏回调失败时步数不变，原因原样进状态行
TEST_F(StepReviewWindow, ManualCaptureFailureLeavesStepsAndShowsReason)
{
    appendStep(QStringLiteral("已有的一步"));
    int                 captureCalls = 0;
    GuiStepReviewWindow window{*m_session, *m_settings, [&captureCalls]() -> Expected<QImage>
                               {
                                   ++captureCalls;
                                   return std::unexpected(Error{ErrorCode::ScreenUnavailable, "抓屏失败原因"});
                               }};

    window.addManualStepForTesting();
    EXPECT_EQ(captureCalls, 1);
    EXPECT_EQ(m_session->count(), 1u);
    EXPECT_TRUE(window.statusText().contains(QStringLiteral("抓屏失败原因"))) << window.statusText().toStdString();
}

/// 空会话直接导出也要给出中文原因，不能落出空文件
TEST_F(StepReviewWindow, ExportRejectsEmptySession)
{
    auto       window = makeWindow();
    const auto path   = std::filesystem::path{m_root->path().toStdWString()} / L"empty.pdf";

    const auto exported = window->exportPdf(path);
    ASSERT_FALSE(exported.has_value());
    EXPECT_FALSE(exported.error().message.empty());
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_FALSE(window->statusText().isEmpty());
}
