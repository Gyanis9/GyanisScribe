// 审核窗的几条主路径：改写说明与调序、补一步、导出、放弃与关闭确认；按钮可用状态跟着步骤数走。
// 用例绕开文件对话框、抓屏与原生确认框，只走公开接口，原生弹窗留给按钮槽与默认回调。

#include "Gui/Steps/StepReviewWindow.h"

#include "Base/Paths.h"
#include "Base/Types.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"
#include "Core/Steps/StepSession.h"
#include "Core/Steps/StepTypes.h"

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
using GyanisScribe::Gui::suggestedStepExportPath;

namespace
{
    /// 让窗口可见但不真的落到桌面上；关闭与可见性用例需要 show 之后的状态
    void showWithoutTouchingDesktop(QWidget *window)
    {
        window->setAttribute(Qt::WA_DontShowOnScreen);
        window->show();
    }

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

        void appendStep(const QString &description, const StepKind kind = StepKind::Manual, const QColor &color = QColor{190, 40, 40})
        {
            RecordedStep step;
            step.kind           = kind;
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

/// 列表行带序号、类型与说明，窗标题按设计稿写「步骤记录」
TEST_F(StepReviewWindow, RowsCarryNumberKindAndDescription)
{
    appendStep(QStringLiteral("打开设置"), StepKind::Click);
    appendStep(QString{}, StepKind::WindowChange);
    appendStep(QStringLiteral("手动改过"), StepKind::Manual);
    auto window = makeWindow();

    EXPECT_EQ(window->windowTitle(), QStringLiteral("步骤记录"));

    auto *list = window->findChild<QListWidget *>(QStringLiteral("stepList"));
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 3);
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("1. 点击 · 打开设置"));
    EXPECT_EQ(list->item(1)->text(), QStringLiteral("2. 切换窗口")) << "说明与类型标签相同时不该写两遍";
    EXPECT_EQ(list->item(2)->text(), QStringLiteral("3. 手动补充 · 手动改过"));
}

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
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("1. 手动补充 · 第一步"));
    EXPECT_EQ(list->item(1)->text(), QStringLiteral("2. 手动补充 · 第二步"));
    EXPECT_FALSE(list->item(0)->icon().isNull()) << "列表项没有缩略图";
    EXPECT_EQ(list->currentRow(), 0);

    description->setText(QStringLiteral("改过的说明"));
    description->editingFinished();
    ASSERT_EQ(m_session->steps()[0].description, QStringLiteral("改过的说明"));
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("1. 手动补充 · 改过的说明"));

    remove->click();
    ASSERT_EQ(m_session->count(), 1u);
    ASSERT_EQ(list->count(), 1);
    EXPECT_EQ(m_session->steps()[0].description, QStringLiteral("第二步"));
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("1. 手动补充 · 第二步"));
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
    EXPECT_EQ(list->item(1)->text(), QStringLiteral("2. 手动补充 · 第二步"));
    EXPECT_EQ(list->currentRow(), 0) << "上移后选中项没有跟着这一步";

    moveDown->click();
    ASSERT_EQ(m_session->count(), 2u);
    EXPECT_EQ(m_session->steps()[0].description, QStringLiteral("第二步"));
    EXPECT_EQ(m_session->steps()[1].description, QStringLiteral("手动补充"));
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("1. 手动补充 · 第二步"));
    EXPECT_EQ(list->item(1)->text(), QStringLiteral("2. 手动补充"));
    EXPECT_EQ(list->currentRow(), 1) << "下移后选中项没有跟着这一步";
}

/// 编辑途中选中项换了，文字仍写回开始编辑的那一行，编辑框随后切到新行
TEST_F(StepReviewWindow, DescriptionEditFollowsTheRowBeingEdited)
{
    appendStep(QStringLiteral("第一步"));
    appendStep(QStringLiteral("第二步"));
    auto window = makeWindow();

    auto *list        = window->findChild<QListWidget *>(QStringLiteral("stepList"));
    auto *description = window->findChild<QLineEdit *>(QStringLiteral("stepDescription"));
    ASSERT_NE(list, nullptr);
    ASSERT_NE(description, nullptr);
    ASSERT_EQ(list->currentRow(), 0);

    description->setCursorPosition(description->text().size());
    QTest::keyClicks(description, QStringLiteral("X"));
    ASSERT_EQ(description->text(), QStringLiteral("第一步X")) << "前提没造出来：键盘输入没有进编辑框";

    // 模拟选中项在 editingFinished 之前换到第二行：这段文字不该改投第二行
    list->setCurrentRow(1);

    EXPECT_EQ(m_session->steps()[0].description, QStringLiteral("第一步X")) << "编辑期间换行把第一行的修改丢了";
    EXPECT_EQ(m_session->steps()[1].description, QStringLiteral("第二步"));
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("1. 手动补充 · 第一步X"));
    EXPECT_EQ(description->text(), QStringLiteral("第二步")) << "编辑框没有切到新选中项的说明";
}

