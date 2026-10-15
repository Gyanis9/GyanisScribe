// 一次截图的去处词表：默认两处、交付顺序只由表定、坏写法逐条拒。
// 脚本里最贵的错是「以为某一处生效了」，所以这里的每一条拒绝都要点名它收到的是什么。
#include "App/CaptureDelivery.h"

#include <QString>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using GyanisScribe::App::CaptureOutput;
using GyanisScribe::App::captureOutputsContain;
using GyanisScribe::App::CaptureOutputSet;
using GyanisScribe::App::captureOutputTokenList;
using GyanisScribe::App::defaultCaptureOutputs;
using GyanisScribe::App::parseCaptureOutputs;

namespace
{
    /// 表序就是交付顺序，用例里的期望值都按它写
    const CaptureOutputSet kFileAndClipboard{CaptureOutput::File, CaptureOutput::Clipboard};
    const CaptureOutputSet kAllThree{CaptureOutput::File, CaptureOutput::Clipboard, CaptureOutput::Pin};
    const CaptureOutputSet kFileAndPin{CaptureOutput::File, CaptureOutput::Pin};
    const CaptureOutputSet kClipboardAndPin{CaptureOutput::Clipboard, CaptureOutput::Pin};
} // namespace

/**
 * @brief 默认去处是「文件 + 剪贴板」，与命令行加这个开关之前的行为同一档
 * @details 默认值一改，旧脚本就少了一半输出，而退出码仍然是 0
 */
TEST(CaptureDelivery, DefaultOutputsAreFileAndClipboard)
{
    EXPECT_EQ(defaultCaptureOutputs(), kFileAndClipboard);
}

/**
 * @brief 输入里的先后不定顺序：交付顺序恒为文件 → 剪贴板 → 贴到屏幕上
 */
TEST(CaptureDelivery, OrderComesFromTheTableNotFromInput)
{
    const auto forward = parseCaptureOutputs(QStringLiteral("file,clipboard,pin"));
    ASSERT_TRUE(forward.has_value());
    EXPECT_EQ(*forward, kAllThree);

    const auto reversed = parseCaptureOutputs(QStringLiteral("pin,clipboard,file"));
    ASSERT_TRUE(reversed.has_value());
    EXPECT_EQ(*reversed, kAllThree);

    const auto pair = parseCaptureOutputs(QStringLiteral("pin,file"));
    ASSERT_TRUE(pair.has_value());
    EXPECT_EQ(*pair, kFileAndPin);
}

/**
 * @brief 段前后的空格与写法无关：`file, clipboard` 是脚本里的常见写法
 */
TEST(CaptureDelivery, IgnoresSpacesAroundPieces)
{
    const auto spaced = parseCaptureOutputs(QStringLiteral("  clipboard , pin  "));
    ASSERT_TRUE(spaced.has_value());
    EXPECT_EQ(*spaced, kClipboardAndPin);
}

/**
 * @brief 词表外的取值与大小写都拒绝，且点名收到的那一个
 */
TEST(CaptureDelivery, RejectsUnknownTokenAndCaseVariants)
{
    for (const auto *value: {"bogus", "File", "CLIPBOARD", "desk", "剪贴板"})
    {
        const auto parsed = parseCaptureOutputs(QString::fromUtf8(value));
        ASSERT_FALSE(parsed.has_value()) << value;
        EXPECT_EQ(parsed.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument) << value;
        EXPECT_NE(parsed.error().message.find(value), std::string::npos) << "要点名收到的那一个：" << parsed.error().message;
        EXPECT_NE(parsed.error().message.find("file"), std::string::npos) << "要列出可取值：" << parsed.error().message;
    }
}

/**
 * @brief 空清单与空段（结尾多一个逗号）都拒绝，而不是当成「只剩前面的那一处」
 * @details 把 `file,` 当成 file 是静默变形：用户以为逗号后面那一处也落了
 */
TEST(CaptureDelivery, RejectsEmptyListAndEmptyPiece)
{
    for (const auto *value: {"", " ", ",", "file,", ",clipboard"})
    {
        const auto parsed = parseCaptureOutputs(QString::fromUtf8(value));
        EXPECT_FALSE(parsed.has_value()) << value;
    }
}

/**
 * @brief 重复点名不代劳去重：手抖写两遍要说出来
 */
TEST(CaptureDelivery, RejectsDuplicatedDestination)
{
    const auto duplicated = parseCaptureOutputs(QStringLiteral("clipboard,clipboard"));
    ASSERT_FALSE(duplicated.has_value());
    EXPECT_NE(duplicated.error().message.find("clipboard"), std::string::npos) << duplicated.error().message;
    EXPECT_NE(duplicated.error().message.find("两次"), std::string::npos) << duplicated.error().message;
}

/**
 * @brief 「点没点名某一处」按集合判，与输入顺序无关
 */
TEST(CaptureDelivery, ContainsLooksUpOneDestination)
{
    EXPECT_TRUE(captureOutputsContain(kAllThree, CaptureOutput::Pin));
    EXPECT_FALSE(captureOutputsContain(kFileAndClipboard, CaptureOutput::Pin));
    EXPECT_FALSE(captureOutputsContain(CaptureOutputSet{}, CaptureOutput::File));
}

/**
 * @brief 可取值清单与词表同解：改了表漏改说明句在这里会红
 */
TEST(CaptureDelivery, TokenListCarriesEveryChoice)
{
    const auto list = captureOutputTokenList();
    for (const auto &choice: GyanisScribe::App::kCaptureOutputChoices)
    {
        EXPECT_NE(list.find(choice.token), std::string::npos) << std::string{choice.token};
    }
}
