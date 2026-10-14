// 命令行解析的直测：每一条参数的两种写法、坏值与冲突的报错、以及用法说明与词表同解。
// 解析是纯函数（不碰 QApplication），这是它能被逐条钉住的原因——脚本里最贵的错是「以为参数生效了」。
#include "App/CommandLine.h"

#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <string>

using GyanisScribe::App::CaptureOutput;
using GyanisScribe::App::CaptureOutputSet;
using GyanisScribe::App::CommandLineAction;
using GyanisScribe::App::commandLineUsageText;
using GyanisScribe::App::kCommandLineCaptureChoices;
using GyanisScribe::App::parseStartupArguments;

namespace
{
    const CaptureOutputSet kFileAndClipboard{CaptureOutput::File, CaptureOutput::Clipboard};
    const CaptureOutputSet kAllThree{CaptureOutput::File, CaptureOutput::Clipboard, CaptureOutput::Pin};
    const CaptureOutputSet kFileAndPin{CaptureOutput::File, CaptureOutput::Pin};
} // namespace

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
    for (const auto *flag: {"--help", "--version", "--selftest", "--capture", "--to", "--exec", "--pin", "--pick"})
    {
        EXPECT_TRUE(usage.contains(QLatin1String(flag))) << "用法说明里少了参数：" << flag;
    }
    // 交付顺序与占位符是这一族参数的用法本体，说明里点不到就等于没写
    EXPECT_TRUE(usage.contains(QStringLiteral("%file%"))) << "说明里要写出 %file% 这一格怎么用";
}

/**
 * @brief --to 的两种写法与默认档：没点名时仍是「落盘 + 剪贴板」那两处
 * @details 默认值一变，旧脚本就少一半输出而退出码照旧是 0，所以这一格要钉住
 */
TEST(CommandLineParsing, OutputDestinationsParseInBothWritingStyles)
{
    const auto withEquals = parseStartupArguments({QStringLiteral("--capture=full"), QStringLiteral("--to=file,clipboard,pin")});
    ASSERT_TRUE(withEquals.errorText.isEmpty()) << withEquals.errorText.toStdString();
    EXPECT_EQ(withEquals.request.outputs, kAllThree);
    EXPECT_TRUE(withEquals.request.outputsGiven);

    const auto spaced = parseStartupArguments({QStringLiteral("--capture"), QStringLiteral("full"), QStringLiteral("--to"), QStringLiteral("pin,file")});
    ASSERT_TRUE(spaced.errorText.isEmpty()) << spaced.errorText.toStdString();
    EXPECT_EQ(spaced.request.outputs, kFileAndPin) << "输入里的先后不定交付顺序";

    const auto omitted = parseStartupArguments({QStringLiteral("--capture=full")});
    ASSERT_TRUE(omitted.errorText.isEmpty());
    EXPECT_EQ(omitted.request.outputs, kFileAndClipboard);
    EXPECT_FALSE(omitted.request.outputsGiven) << "没点名时「点没点名」这一格要留假，控制器据此区分默认档与显式写法";
}

/**
 * @brief --to 的坏写法逐条拒：不认识的值、重复、空清单都当场报错，不执行那一次截图
 */
TEST(CommandLineParsing, OutputDestinationsRejectBadWrites)
{
    for (const auto *value: {"bogus", "File", "file,,clipboard", "file,", "clipboard,clipboard", "", " "})
    {
        const auto parsed = parseStartupArguments({QStringLiteral("--capture=full"), QString{QLatin1String("--to=")} + QLatin1String(value)});
        EXPECT_EQ(parsed.request.action, CommandLineAction::None) << value;
        EXPECT_FALSE(parsed.errorText.isEmpty()) << value;
    }

    // --to 给两次：不去「取后一次」，那会把前一处目的地悄悄丢掉
    const auto twice = parseStartupArguments({QStringLiteral("--capture=full"), QStringLiteral("--to=file"), QStringLiteral("--to=clipboard")});
    EXPECT_FALSE(twice.errorText.isEmpty());
    EXPECT_TRUE(twice.errorText.contains(QStringLiteral("只给一次"))) << twice.errorText.toStdString();
}

/**
 * @brief --exec 带的是一条完整命令行：原样收进请求，引号里的内容不在此处拆
 */
TEST(CommandLineParsing, ExecCarriesTheWholeCommandLine)
{
    const auto withEquals = parseStartupArguments({QStringLiteral("--capture=region"), QStringLiteral("--exec=上传工具 --path \"%file%\"")});
    ASSERT_TRUE(withEquals.errorText.isEmpty()) << withEquals.errorText.toStdString();
    EXPECT_EQ(withEquals.request.execCommand, QStringLiteral("上传工具 --path \"%file%\""));

    const auto withSpace = parseStartupArguments({QStringLiteral("--capture=full"), QStringLiteral("--exec"), QStringLiteral(" notifier.exe  截好了")});
    ASSERT_TRUE(withSpace.errorText.isEmpty()) << withSpace.errorText.toStdString();
    EXPECT_EQ(withSpace.request.execCommand, QStringLiteral(" notifier.exe  截好了")) << "整串按一个参数交给解析器，前后空格留给拆分那一层";

    EXPECT_FALSE(parseStartupArguments({QStringLiteral("--capture=full"), QStringLiteral("--exec")}).errorText.isEmpty());
    EXPECT_FALSE(parseStartupArguments({QStringLiteral("--capture=full"), QStringLiteral("--exec=")}).errorText.isEmpty());
    const auto twice = parseStartupArguments({QStringLiteral("--capture=full"), QStringLiteral("--exec=a.exe"), QStringLiteral("--exec=b.exe")});
    EXPECT_FALSE(twice.errorText.isEmpty());
}

/**
 * @brief --to 与 --exec 只配 --capture 用：给 --pin / --pick / --selftest 时报错而不是收下不生效
 * @details 这两条描述的是「截完之后」，别的动作没有这一步；悄悄收下就是脚本里最难查的那种「以为生效了」
 */
TEST(CommandLineParsing, DeliveryFlagsNeedACaptureAction)
{
    for (const auto *action: {"--pin=a.png", "--pick", "--selftest"})
    {
        const auto withTo = parseStartupArguments({QString::fromLatin1(action), QStringLiteral("--to=file")});
        EXPECT_FALSE(withTo.errorText.isEmpty()) << action;
        EXPECT_TRUE(withTo.errorText.contains(QStringLiteral("--capture"))) << "报错要点出该配哪一个：" << withTo.errorText.toStdString();

        const auto withExec = parseStartupArguments({QString::fromLatin1(action), QStringLiteral("--exec=a.exe")});
        EXPECT_FALSE(withExec.errorText.isEmpty()) << action;
    }

    // 单独给 --to（一个动作都没给）也算错：那条请求既不是截图也没有去处可落
    EXPECT_FALSE(parseStartupArguments({QStringLiteral("--to=file")}).errorText.isEmpty());
}