/// 删除末行后选中相邻项；删到空列表时编辑与导出都收起来
TEST_F(StepReviewWindow, DeletingRowsReselectsNeighbourAndHandlesEmptyList)
{
    appendStep(QStringLiteral("第一步"));
    appendStep(QStringLiteral("第二步"));
    auto window = makeWindow();

    auto *list        = window->findChild<QListWidget *>(QStringLiteral("stepList"));
    auto *description = window->findChild<QLineEdit *>(QStringLiteral("stepDescription"));
    auto *remove      = window->findChild<QPushButton *>(QStringLiteral("stepDelete"));
    auto *exportPdf   = window->findChild<QPushButton *>(QStringLiteral("stepExportPdf"));
    ASSERT_NE(list, nullptr);
    ASSERT_NE(description, nullptr);
    ASSERT_NE(remove, nullptr);
    ASSERT_NE(exportPdf, nullptr);

    list->setCurrentRow(1);
    remove->click();
    ASSERT_EQ(m_session->count(), 1u);
    EXPECT_EQ(list->currentRow(), 0) << "删掉最后一行后应选中相邻的上一行";
    EXPECT_EQ(description->text(), QStringLiteral("第一步"));

    remove->click();
    EXPECT_EQ(m_session->count(), 0u);
    EXPECT_EQ(list->count(), 0);
    EXPECT_TRUE(description->text().isEmpty());
    EXPECT_FALSE(description->isEnabled());
    EXPECT_FALSE(remove->isEnabled());
    EXPECT_FALSE(exportPdf->isEnabled());
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

/// 默认导出路径落在设置里的截图目录，文件名带固定的日期与时刻
TEST_F(StepReviewWindow, SuggestsExportPathFromSettings)
{
    const auto outputDirectory = std::filesystem::path{m_root->path().toStdWString()} / L"exports";
    const auto stored          = m_settings->setValue(GyanisScribe::Core::kPathScreenshot, GyanisScribe::Base::Paths::toDisplayText(outputDirectory));
    ASSERT_TRUE(stored.has_value()) << stored.error().message;

    const QDateTime moment{QDate{2026, 11, 11}, QTime{12, 34, 56}};
    const auto      pdfPath = suggestedStepExportPath(*m_settings, moment, QStringLiteral("pdf"));
    EXPECT_EQ(pdfPath.parent_path(), outputDirectory);
    EXPECT_EQ(pdfPath.filename().wstring(), L"GyanisScribe_教程_20261111_123456.pdf");

    const auto htmlPath = suggestedStepExportPath(*m_settings, moment, QStringLiteral("html"));
    EXPECT_EQ(htmlPath.parent_path(), outputDirectory);
    EXPECT_EQ(htmlPath.extension().wstring(), L".html");
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

/// 关闭确认选「否」时窗口留着、步骤与临时图都不动
TEST_F(StepReviewWindow, CloseDeclinedKeepsWindowAndTemp)
{
    appendStep(QStringLiteral("第一步"));
    appendStep(QStringLiteral("第二步"));
    auto window = makeWindow();
    showWithoutTouchingDesktop(window.get());

    int askedSteps = 0;
    window->setDiscardConfirmation(
            [&askedSteps](const int stepCount)
            {
                askedSteps = stepCount;
                return false;
            });
    QSignalSpy confirmed{window.get(), &GuiStepReviewWindow::discardConfirmed};
    ASSERT_TRUE(confirmed.isValid());
    const auto temp = m_session->tempDirectory();

    EXPECT_FALSE(window->close());
    EXPECT_EQ(askedSteps, 2);
    EXPECT_TRUE(window->isVisible()) << "拒绝放弃后窗口不该关";
    EXPECT_EQ(m_session->count(), 2u);
    EXPECT_TRUE(std::filesystem::exists(temp));
    EXPECT_EQ(confirmed.count(), 0);
}

/// 关闭确认选「是」时清理会话、发信号并以 Accepted 收场
TEST_F(StepReviewWindow, CloseAcceptedCleansAndConfirms)
{
    appendStep(QStringLiteral("第一步"));
    auto window = makeWindow();
    showWithoutTouchingDesktop(window.get());

    int askedSteps = 0;
    window->setDiscardConfirmation(
            [&askedSteps](const int stepCount)
            {
                askedSteps = stepCount;
                return true;
            });
    QSignalSpy confirmed{window.get(), &GuiStepReviewWindow::discardConfirmed};
    ASSERT_TRUE(confirmed.isValid());
    const auto temp = m_session->tempDirectory();

    EXPECT_TRUE(window->close());
    EXPECT_EQ(askedSteps, 1);
    EXPECT_EQ(confirmed.count(), 1);
    EXPECT_EQ(window->result(), static_cast<int>(QDialog::Accepted));
    EXPECT_EQ(m_session->count(), 0u);
    EXPECT_FALSE(std::filesystem::exists(temp));
}

/// Esc 与关闭走同一条确认路径：先拒绝再接受各试一次
TEST_F(StepReviewWindow, EscapeAsksTheSameConfirmation)
{
    appendStep(QStringLiteral("第一步"));
    auto window = makeWindow();
    showWithoutTouchingDesktop(window.get());

    int  askedSteps = 0;
    bool answer     = false;
    window->setDiscardConfirmation(
            [&askedSteps, &answer](int)
            {
                ++askedSteps;
                return answer;
            });
    QSignalSpy confirmed{window.get(), &GuiStepReviewWindow::discardConfirmed};
    ASSERT_TRUE(confirmed.isValid());

    QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
    QApplication::sendEvent(window.get(), &escape);
    EXPECT_EQ(askedSteps, 1);
    EXPECT_TRUE(window->isVisible()) << "拒绝放弃后 Esc 不该关窗";
    EXPECT_EQ(confirmed.count(), 0);

    answer = true;
    QKeyEvent acceptEscape{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
    QApplication::sendEvent(window.get(), &acceptEscape);
    EXPECT_EQ(askedSteps, 2);
    EXPECT_EQ(confirmed.count(), 1);
    EXPECT_EQ(window->result(), static_cast<int>(QDialog::Accepted));
    EXPECT_EQ(m_session->count(), 0u);
}

/// 空会话没有可确认的内容：不弹确认，直接清理关闭
TEST_F(StepReviewWindow, CloseWithoutStepsSkipsConfirmation)
{
    auto window = makeWindow();
    showWithoutTouchingDesktop(window.get());

    int askedTimes = 0;
    window->setDiscardConfirmation(
            [&askedTimes](int)
            {
                ++askedTimes;
                return false;
            });
    QSignalSpy confirmed{window.get(), &GuiStepReviewWindow::discardConfirmed};
    ASSERT_TRUE(confirmed.isValid());
    const auto temp = m_session->tempDirectory();
    ASSERT_TRUE(std::filesystem::exists(temp));

    EXPECT_TRUE(window->close()) << "空会话关窗不该被确认拦住";
    EXPECT_EQ(askedTimes, 0) << "空会话不该弹确认";
    EXPECT_EQ(confirmed.count(), 1);
    EXPECT_EQ(window->result(), static_cast<int>(QDialog::Accepted));
    EXPECT_TRUE(m_session->tempDirectory().empty());
    EXPECT_FALSE(std::filesystem::exists(temp));
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

/// 补步时审核窗先离开屏幕，抓完不论成败都恢复
TEST_F(StepReviewWindow, ManualStepHidesWindowAroundCapture)
{
    appendStep(QStringLiteral("已有的一步"));
    int                  captureCalls = 0;
    GuiStepReviewWindow *windowPtr    = nullptr;
    GuiStepReviewWindow  window{*m_session, *m_settings, [&captureCalls, &windowPtr]() -> Expected<QImage>
                                {
                                   ++captureCalls;
                                   EXPECT_FALSE(windowPtr->isVisible()) << "抓屏时审核窗还在屏幕上";
                                   QImage image{200, 100, QImage::Format_ARGB32_Premultiplied};
                                   image.fill(Qt::red);
                                   return image;
                                }};
    windowPtr = &window;
    showWithoutTouchingDesktop(&window);
    ASSERT_TRUE(window.isVisible());

    window.addManualStepForTesting();
    EXPECT_EQ(captureCalls, 1);
    EXPECT_TRUE(window.isVisible()) << "补步成功后审核窗没有恢复";
    EXPECT_EQ(m_session->count(), 2u);
}

/// 抓屏回调失败时窗口照样恢复，步数不变，原因原样进状态行
TEST_F(StepReviewWindow, ManualCaptureFailureLeavesStepsAndShowsReason)
{
    appendStep(QStringLiteral("已有的一步"));
    int                  captureCalls = 0;
    GuiStepReviewWindow *windowPtr    = nullptr;
    GuiStepReviewWindow  window{*m_session, *m_settings, [&captureCalls, &windowPtr]() -> Expected<QImage>
                                {
                                   ++captureCalls;
                                   EXPECT_FALSE(windowPtr->isVisible()) << "抓屏时审核窗还在屏幕上";
                                   return std::unexpected(Error{ErrorCode::ScreenUnavailable, "抓屏失败原因"});
                                }};
    windowPtr = &window;
    showWithoutTouchingDesktop(&window);

    window.addManualStepForTesting();
    EXPECT_EQ(captureCalls, 1);
    EXPECT_TRUE(window.isVisible()) << "抓屏失败后审核窗没有恢复";
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
