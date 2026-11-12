// 报告窗用例：字段与开关驱动预览、检查中禁写、自动打码默认全选、
// 不可用时只存文字、复制文本与预览逐字一致。

#include "Base/Text.h"
#include "Core/Annotation/AnnotationTool.h"
#include "Core/BugReport/BugReport.h"
#include "Core/Privacy/PiiTypes.h"
#include "Gui/BugReport/BugReportDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTime>
#include <QTimeZone>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using GyanisScribe::Base::uiText;
using GyanisScribe::Core::BugReport::BugReportEnvironment;
using GyanisScribe::Core::BugReport::RedactionState;
using GyanisScribe::Core::Privacy::PiiItem;
using GyanisScribe::Core::Privacy::PiiKind;
using GyanisScribe::Gui::BugReportDialog;

namespace
{
    std::unique_ptr<QApplication> application;

    class BugReportDialogFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestGuiBugReport";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        static QImage makeImage()
        {
            QImage image{QSize{200, 100}, QImage::Format_ARGB32_Premultiplied};
            for (int y = 0; y < image.height(); ++y)
            {
                auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x)
                {
                    line[x] = qRgba((x * 9 + y * 5) % 256, (x * 3) % 256, (y * 7) % 256, 255);
                }
            }
            return image;
        }

        static BugReportEnvironment makeEnvironment()
        {
            BugReportEnvironment environment;
            environment.appName          = QStringLiteral("智录");
            environment.appVersion       = QStringLiteral("0.0.1");
            environment.osName           = QStringLiteral("Windows 11 Version 23H2");
            environment.osVersion        = QStringLiteral("10.0.26100");
            environment.architecture     = QStringLiteral("x86_64");
            environment.qtVersion        = QStringLiteral("6.12.0");
            environment.screenName       = QStringLiteral("DISPLAY2");
            environment.logicalSize      = QSize{200, 100};
            environment.physicalSize     = QSize{200, 100};
            environment.devicePixelRatio = 1.0;
            environment.capturedAt       = QDateTime{QDate{2026, 11, 12}, QTime{15, 30, 12}, QTimeZone::fromSecondsAheadOfUtc(8 * 3600)};
            environment.windowTitle      = QStringLiteral("机密窗口");
            environment.processName      = QStringLiteral("notepad.exe");
            return environment;
        }

        static std::unique_ptr<BugReportDialog> makeDialog()
        {
            return std::make_unique<BugReportDialog>(makeImage(), makeEnvironment(), std::filesystem::path{"C:/reports/report.md"});
        }

        static PiiItem makePhoneItem()
        {
            return PiiItem{PiiKind::Phone, QRect{40, 30, 80, 20}, QStringLiteral("138****8000")};
        }

        static void setText(BugReportDialog &dialog, const QString &objectName, const QString &text)
        {
            auto *edit = dialog.findChild<QLineEdit *>(objectName);
            ASSERT_NE(edit, nullptr) << objectName.toStdString();
            edit->setText(text);
        }

        static void setMultiline(BugReportDialog &dialog, const QString &objectName, const QString &text)
        {
            auto *edit = dialog.findChild<QTextEdit *>(objectName);
            ASSERT_NE(edit, nullptr) << objectName.toStdString();
            edit->setPlainText(text);
        }
    };
} // namespace

TEST_F(BugReportDialogFixture, StartsPendingAndBlocksEveryWriteAction)
{
    auto  dialog = makeDialog();
    auto *copy   = dialog->findChild<QPushButton *>(QStringLiteral("bugReportCopy"));
    auto *save   = dialog->findChild<QPushButton *>(QStringLiteral("bugReportSave"));
    auto *adjust = dialog->findChild<QPushButton *>(QStringLiteral("bugReportAdjust"));
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(save, nullptr);
    ASSERT_NE(adjust, nullptr);
    EXPECT_FALSE(copy->isEnabled());
    EXPECT_FALSE(save->isEnabled());
    EXPECT_FALSE(adjust->isEnabled());

    QGuiApplication::clipboard()->setText(QStringLiteral("保持不变"));
    const auto copied = dialog->copyReport();
    EXPECT_FALSE(copied.has_value());
    EXPECT_TRUE(copied.error().message.find("正在检查") != std::string::npos);
    EXPECT_EQ(QGuiApplication::clipboard()->text(), QStringLiteral("保持不变"));
    EXPECT_FALSE(dialog->saveReport(std::filesystem::path{"C:/reports/report.md"}).has_value());
}

