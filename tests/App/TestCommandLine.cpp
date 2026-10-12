// 命令行解析的直测：每一条参数的两种写法、坏值与冲突的报错、以及用法说明与词表同解。
// 解析是纯函数（不碰 QApplication），这是它能被逐条钉住的原因——脚本里最贵的错是「以为参数生效了」。
#include "App/CommandLine.h"

#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <string>

using GyanisScribe::App::CommandLineAction;
using GyanisScribe::App::commandLineUsageText;
using GyanisScribe::App::kCommandLineCaptureChoices;
using GyanisScribe::App::parseStartupArguments;

/**
 * @brief 空参数是正常启动，不是错误
 */
TEST(CommandLineParsing, EmptyArgumentsMeanNormalStart)
{
    const auto parsed = parseStartupArguments({});
    EXPECT_EQ(parsed.request.action, CommandLineAction::None);
    EXPECT_TRUE(parsed.errorText.isEmpty());
}

/**
 * @brief 帮助与版本：两种写法都认，且出现在任何位置都直接生效
 * @details --help 给错参数时先看帮助：报错次次带着用法说明，用户不至于两头摸不着
 */
TEST(CommandLineParsing, HelpAndVersionWinOverEverything)
{
    EXPECT_EQ(parseStartupArguments({QStringLiteral("--help")}).request.action, CommandLineAction::Help);
    EXPECT_EQ(parseStartupArguments({QStringLiteral("-h")}).request.action, CommandLineAction::Help);
    EXPECT_EQ(parseStartupArguments({QStringLiteral("--version")}).request.action, CommandLineAction::Version);
    EXPECT_EQ(parseStartupArguments({QStringLiteral("-v")}).request.action, CommandLineAction::Version);

    const auto withGarbage = parseStartupArguments({QStringLiteral("--capture=bogus"), QStringLiteral("--help")});
    EXPECT_EQ(withGarbage.request.action, CommandLineAction::Help) << "帮助要压过其余参数的报错";
    EXPECT_TRUE(withGarbage.errorText.isEmpty());
}

/**
 * @brief --selftest 与 --pick 各自解析
 */
TEST(CommandLineParsing, SimpleFlagsParse)
{
    EXPECT_EQ(parseStartupArguments({QStringLiteral("--selftest")}).request.action, CommandLineAction::SelfTest);
    EXPECT_EQ(parseStartupArguments({QStringLiteral("--pick")}).request.action, CommandLineAction::ColorPick);
}

/**
 * @brief --capture 的三档与两种写法
 */
TEST(CommandLineParsing, CaptureScopesParseInBothWritingStyles)
{
    EXPECT_EQ(parseStartupArguments({QStringLiteral("--capture=full")}).request.action, CommandLineAction::CaptureFullScreen);
    EXPECT_EQ(parseStartupArguments({QStringLiteral("--capture"), QStringLiteral("region")}).request.action, CommandLineAction::CaptureLastRegion);
    EXPECT_EQ(parseStartupArguments({QStringLiteral("--capture=window")}).request.action, CommandLineAction::CaptureWindow);
    EXPECT_EQ(parseStartupArguments({QStringLiteral("--capture"), QStringLiteral("full")}).request.action, CommandLineAction::CaptureFullScreen);
}

/**
 * @brief --capture 的坏值：点名实际收到的取值与可取值，且区分大小写
 * @details 「就近取一个」是这里最贵的错法：脚本把 --capture=Full 当成 full 跑下去，
 *          用户拿到的图与他以为的那一档不同，而退出码还是 0。
 */
TEST(CommandLineParsing, CaptureRejectsUnknownScopesWithTheVocabulary)
{
    for (const auto *value: {"bogus", "Full", "FULL", "screen", ""})
    {
        const auto parsed = parseStartupArguments({QString{QLatin1String("--capture=")} + QLatin1String(value)});
        EXPECT_EQ(parsed.request.action, CommandLineAction::None) << value;
        EXPECT_FALSE(parsed.errorText.isEmpty()) << value;
        EXPECT_TRUE(parsed.errorText.contains(QStringLiteral("full")) && parsed.errorText.contains(QStringLiteral("region")) && parsed.errorText.contains(QStringLiteral("window")))
                << "报错要点出可取值：" << parsed.errorText.toStdString();
    }

    // 取值缺失（只给参数名）：两种写法各自报错
    EXPECT_FALSE(parseStartupArguments({QStringLiteral("--capture")}).errorText.isEmpty());
    EXPECT_FALSE(parseStartupArguments({QStringLiteral("--capture=")}).errorText.isEmpty());
}

/**
 * @brief --pin 要带路径，路径原样落进请求
 */
TEST(CommandLineParsing, PinCarriesTheFilePath)
{
    const auto withSpace = parseStartupArguments({QStringLiteral("--pin"), QStringLiteral("C:/我的 图/a.png")});
    EXPECT_EQ(withSpace.request.action, CommandLineAction::PinFile);
    EXPECT_EQ(withSpace.request.filePath, QStringLiteral("C:/我的 图/a.png"));

    const auto withEquals = parseStartupArguments({QStringLiteral("--pin=D:/b.png")});
    EXPECT_EQ(withEquals.request.action, CommandLineAction::PinFile);
    EXPECT_EQ(withEquals.request.filePath, QStringLiteral("D:/b.png"));

    EXPECT_FALSE(parseStartupArguments({QStringLiteral("--pin")}).errorText.isEmpty()) << "缺路径要报错，不能贴一张空图";
    EXPECT_FALSE(parseStartupArguments({QStringLiteral("--pin=")}).errorText.isEmpty());
}

/**
 * @brief 不认识的参数与「一次给两个动作」都当场拒绝
 */
TEST(CommandLineParsing, UnknownAndConflictingArgumentsAreRejected)
{
    const auto unknown = parseStartupArguments({QStringLiteral("--speed")});
    EXPECT_FALSE(unknown.errorText.isEmpty());
    EXPECT_TRUE(unknown.errorText.contains(QStringLiteral("--help"))) << "报错要指一条出路：" << unknown.errorText.toStdString();

    const auto conflict = parseStartupArguments({QStringLiteral("--selftest"), QStringLiteral("--pick")});
    EXPECT_EQ(conflict.request.action, CommandLineAction::None) << "冲突时不许替用户挑一个执行";
    EXPECT_FALSE(conflict.errorText.isEmpty());
    EXPECT_TRUE(conflict.errorText.contains(QStringLiteral("--selftest")) && conflict.errorText.contains(QStringLiteral("--pick")))
            << "两个参数都要点名：" << conflict.errorText.toStdString();
}

/**
 * @brief 用法说明与词表同解：每个 --capture 取值都要能在说明里找到
 * @details 说明与解析共用 kCommandLineCaptureChoices，改了词表漏改说明在这里会红
 */
TEST(CommandLineParsing, UsageTextMentionsEveryCaptureScopeAndFlag)
{
    const auto usage = commandLineUsageText();
    for (const auto &choice: kCommandLineCaptureChoices)
    {
        const auto token = QString::fromUtf8(choice.token.data(), static_cast<qsizetype>(choice.token.size()));
        EXPECT_TRUE(usage.contains(token)) << "用法说明里少了取值：" << std::string{choice.token};
    }
    for (const auto *flag: {"--help", "--version", "--selftest", "--capture", "--pin", "--pick"})
    {
        EXPECT_TRUE(usage.contains(QLatin1String(flag))) << "用法说明里少了参数：" << flag;
    }
}
