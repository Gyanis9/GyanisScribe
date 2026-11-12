#include "Core/BugReport/BugReport.h"

#include "Base/Text.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSize>
#include <QTemporaryDir>
#include <QTime>
#include <QTimeZone>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

using GyanisScribe::Base::uiText;
using GyanisScribe::Core::BugReport::bugReportDefaultPath;
using GyanisScribe::Core::BugReport::BugReportEnvironment;
using GyanisScribe::Core::BugReport::BugReportFields;
using GyanisScribe::Core::BugReport::BugReportOptions;
using GyanisScribe::Core::BugReport::bugReportPathsForDialog;
using GyanisScribe::Core::BugReport::BugReportRedaction;
using GyanisScribe::Core::BugReport::buildBugReportMarkdown;
using GyanisScribe::Core::BugReport::formatBugReportTime;
using GyanisScribe::Core::BugReport::RedactionState;

namespace
{
    std::unique_ptr<QCoreApplication> application;

    class BugReportFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestCoreBugReport";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QCoreApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }
    };

    QDateTime fixedTime()
    {
        return QDateTime{QDate{2026, 11, 12}, QTime{15, 30, 12}, QTimeZone::fromSecondsAheadOfUtc(8 * 3600)};
    }

    BugReportEnvironment fullEnvironment()
    {
        BugReportEnvironment environment;
        environment.appName          = QStringLiteral("智录");
        environment.appVersion       = QStringLiteral("0.0.1");
        environment.osName           = QStringLiteral("Windows 11 Version 23H2");
        environment.osVersion        = QStringLiteral("10.0.26100");
        environment.architecture     = QStringLiteral("x86_64");
        environment.qtVersion        = QStringLiteral("6.12.0");
        environment.screenName       = QStringLiteral("DISPLAY2");
        environment.logicalSize      = QSize{800, 600};
        environment.physicalSize     = QSize{1600, 1200};
        environment.devicePixelRatio = 2.0;
        environment.capturedAt       = fixedTime();
        environment.windowTitle      = QStringLiteral("记事本");
        environment.processName      = QStringLiteral("notepad.exe");
        return environment;
    }

    BugReportFields fullFields()
    {
        BugReportFields fields;
        fields.title    = QStringLiteral("保存失败");
        fields.steps    = QStringLiteral("1. 打开记事本\n2. 按 Ctrl+S");
        fields.expected = QStringLiteral("文件保存成功");
        fields.actual   = QStringLiteral("弹窗提示路径无效");
        fields.notes    = QStringLiteral("复现三次");
        return fields;
    }

    BugReportRedaction maskedRedaction()
    {
        return BugReportRedaction{RedactionState::Masked, 3, QStringLiteral("马赛克"), {}};
    }
} // namespace

TEST_F(BugReportFixture, BuildsTheWholeMarkdownDocument)
{
    const auto report = buildBugReportMarkdown(fullEnvironment(), fullFields(), maskedRedaction(), BugReportOptions{}, QStringLiteral("bug-report-20261112-153012.png"));

    const auto expected = QStringLiteral("# 保存失败\n"
                                         "\n"
                                         "## 环境\n"
                                         "- 应用：智录 0.0.1\n"
                                         "- 系统：Windows 11 Version 23H2（内核 10.0.26100，x86_64）\n"
                                         "- Qt：6.12.0\n"
                                         "- 屏幕：DISPLAY2\n"
                                         "- 截图尺寸：800 × 600 逻辑像素（1600 × 1200 像素，缩放 2.00）\n"
                                         "- 截图时间：2026-11-12 15:30:12 UTC+08:00\n"
                                         "\n"
                                         "## 窗口\n"
                                         "- 前台窗口（截图时）：记事本\n"
                                         "- 进程：notepad.exe\n"
                                         "\n"
                                         "## 复现步骤\n"
                                         "1. 打开记事本\n"
                                         "2. 按 Ctrl+S\n"
                                         "\n"
                                         "## 期望结果\n"
                                         "文件保存成功\n"
                                         "\n"
                                         "## 实际结果\n"
                                         "弹窗提示路径无效\n"
                                         "\n"
                                         "## 补充说明\n"
                                         "复现三次\n"
                                         "\n"
                                         "## 截图\n"
                                         "![截图](<bug-report-20261112-153012.png>)\n"
                                         "\n"
                                         "自动打码：已对 3 处敏感信息打码（马赛克）。请自行检查后再分享。\n");
    EXPECT_EQ(report, expected);
}

