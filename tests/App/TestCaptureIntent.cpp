// 「截完之后做什么」这一格的词表与枚举同解：词表是配置里的取值、设置页的标签与控制器分派三处共用的
// 唯一出处，任何一处漏一档都会变成「界面显示了却走不通」或「走通了却列不出来」。
#include "App/CaptureIntent.h"

#include <QString>
#include "Settings/SettingsKeys.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

using GyanisScribe::App::CaptureIntent;
using GyanisScribe::App::captureIntentText;
using GyanisScribe::App::defaultCaptureIntent;
using GyanisScribe::App::kCaptureIntentChoices;
using GyanisScribe::App::parseCaptureDelay;
using GyanisScribe::App::parseCaptureIntent;

namespace
{
    /// 全部枚举值，逐个点名写出来：加了新档位而忘了补词表，这条用例会因为对不上数而红
    constexpr std::array<CaptureIntent, 6> kAllIntents{
            CaptureIntent::Annotate, CaptureIntent::CopyToClipboard, CaptureIntent::SaveToFile, CaptureIntent::QuickSave, CaptureIntent::PinToDesk, CaptureIntent::Recognize,
    };
} // namespace

/**
 * @brief 钉住「每个枚举值都有 token，且 token 解析回同一个档位」
 * @details 两件事各钉一次：只测单向会把「词表里少一档」读成「这一档没被用到」，
 *          而配置里存着一个解析不出来的值时，用户每次截图都被悄悄拨回默认档。
 */
TEST(CaptureIntentVocabulary, EveryIntentRoundTripsThroughItsToken)
{
    for (const auto intent: kAllIntents)
    {
        const auto token = captureIntentText(intent);
        ASSERT_FALSE(token.empty()) << "枚举值没进词表：设置页列不出、配置里也存不了这一档";

        const auto parsed = parseCaptureIntent(QString::fromUtf8(token.data(), static_cast<qsizetype>(token.size())));
        ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
        EXPECT_EQ(*parsed, intent) << "token 解析回了另一档：界面显示与实际走的不是同一步";
    }

    // 词表的条数与枚举的值数一致：新加一档忘了补词表就落在这里
    EXPECT_EQ(kCaptureIntentChoices.size(), kAllIntents.size());

    // token 不许重复：两档用同一个 token 时，下拉框选中的那一项与配置里的值就对不上是谁
    std::vector<std::string_view> tokens;
    for (const auto &choice: kCaptureIntentChoices)
    {
        tokens.push_back(choice.token);
    }
    const std::vector<std::string_view> sortedTokens = [&tokens]
    {
        auto copy = tokens;
        std::ranges::sort(copy);
        return copy;
    }();
    EXPECT_EQ(std::ranges::adjacent_find(sortedTokens), sortedTokens.end()) << "词表里有两档共用同一个 token";
}

/**
 * @brief 拒绝面：词表外的取值要报出来并列出可取的几个，同时指清去哪一页
 * @details 悄悄回落到默认档会让「我已经选了直接存文件」的用户每按一次都多出一层窗，
 *          而报得不清楚（不点名是哪一格、不给可取值）用户就修不了配置。
 */
TEST(CaptureIntentVocabulary, RejectsUnknownTokenWithTheVocabulary)
{
    for (const auto *stored: {"annotate", "Annotations", "", "复制"})
    {
        const auto parsed = parseCaptureIntent(QString::fromUtf8(stored));
        ASSERT_FALSE(parsed.has_value()) << "取值 \"" << stored << "\" 不在词表里，却被接下了";
        EXPECT_EQ(parsed.error().code, GyanisScribe::Core::ErrorCode::InvalidArgument);
        EXPECT_NE(parsed.error().message.find("不在可选值里"), std::string::npos) << parsed.error().message;
        EXPECT_NE(parsed.error().message.find("Annotate / Copy / Save / QuickSave / Pin / Recognize"), std::string::npos) << "没列出可取值：" << parsed.error().message;
        EXPECT_NE(parsed.error().message.find("Capture/AfterAction"), std::string::npos) << "没点出键名：" << parsed.error().message;
    }
}

/**
 * @brief 钉住词表外的枚举值交出空 token，而不是硬编一个
 * @details 这一格走的就是「加了枚举值忘了补词表」那条路：交回空让调用方看见缺档，
 *          硬编一个会把缺档伪装成一档可用的取值。
 */
TEST(CaptureIntentVocabulary, UnknownEnumeratorYieldsNoToken)
{
    EXPECT_TRUE(captureIntentText(static_cast<CaptureIntent>(99)).empty()) << "词表外的枚举值不该给出一个 token";
}

/**
 * @brief 钉住回落档就是词表第一档
 * @details 设置页的初值、配置没写这一项时的行为与「读不出时按哪档显示」三处共用这一个出处；
 *          分开写就会出现「界面显示进标注窗，按下热键却直接存了文件」。
 */
TEST(CaptureIntentVocabulary, DefaultIsTheFirstChoiceInTheTable)
{
    EXPECT_EQ(defaultCaptureIntent(), kCaptureIntentChoices.front().intent);
    EXPECT_EQ(defaultCaptureIntent(), CaptureIntent::Annotate) << "默认档改了要同步改设置页的初值与本条期望值";
}

/**
 * @brief 钉住延时秒数的可用范围：0 是一档（不延时），越界要报而不是夹
 * @details 把 600 夹到 60 会让用户等到一个自己没要过的时刻弹出遮罩；报出来他才改得动那一格。
 */
TEST(CaptureIntentSettings, DelayAcceptsZeroAndRejectsOutOfRange)
{
    for (const int stored: {0, 1, 60})
    {
        const auto parsed = parseCaptureDelay(stored);
        ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
        EXPECT_EQ(*parsed, stored);
    }

    for (const int stored: {-1, 61, 600})
    {
        const auto parsed = parseCaptureDelay(stored);
        ASSERT_FALSE(parsed.has_value()) << "延时秒数 " << stored << " 被当成了可用值";
        EXPECT_EQ(parsed.error().code, GyanisScribe::Core::ErrorCode::InvalidArgument);
        EXPECT_NE(parsed.error().message.find("0 到 60"), std::string::npos) << "文案没写清可用范围：" << parsed.error().message;
        EXPECT_NE(parsed.error().message.find("Capture/DelaySeconds"), std::string::npos) << "文案没点出键名：" << parsed.error().message;
    }
}
