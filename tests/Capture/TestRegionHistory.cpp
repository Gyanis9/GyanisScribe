// 选区历史的存取：文本往返、坏文本点名第几段、压进历史的去重与上限、
// 读整份历史时对升级前单值键的回退。读写的落点都在设置里，每条用例各用自己的一份 ini。

#include "Capture/RegionHistory.h"

#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QVariant>

#include <filesystem>
#include <format>
#include <memory>
#include <string>

using GyanisScribe::Capture::kRegionHistoryLimit;
using GyanisScribe::Capture::loadRegionHistory;
using GyanisScribe::Capture::pushedRegionHistory;
using GyanisScribe::Capture::RegionHistoryEntry;
using GyanisScribe::Capture::regionHistoryFromText;
using GyanisScribe::Capture::regionHistoryToText;
using GyanisScribe::Capture::regionsOnScreen;
using GyanisScribe::Capture::rememberRegion;
using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Settings::SettingsStore;

namespace
{
    std::unique_ptr<QApplication> application;

    class RegionHistoryFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestRegionHistory";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            // 每条用例一份 ini：ctest 并行时同名的键会互相盖掉
            const auto caseName = std::string_view(::testing::UnitTest::GetInstance()->current_test_info()->name());
            m_iniFile           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-RegionHistory-{}.ini", caseName);
            std::error_code ignored;
            std::filesystem::remove(m_iniFile, ignored);
        }

        void TearDown() override
        {
            std::error_code ignored;
            std::filesystem::remove(m_iniFile, ignored);
        }

        std::filesystem::path m_iniFile; ///< 本用例专用的设置文件
    };
} // namespace

/**
 * @brief 钉住文本往返：顺序、坐标与屏名都不丢
 */
TEST_F(RegionHistoryFixture, TextRoundTripsEntriesInOrder)
{
    const std::vector<RegionHistoryEntry> entries{{QRect(10, 20, 30, 40), QStringLiteral("Screen-A")}, {QRect(1, 2, 3, 4), QStringLiteral("Screen-B")}};

    const auto text = regionHistoryToText(entries);
    EXPECT_EQ(text, QStringLiteral("10,20,30,40,Screen-A;1,2,3,4,Screen-B"));

    const auto parsed = regionHistoryFromText(text.toStdString());
    ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
    EXPECT_EQ(*parsed, entries) << "往返一趟内容变了";
}

/**
 * @brief 钉住空文本是合法输入：还没有历史不是错误
 */
TEST_F(RegionHistoryFixture, EmptyTextMeansNoHistory)
{
    const auto parsed = regionHistoryFromText("");
    ASSERT_TRUE(parsed.has_value()) << "空文本被当成坏记录：第一次截图前读历史会莫名报错";
    EXPECT_TRUE(parsed->empty());
}

/**
 * @brief 钉住坏文本点名第几段而不猜
 */
TEST_F(RegionHistoryFixture, MalformedTextNamesTheSegment)
{
    const auto missingField = regionHistoryFromText("10,20,30,40,Screen-A;1,2,3");
    ASSERT_FALSE(missingField.has_value()) << "少一段也被收下，屏名会错位到数字上";
    EXPECT_NE(missingField.error().message.find("第 2 段"), std::string::npos) << missingField.error().message;

    const auto badNumber = regionHistoryFromText("10,20,三十,40,Screen-A");
    ASSERT_FALSE(badNumber.has_value()) << "非整数也被收下";
    EXPECT_EQ(badNumber.error().code, ErrorCode::InvalidArgument);
}

/**
 * @brief 钉住压进历史的三条规矩：去重前移、新的最前、超上限丢最久
 */
TEST_F(RegionHistoryFixture, PushDedupesMovesToFrontAndCaps)
{
    std::vector<RegionHistoryEntry> history;
    for (int i = 0; i < static_cast<int>(kRegionHistoryLimit) + 2; ++i)
    {
        history = pushedRegionHistory(history, RegionHistoryEntry{QRect(i, i, 10, 10), QStringLiteral("S")});
    }
    ASSERT_EQ(history.size(), kRegionHistoryLimit) << "超出上限没丢掉最久的一条";
    EXPECT_EQ(history.front().region, QRect(6, 6, 10, 10)) << "新的没排在最前";
    EXPECT_EQ(history.back().region, QRect(2, 2, 10, 10)) << "该丢的是最久的两条";

    // 把中间一条按同屏同矩形再记一次：前移到最前，不重复记
    history = pushedRegionHistory(history, history[3]);
    ASSERT_EQ(history.size(), kRegionHistoryLimit) << "去重把条数改掉了";
    EXPECT_EQ(history.front().region, QRect(3, 3, 10, 10)) << "重记的一条没前移到最前";
    int occurrences = 0;
    for (const auto &entry: history)
    {
        if (entry.region == QRect(3, 3, 10, 10))
        {
            ++occurrences;
        }
    }
    EXPECT_EQ(occurrences, 1) << "同屏同矩形被记了两遍";
}