TEST_F(BugReportFixture, EmptyFieldsUsePlaceholdersAndEmptyNotesDisappear)
{
    BugReportFields fields;
    const auto      report = buildBugReportMarkdown(BugReportEnvironment{}, fields, BugReportRedaction{}, BugReportOptions{});

    EXPECT_TRUE(report.startsWith(QStringLiteral("# 截图问题反馈\n")));
    EXPECT_EQ(report.count(uiText("（未填写）")), 3);
    EXPECT_FALSE(report.contains(uiText("## 补充说明")));
    EXPECT_TRUE(report.contains(uiText("正在检查。")));
    EXPECT_TRUE(report.contains(uiText("截图：未附带（本段只含文字）。")));
    EXPECT_FALSE(report.contains(QStringLiteral("## 环境")));
    EXPECT_FALSE(report.contains(QStringLiteral("## 窗口")));
}

TEST_F(BugReportFixture, TogglesRemoveWholeSections)
{
    auto options          = BugReportOptions{};
    options.includeSystem = false;
    auto report           = buildBugReportMarkdown(fullEnvironment(), fullFields(), maskedRedaction(), options);
    EXPECT_FALSE(report.contains(QStringLiteral("## 环境")));
    EXPECT_TRUE(report.contains(QStringLiteral("## 窗口")));
    EXPECT_TRUE(report.contains(QStringLiteral("记事本")));

    options.includeSystem = true;
    options.includeWindow = false;
    report                = buildBugReportMarkdown(fullEnvironment(), fullFields(), maskedRedaction(), options);
    EXPECT_TRUE(report.contains(QStringLiteral("## 环境")));
    EXPECT_FALSE(report.contains(QStringLiteral("## 窗口")));
    // 「记事本」也出现在复现步骤里，这里只断言窗口段的那一行，避免把用户文字当成窗口标题
    EXPECT_FALSE(report.contains(QStringLiteral("- 前台窗口（截图时）：记事本")));
    EXPECT_FALSE(report.contains(QStringLiteral("notepad.exe")));

    options.includeSystem = false;
    report                = buildBugReportMarkdown(fullEnvironment(), fullFields(), maskedRedaction(), options);
    EXPECT_FALSE(report.contains(QStringLiteral("## 环境")));
    EXPECT_FALSE(report.contains(QStringLiteral("## 窗口")));
}

TEST_F(BugReportFixture, MissingEnvironmentPartsAreOmittedNotFaked)
{
    auto environment             = fullEnvironment();
    environment.osName           = {};
    environment.screenName       = {};
    environment.logicalSize      = {};
    environment.physicalSize     = {};
    environment.devicePixelRatio = 0.0;
    environment.capturedAt       = {};

    const auto report = buildBugReportMarkdown(environment, fullFields(), maskedRedaction(), BugReportOptions{});
    EXPECT_TRUE(report.contains(QStringLiteral("- 系统：内核 10.0.26100，x86_64")));
    EXPECT_FALSE(report.contains(QStringLiteral("屏幕：")));
    EXPECT_FALSE(report.contains(QStringLiteral("截图尺寸：")));
    EXPECT_FALSE(report.contains(QStringLiteral("截图时间：")));
    EXPECT_TRUE(report.contains(QStringLiteral("应用：智录 0.0.1")));
}

TEST_F(BugReportFixture, WindowSectionIsOmittedWhenBothWindowFieldsAreEmpty)
{
    auto environment        = fullEnvironment();
    environment.windowTitle = {};
    environment.processName = {};
    const auto report       = buildBugReportMarkdown(environment, fullFields(), maskedRedaction(), BugReportOptions{});
    EXPECT_FALSE(report.contains(QStringLiteral("## 窗口")));
}

TEST_F(BugReportFixture, RedactionStatesUseTheirOwnCopy)
{
    BugReportEnvironment environment;
    BugReportFields      fields;

    auto redaction = BugReportRedaction{RedactionState::NoHits, 0, {}, {}};
    auto report    = buildBugReportMarkdown(environment, fields, redaction, BugReportOptions{});
    EXPECT_TRUE(report.contains(uiText("未检出可打码的敏感信息。请自行检查后再分享。")));

    redaction = BugReportRedaction{RedactionState::Unavailable, 0, {}, QStringLiteral("没有后端")};
    report    = buildBugReportMarkdown(environment, fields, redaction, BugReportOptions{});
    EXPECT_TRUE(report.contains(uiText("不可用（没有后端）。本次不保存截图，请自行检查文字内容后再分享。")));

    redaction = BugReportRedaction{RedactionState::Masked, 2, QStringLiteral("高斯模糊"), {}};
    report    = buildBugReportMarkdown(environment, fields, redaction, BugReportOptions{});
    EXPECT_TRUE(report.contains(uiText("已对 2 处敏感信息打码（高斯模糊）。请自行检查后再分享。")));
    EXPECT_FALSE(report.contains(QStringLiteral("138")));
    EXPECT_FALSE(report.contains(QStringLiteral("手机号")));
}

