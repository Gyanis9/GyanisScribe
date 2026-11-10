#include "Core/Pick/RecentColors.h"
#include "Core/Settings/SettingsKeys.h"

#include <gtest/gtest.h>

#include <QColor>
#include <QStringList>

#include <format>
#include <string>
#include <vector>

using GyanisScribe::Core::kAnnotationPalette;
using GyanisScribe::Core::kPickRecentColors;
using GyanisScribe::Core::kRecentColorCapacity;
using GyanisScribe::Core::parseRecentColors;
using GyanisScribe::Core::pushRecentColor;
using GyanisScribe::Core::recentColorToken;
using GyanisScribe::Core::serializeRecentColors;

namespace
{
    std::vector<QColor> colors(std::initializer_list<QColor> list)
    {
        return {list};
    }

    /// 取色器这一路给解析器的恢复办法：提示里点名的必须是它真正读的那一格
    std::string pickRecoveryHint()
    {
        return std::format("删除配置中的 {} 可恢复默认设置", kPickRecentColors);
    }
} // namespace

/**
 * @brief 钉住「最近使用」的字面写法，而不是让 parseRecentColors 反过来定义它
 * @details 用被测函数自己的另一侧算期望值时，整条序列化格式换一个写法也照样绿；
 *          这一串手算值是配置文件对外的契约。
 */
TEST(RecentColorsToken, MatchesHandWrittenHexForm)
{
    EXPECT_EQ(recentColorToken(QColor(255, 0, 0)), QStringLiteral("#FF0000"));
    EXPECT_EQ(recentColorToken(QColor(0, 120, 255)), QStringLiteral("#0078FF"));
    EXPECT_EQ(recentColorToken(QColor(0, 0, 0)), QStringLiteral("#000000"));
    EXPECT_EQ(recentColorToken(QColor(18, 52, 86)), QStringLiteral("#123456"));
}

/**
 * @brief 钉住带 alpha 的颜色只留 RGB
 * @details QColor::name() 在这种颜色上会给 8 位，而读回那一侧只认 6 位——
 *          不显式取 RGB 就会出现「存得下、读不回」。
 */
TEST(RecentColorsToken, DropsAlphaToKeepSixDigits)
{
    const auto translucent = QColor(255, 0, 0, 128);
    EXPECT_EQ(recentColorToken(translucent), QStringLiteral("#FF0000"));

    const auto readBack = parseRecentColors({recentColorToken(translucent)}, "最近使用颜色", pickRecoveryHint());
    ASSERT_TRUE(readBack.has_value()) << readBack.error().message;
    ASSERT_EQ(readBack->size(), 1U);
    EXPECT_EQ(readBack->at(0).red(), 255);
    EXPECT_EQ(readBack->at(0).green(), 0);
    EXPECT_EQ(readBack->at(0).blue(), 0);
}

/**
 * @brief 钉住新用到的颜色排在最前、同色不占两个名额
 */
TEST(RecentColorsOrder, NewestFirstAndNoDuplicates)
{
    const auto red   = QColor(255, 0, 0);
    const auto green = QColor(0, 255, 0);
    const auto blue  = QColor(0, 0, 255);

    const auto once  = pushRecentColor(colors({green}), red);
    const auto twice = pushRecentColor(once, red);
    ASSERT_EQ(twice.size(), 2U) << "同一个颜色被记了两次，上限就名存实亡";
    EXPECT_EQ(twice[0], red);
    EXPECT_EQ(twice[1], green);

    const auto revived = pushRecentColor(twice, green);
    ASSERT_EQ(revived.size(), 2U);
    EXPECT_EQ(revived[0], green) << "又用了一次却没挪到最前，清单顺序就不再是「最近」";

    const auto three = pushRecentColor(revived, blue);
    ASSERT_EQ(three.size(), 3U);
    EXPECT_EQ(three[2], red) << "丢的不是最久没用过的那一个";
}

/**
 * @brief 钉住容量截断与「无效颜色不改清单」
 */