/**
 * @brief 钉住读整份历史的两条路：历史键优先；只剩升级前的单值键时当作一条
 */
TEST_F(RegionHistoryFixture, LoadPrefersHistoryAndFallsBackToLegacyKeys)
{
    SettingsStore settings(m_iniFile);

    // 只有升级前的单值键：当作一条历史，旧记录还复用得上
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureLastRegion, QVariant(QStringLiteral("5,6,7,8"))).has_value());
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureLastScreen, QVariant(QStringLiteral("Legacy-Screen"))).has_value());
    {
        const auto loaded = loadRegionHistory(settings);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
        ASSERT_EQ(loaded->size(), 1U) << "旧单值键没被回退读成一条历史";
        EXPECT_EQ(loaded->front().region, QRect(5, 6, 7, 8));
        EXPECT_EQ(loaded->front().screenName, QStringLiteral("Legacy-Screen"));
    }

    // 历史键在：不再看旧键
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureRegionHistory, QVariant(QStringLiteral("1,1,2,2,Fresh-Screen"))).has_value());
    {
        const auto loaded = loadRegionHistory(settings);
        ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
        ASSERT_EQ(loaded->size(), 1U);
        EXPECT_EQ(loaded->front().screenName, QStringLiteral("Fresh-Screen")) << "有历史还去读旧键，两本账会各说各话";
    }
}

/**
 * @brief 钉住按屏挑历史：只挑同屏的几条、保持顺序；别屏的一条都不许混进来
 * @details 遮罩铺的是特定一块屏：混进别屏的矩形，R/Shift+R 会挑出一块越出这块屏的框。
 */
TEST_F(RegionHistoryFixture, RegionsOnScreenKeepsOnlyThatScreenInOrder)
{
    const std::vector<RegionHistoryEntry> history{
            {QRect(1, 1, 2, 2), QStringLiteral("Screen-A")}, {QRect(3, 3, 4, 4), QStringLiteral("Screen-B")}, {QRect(5, 5, 6, 6), QStringLiteral("Screen-A")}};

    const auto onA = regionsOnScreen(history, QStringLiteral("Screen-A"));
    ASSERT_EQ(onA.size(), 2U) << "同屏的两条该都在，别屏的一条都不该混进来";
    EXPECT_EQ(onA[0], QRect(1, 1, 2, 2)) << "顺序没保持：循环挑会从中间的某条起步";
    EXPECT_EQ(onA[1], QRect(5, 5, 6, 6));

    EXPECT_TRUE(regionsOnScreen(history, QStringLiteral("Screen-C")).empty()) << "没有历史的屏该拿到空清单";
}

/**
 * @brief 钉住记一条的两本账同源：单值键永远是历史首条
 */
TEST_F(RegionHistoryFixture, RememberWritesHistoryAndTheLegacyHeadTogether)
{
    SettingsStore settings(m_iniFile);

    rememberRegion(settings, RegionHistoryEntry{QRect(1, 2, 3, 4), QStringLiteral("S1")});
    rememberRegion(settings, RegionHistoryEntry{QRect(5, 6, 7, 8), QStringLiteral("S2")});

    settings.sync();
    SettingsStore reopened(m_iniFile);
    EXPECT_EQ(reopened.stringValue(GyanisScribe::Settings::kCaptureRegionHistory, QString()), QStringLiteral("5,6,7,8,S2;1,2,3,4,S1"));
    EXPECT_EQ(reopened.stringValue(GyanisScribe::Settings::kCaptureLastRegion, QString()), QStringLiteral("5,6,7,8")) << "单值键该与历史首条同一条";
    EXPECT_EQ(reopened.stringValue(GyanisScribe::Settings::kCaptureLastScreen, QString()), QStringLiteral("S2"));
}