TEST_F(BugReportDialogFixture, FieldsAndTogglesDriveThePreviewText)
{
    auto dialog = makeDialog();
    setText(*dialog, QStringLiteral("bugReportTitle"), QStringLiteral("保存失败"));
    setMultiline(*dialog, QStringLiteral("bugReportSteps"), QStringLiteral("1. 打开记事本\n2. 按 Ctrl+S"));
    setMultiline(*dialog, QStringLiteral("bugReportExpected"), QStringLiteral("文件保存成功"));
    setMultiline(*dialog, QStringLiteral("bugReportActual"), QStringLiteral("弹窗提示路径无效"));
    setMultiline(*dialog, QStringLiteral("bugReportNotes"), QStringLiteral("复现三次"));

    auto *preview = dialog->findChild<QTextEdit *>(QStringLiteral("bugReportPreview"));
    ASSERT_NE(preview, nullptr);
    EXPECT_EQ(preview->toPlainText(), dialog->reportText());
    EXPECT_TRUE(dialog->reportText().startsWith(QStringLiteral("# 保存失败\n")));
    EXPECT_TRUE(dialog->reportText().contains(QStringLiteral("2. 按 Ctrl+S")));

    auto *systemToggle = dialog->findChild<QCheckBox *>(QStringLiteral("bugReportSystemToggle"));
    auto *windowToggle = dialog->findChild<QCheckBox *>(QStringLiteral("bugReportWindowToggle"));
    ASSERT_NE(systemToggle, nullptr);
    ASSERT_NE(windowToggle, nullptr);
    EXPECT_TRUE(systemToggle->isChecked());

    windowToggle->setChecked(false);
    EXPECT_FALSE(dialog->reportText().contains(QStringLiteral("机密窗口")));
    EXPECT_FALSE(dialog->reportText().contains(QStringLiteral("notepad.exe")));
    EXPECT_FALSE(dialog->reportText().contains(QStringLiteral("## 窗口")));

    systemToggle->setChecked(false);
    EXPECT_FALSE(dialog->reportText().contains(QStringLiteral("## 环境")));
    EXPECT_EQ(preview->toPlainText(), dialog->reportText());
}

TEST_F(BugReportDialogFixture, WindowTitleNeverBecomesTheDefaultTitle)
{
    auto dialog = makeDialog();
    EXPECT_TRUE(dialog->reportText().startsWith(QStringLiteral("# 截图问题反馈\n")));
    EXPECT_TRUE(dialog->reportText().contains(QStringLiteral("机密窗口"))) << "环境段仍应带窗口标题";
}

TEST_F(BugReportDialogFixture, ScanResultMasksAllHitsAndDoesNotLeakSamples)
{
    auto       dialog = makeDialog();
    const auto before = dialog->reportImageForTesting().copy();
    dialog->setScanResult({makePhoneItem()});

    EXPECT_EQ(dialog->redactionForTesting().state, RedactionState::Masked);
    EXPECT_EQ(dialog->redactionForTesting().maskedCount, 1);
    EXPECT_EQ(dialog->redactionForTesting().toolLabel, QStringLiteral("马赛克"));
    EXPECT_TRUE(dialog->reportText().contains(uiText("已对 1 处敏感信息打码（马赛克）")));
    EXPECT_FALSE(dialog->reportText().contains(QStringLiteral("138****8000")));
    EXPECT_FALSE(dialog->reportText().contains(QStringLiteral("手机号")));

    int changed = 0;
    for (int y = 28; y < 52; ++y)
    {
        for (int x = 38; x < 122; ++x)
        {
            if (before.pixel(x, y) != dialog->reportImageForTesting().pixel(x, y))
            {
                ++changed;
            }
        }
    }
    EXPECT_GT(changed, 0) << "自动打码没有改到命中框";
    EXPECT_EQ(before.pixel(5, 5), dialog->reportImageForTesting().pixel(5, 5)) << "框外被改了";

    auto *copy   = dialog->findChild<QPushButton *>(QStringLiteral("bugReportCopy"));
    auto *save   = dialog->findChild<QPushButton *>(QStringLiteral("bugReportSave"));
    auto *adjust = dialog->findChild<QPushButton *>(QStringLiteral("bugReportAdjust"));
    ASSERT_NE(copy, nullptr);
    ASSERT_NE(save, nullptr);
    ASSERT_NE(adjust, nullptr);
    EXPECT_TRUE(copy->isEnabled());
    EXPECT_TRUE(save->isEnabled());
    EXPECT_TRUE(adjust->isEnabled());
}