TEST_F(BugReportFixture, SavedReportLinksTheScreenshotAndCopiedTextDoesNot)
{
    const auto saved = buildBugReportMarkdown(fullEnvironment(), fullFields(), maskedRedaction(), BugReportOptions{}, QStringLiteral("有 空格.png"));
    EXPECT_TRUE(saved.contains(QStringLiteral("![截图](<有 空格.png>)")));
    EXPECT_FALSE(saved.contains(uiText("截图：未附带（本段只含文字）。")));

    const auto copied = buildBugReportMarkdown(fullEnvironment(), fullFields(), maskedRedaction(), BugReportOptions{});
    EXPECT_FALSE(copied.contains(QStringLiteral("![截图]")));
    EXPECT_TRUE(copied.contains(uiText("截图：未附带（本段只含文字）。")));
}

TEST_F(BugReportFixture, FormatsLocalTimeWithUtcOffset)
{
    EXPECT_EQ(formatBugReportTime(fixedTime()), QStringLiteral("2026-11-12 15:30:12 UTC+08:00"));
    const QDateTime utc{QDate{2026, 11, 12}, QTime{7, 30, 12}, QTimeZone::UTC};
    EXPECT_EQ(formatBugReportTime(utc), QStringLiteral("2026-11-12 07:30:12 UTC+00:00"));
    EXPECT_TRUE(formatBugReportTime(QDateTime{}).isEmpty());
}

TEST_F(BugReportFixture, DefaultPathAvoidsExistingMarkdownAndPng)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto root = std::filesystem::path{directory.path().toStdWString()};
    const auto time = QDateTime{QDate{2026, 11, 12}, QTime{15, 30, 12}};

    const auto first = bugReportDefaultPath(root, time);
    EXPECT_EQ(QString::fromStdWString(first.filename().wstring()), QStringLiteral("bug-report-20261112-153012.md"));
    QFile md{QString::fromStdWString(first.wstring())};
    ASSERT_TRUE(md.open(QIODevice::WriteOnly));
    md.close();

    const auto second = bugReportDefaultPath(root, time);
    EXPECT_EQ(QString::fromStdWString(second.filename().wstring()), QStringLiteral("bug-report-20261112-153012-2.md"));

    QTemporaryDir pngOnlyDirectory;
    ASSERT_TRUE(pngOnlyDirectory.isValid());
    const auto pngRoot = std::filesystem::path{pngOnlyDirectory.path().toStdWString()};
    QFile      png{QString::fromStdWString((pngRoot / "bug-report-20261112-153012.png").wstring())};
    ASSERT_TRUE(png.open(QIODevice::WriteOnly));
    png.close();
    EXPECT_EQ(QString::fromStdWString(bugReportDefaultPath(pngRoot, time).filename().wstring()), QStringLiteral("bug-report-20261112-153012-2.md"));
}

TEST_F(BugReportFixture, NormalizesSavePathToMarkdownAndPngSiblings)
{
    const auto noSuffix = bugReportPathsForDialog(std::filesystem::path{"C:/reports/my report"});
    EXPECT_EQ(QString::fromStdWString(noSuffix.markdown.wstring()), QStringLiteral("C:/reports/my report.md"));
    EXPECT_EQ(QString::fromStdWString(noSuffix.screenshot.wstring()), QStringLiteral("C:/reports/my report.png"));

    const auto textSuffix = bugReportPathsForDialog(std::filesystem::path{"C:/reports/my report.txt"});
    EXPECT_EQ(QString::fromStdWString(textSuffix.markdown.wstring()), QStringLiteral("C:/reports/my report.md"));
    EXPECT_EQ(QString::fromStdWString(textSuffix.screenshot.wstring()), QStringLiteral("C:/reports/my report.png"));

    const auto markdownSuffix = bugReportPathsForDialog(std::filesystem::path{"C:/reports/my report.MD"});
    EXPECT_EQ(QString::fromStdWString(markdownSuffix.markdown.wstring()), QStringLiteral("C:/reports/my report.MD"));
    EXPECT_EQ(QString::fromStdWString(markdownSuffix.screenshot.wstring()), QStringLiteral("C:/reports/my report.png"));
}