TEST(RecentColorsCapacity, TrimsOldestAndIgnoresInvalidPick)
{
    std::vector<QColor> current;
    for (int channel = 1; channel <= 8; ++channel)
    {
        current = pushRecentColor(current, QColor(channel, 0, 0), kRecentColorCapacity);
    }
    ASSERT_EQ(current.size(), kRecentColorCapacity);
    EXPECT_EQ(current[0].red(), 8) << "最新的没排在最前";
    EXPECT_EQ(current[5].red(), 3) << "截断截错了方向：留下的应该是最活跃的六个";

    // 上限为 0 是一档显式的「不记」配置，不是「记一条」
    const auto none = pushRecentColor(colors({QColor(1, 2, 3)}), QColor(4, 5, 6), 0);
    EXPECT_TRUE(none.empty());

    // 无效色（取色对话框被取消那一类）不该改动清单
    const auto untouched = pushRecentColor(colors({QColor(9, 9, 9)}), QColor());
    ASSERT_EQ(untouched.size(), 1U);
    EXPECT_EQ(untouched[0].red(), 9);
}

/**
 * @brief 钉住序列化与读回是同一份契约的两端
 */
TEST(RecentColorsRoundTrip, SerializedListReadsBackEqual)
{
    const auto list   = colors({QColor(255, 0, 0), QColor(0, 120, 255), QColor(18, 52, 86)});
    const auto tokens = serializeRecentColors(list);
    ASSERT_EQ(tokens.size(), 3);
    EXPECT_EQ(tokens[2], QStringLiteral("#123456")) << "顺序变了，重启后色板上的位置就会和用户记忆里不一样";

    const auto readBack = parseRecentColors(tokens, "最近使用颜色", pickRecoveryHint());
    ASSERT_TRUE(readBack.has_value()) << readBack.error().message;
    EXPECT_EQ(readBack.value(), list);
}

/**
 * @brief 钉住坏值的拒绝面：整条拒绝而不是跳过，且原因里带上那条词
 * @details 提示里点名的是调用方读的那一格键，照着删才真能恢复默认
 */
TEST(RecentColorsRoundTrip, RejectsAnyTokenThatIsNotSixHexDigits)
{
    // 每条都是非空文本：空串的 find(\"\") 恒为 0，那样的断言不带信息（这里刻意不放空串）
    for (const auto *bad: {"red", "#GGGGGG", "#FF00", "#FF00000", " FF0000"})
    {
        const auto parsed = parseRecentColors({QStringLiteral("#123456"), QString::fromUtf8(bad)}, "最近使用颜色", pickRecoveryHint());
        ASSERT_FALSE(parsed.has_value()) << bad;
        EXPECT_EQ(parsed.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument) << bad;
        // 原因要点出是哪一条词与怎么恢复，否则用户只能对着配置文件猜；
        // 恢复办法说的必须是这条读路径真正读的键，照着删错键等于没修
        EXPECT_NE(parsed.error().message.find(QString::fromUtf8(bad).toStdString()), std::string::npos) << bad;
        EXPECT_NE(parsed.error().message.find(kPickRecentColors), std::string::npos) << bad;
        EXPECT_EQ(parsed.error().message.find(kAnnotationPalette), std::string::npos) << bad << " 提示指向了另一条读路径的键";
    }

    // 空清单是合法状态（用户还没用过自定义颜色），不能报成坏值
    const auto empty = parseRecentColors({}, "最近使用颜色", pickRecoveryHint());
    ASSERT_TRUE(empty.has_value());
    EXPECT_TRUE(empty->empty());
}

/**
 * @brief 钉住清单名与恢复办法都由调用方决定，提示里不写死任何一份清单
 * @details 同一个解析函数被取色器、标注最近色与自定义色板共用；提示里的清单名与键随调用方走，
 *          否则色板读坏时会叫用户去删最近使用那一格，白删一场
 */
TEST(RecentColorsRoundTrip, BadTokenUsesTheCallersListLabelAndRecoveryHint)
{
    const auto recovery = std::format("删除配置中的 {} 可恢复默认设置", kAnnotationPalette);
    const auto parsed   = parseRecentColors({QStringLiteral("#123456"), QStringLiteral("#GGGGGG")}, "自定义色板", recovery);
    ASSERT_FALSE(parsed.has_value());
    EXPECT_EQ(parsed.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument);
    EXPECT_NE(parsed.error().message.find("自定义色板"), std::string::npos) << " 提示没带上调用方给的清单名";
    EXPECT_NE(parsed.error().message.find(kAnnotationPalette), std::string::npos) << " 提示没点名调用方给的那一格键";
    EXPECT_EQ(parsed.error().message.find("最近使用颜色"), std::string::npos) << " 提示沿用了另一条读路径的清单名";
}