TEST_F(BugReportDialogFixture, EmptyScanResultMeansNoHitsAndSavesBothFiles)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto dialog = makeDialog();
    dialog->setScanResult({});
    EXPECT_EQ(dialog->redactionForTesting().state, RedactionState::NoHits);
    EXPECT_TRUE(dialog->reportText().contains(uiText("未检出可打码的敏感信息")));

    const auto chosen = std::filesystem::path{directory.path().toStdWString()} / "my report";
    ASSERT_TRUE(dialog->saveReport(chosen).has_value());
    EXPECT_TRUE(std::filesystem::exists(chosen.string() + ".md"));
    EXPECT_TRUE(std::filesystem::exists(chosen.string() + ".png"));

    const auto markdownPath = std::filesystem::path{chosen.string() + ".md"};
    QFile      markdown{QString::fromStdWString(markdownPath.wstring())};
    ASSERT_TRUE(markdown.open(QIODevice::ReadOnly));
    const auto text = QString::fromUtf8(markdown.readAll());
    EXPECT_TRUE(text.contains(QStringLiteral("![截图](<my report.png>)")));
    EXPECT_TRUE(text.contains(uiText("未检出可打码的敏感信息")));
}

TEST_F(BugReportDialogFixture, UnavailableScanSavesTextOnlyAndNeverAnImage)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto dialog = makeDialog();
    dialog->setScanUnavailable(QStringLiteral("本机没有可用的文字识别后端"));

    EXPECT_EQ(dialog->redactionForTesting().state, RedactionState::Unavailable);
    EXPECT_TRUE(dialog->reportText().contains(uiText("不可用（本机没有可用的文字识别后端）")));
    EXPECT_TRUE(dialog->reportText().contains(uiText("本次不保存截图")));

    const auto chosen = std::filesystem::path{directory.path().toStdWString()} / "report.md";
    ASSERT_TRUE(dialog->saveReport(chosen).has_value());
    EXPECT_TRUE(std::filesystem::exists(chosen));
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path{directory.path().toStdWString()} / "report.png")) << "自动打码不可用时仍落了截图";

    QFile markdown{QString::fromStdWString(chosen.wstring())};
    ASSERT_TRUE(markdown.open(QIODevice::ReadOnly));
    const auto text = QString::fromUtf8(markdown.readAll());
    EXPECT_FALSE(text.contains(QStringLiteral("![截图]")));
    EXPECT_TRUE(text.contains(uiText("截图：未附带（本段只含文字）。")));
}

TEST_F(BugReportDialogFixture, CopyWritesExactlyThePreviewText)
{
    auto dialog = makeDialog();
    dialog->setScanResult({});
    auto *preview = dialog->findChild<QTextEdit *>(QStringLiteral("bugReportPreview"));
    ASSERT_NE(preview, nullptr);

    ASSERT_TRUE(dialog->copyReport().has_value());
    EXPECT_EQ(QGuiApplication::clipboard()->text(), dialog->reportText());
    EXPECT_EQ(preview->toPlainText(), dialog->reportText());
    EXPECT_TRUE(dialog->statusText().contains(QStringLiteral("已复制")));
}

TEST_F(BugReportDialogFixture, SavedPathGetsMarkdownAndPngSiblings)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    auto dialog = makeDialog();
    dialog->setScanResult({});
    const auto base = std::filesystem::path{directory.path().toStdWString()} / "report";
    ASSERT_TRUE(dialog->saveReport(base).has_value());
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path{directory.path().toStdWString()} / "report.md"));
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path{directory.path().toStdWString()} / "report.png"));
}
