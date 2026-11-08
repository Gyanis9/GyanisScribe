#include "Core/Rewind/RewindConfig.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"

#include <QCoreApplication>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>

using GyanisScribe::Core::isExcludedProcessName;
using GyanisScribe::Core::kDefaultRewindBufferSeconds;
using GyanisScribe::Core::kDefaultRewindFrameRate;
using GyanisScribe::Core::kMaximumRewindBufferSeconds;
using GyanisScribe::Core::kMaximumRewindFrameRate;
using GyanisScribe::Core::kMinimumRewindBufferSeconds;
using GyanisScribe::Core::kMinimumRewindFrameRate;
using GyanisScribe::Core::kRewindBufferSeconds;
using GyanisScribe::Core::kRewindEnabled;
using GyanisScribe::Core::kRewindExcludeProcesses;
using GyanisScribe::Core::kRewindFrameRate;
using GyanisScribe::Core::kRewindMemoryBudgetMb;
using GyanisScribe::Core::parseRewindExclusions;
using GyanisScribe::Core::readRewindConfig;
using GyanisScribe::Core::SettingsStore;

namespace
{
    std::unique_ptr<QCoreApplication> application;

    /// QSettings 的后端位置与 QVariant 的元类型都要在应用对象之后才可靠，用例自己把它立起来
    class RewindConfigFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestRewindConfig";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QCoreApplication>(argumentCount, arguments);
            QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
            QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe-Tests"));
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            const auto caseName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            m_iniFile           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Rewind-{}.ini", caseName);
            std::error_code ignored;
            std::filesystem::remove(m_iniFile, ignored);
        }

        void TearDown() override
        {
            std::error_code ignored;
            std::filesystem::remove(m_iniFile, ignored);
        }

        std::filesystem::path m_iniFile;
    };
} // namespace

/**
 * @brief 钉住没写过任何一项时拿到的是关闭、默认时长与帧率、空排除名单
 */
TEST_F(RewindConfigFixture, DefaultsWhenUnset)
{
    SettingsStore store(m_iniFile);
    const auto    config = readRewindConfig(store);
    ASSERT_TRUE(config.has_value());
    EXPECT_FALSE(config->enabled);
    EXPECT_EQ(config->bufferSeconds, kDefaultRewindBufferSeconds);
    EXPECT_EQ(config->frameRate, kDefaultRewindFrameRate);
    EXPECT_TRUE(config->excludedProcesses.isEmpty());
}

/**
 * @brief 钉住越界的时长与帧率回落到默认档，而不是被截到边界上
 */
TEST_F(RewindConfigFixture, BadNumbersFallBackToDefaults)
{
    SettingsStore store(m_iniFile);
    ASSERT_TRUE(store.setValue(kRewindBufferSeconds, QStringLiteral("99")).has_value());
    ASSERT_TRUE(store.setValue(kRewindFrameRate, QStringLiteral("1")).has_value());

    const auto config = readRewindConfig(store);
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->bufferSeconds, kDefaultRewindBufferSeconds);
    EXPECT_EQ(config->frameRate, kDefaultRewindFrameRate);
}

/**
 * @brief 钉住区间内的时长与帧率原样读回，字节预算保持固定档
 */
TEST_F(RewindConfigFixture, InRangeValuesRoundTrip)
{
    SettingsStore store(m_iniFile);
    ASSERT_TRUE(store.setValue(kRewindBufferSeconds, QStringLiteral("20")).has_value());
    ASSERT_TRUE(store.setValue(kRewindFrameRate, QStringLiteral("8")).has_value());
    store.sync();

    SettingsStore reopened(m_iniFile);
    const auto    config = readRewindConfig(reopened);
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->bufferSeconds, 20);
    EXPECT_EQ(config->frameRate, 8);
    EXPECT_EQ(config->memoryBudgetMb, kRewindMemoryBudgetMb);
}

/**
 * @brief 钉住时长与帧率的上下边界本身合法，不会被当成越界回落到默认档
 */
TEST_F(RewindConfigFixture, InclusiveBoundsAreAccepted)
{
    for (const int seconds: {kMinimumRewindBufferSeconds, kMaximumRewindBufferSeconds})
    {
        SettingsStore store(m_iniFile);
        ASSERT_TRUE(store.setValue(kRewindBufferSeconds, QString::number(seconds)).has_value());
        const auto config = readRewindConfig(store);
        ASSERT_TRUE(config.has_value());
        EXPECT_EQ(config->bufferSeconds, seconds);
    }

    for (const int frameRate: {kMinimumRewindFrameRate, kMaximumRewindFrameRate})
    {
        SettingsStore store(m_iniFile);
        ASSERT_TRUE(store.setValue(kRewindFrameRate, QString::number(frameRate)).has_value());
        const auto config = readRewindConfig(store);
        ASSERT_TRUE(config.has_value());
        EXPECT_EQ(config->frameRate, frameRate);
    }
}

/**
 * @brief 钉住排除名单去空白、转小写、去重并保留先出现的顺序，比对时不区分大小写
 */
TEST_F(RewindConfigFixture, ParsesAndNormalizesExclusions)
{
    SettingsStore store(m_iniFile);
    ASSERT_TRUE(store.setValue(kRewindExcludeProcesses, QStringLiteral(" Keepass.exe , , 1password.EXE, keepass.exe ")).has_value());

    const auto config = readRewindConfig(store);
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->excludedProcesses, (QStringList{"keepass.exe", "1password.exe"}));
    EXPECT_TRUE(isExcludedProcessName(QStringLiteral("KEEPASS.EXE"), config->excludedProcesses));
    EXPECT_TRUE(isExcludedProcessName(QStringLiteral("keepass.exe"), config->excludedProcesses));
    EXPECT_FALSE(isExcludedProcessName(QStringLiteral("chrome.exe"), config->excludedProcesses));
}

/**
 * @brief 钉住名单项与进程名的 .exe 后缀两侧都可比，空进程名一律不命中
 */
TEST_F(RewindConfigFixture, MatchesProcessNamesWithOrWithoutExeSuffix)
{
    const QStringList withoutSuffix = parseRewindExclusions(QStringLiteral("keepass")).value();
    EXPECT_TRUE(isExcludedProcessName(QStringLiteral("keepass.exe"), withoutSuffix));
    EXPECT_TRUE(isExcludedProcessName(QStringLiteral("KEEPASS.EXE"), withoutSuffix));
    EXPECT_TRUE(isExcludedProcessName(QStringLiteral("keepass"), withoutSuffix));
    EXPECT_FALSE(isExcludedProcessName(QString(), withoutSuffix));
    EXPECT_FALSE(isExcludedProcessName(QStringLiteral("   "), withoutSuffix));

    const QStringList withSuffix = parseRewindExclusions(QStringLiteral("keepass.exe")).value();
    EXPECT_TRUE(isExcludedProcessName(QStringLiteral("keepass"), withSuffix));
    EXPECT_TRUE(isExcludedProcessName(QStringLiteral("keepass.exe"), withSuffix));
}

/**
 * @brief 钉住带着路径、通配符或超长的条目被拒，报错里带上能定位的原因
 */
TEST_F(RewindConfigFixture, RejectsPathsWildcardsAndOversize)
{
    const auto path = parseRewindExclusions(QStringLiteral("C:/Tools/keepass.exe"));
    ASSERT_FALSE(path.has_value());
    EXPECT_NE(path.error().message.find("文件名"), std::string::npos);

    const auto wildcard = parseRewindExclusions(QStringLiteral("*.exe"));
    ASSERT_FALSE(wildcard.has_value());
    EXPECT_NE(wildcard.error().message.find("通配符"), std::string::npos);

    const auto oversize = parseRewindExclusions(QString(3000, QChar('a')));
    ASSERT_FALSE(oversize.has_value());
    EXPECT_NE(oversize.error().message.find("2048"), std::string::npos);
}

/**
 * @brief 钉住 Windows 文件名里不允许出现的字符被拒，报错仍指向文件名写法
 */
TEST_F(RewindConfigFixture, RejectsInvalidFileNameCharacters)
{
    for (const auto &text: {QStringLiteral("C:keepass.exe"), QStringLiteral("keep|ass.exe"), QStringLiteral("keep<ass>.exe"), QStringLiteral("keep\"ass.exe")})
    {
        const auto rejected = parseRewindExclusions(text);
        ASSERT_FALSE(rejected.has_value()) << text.toStdString();
        EXPECT_NE(rejected.error().message.find("文件名"), std::string::npos) << text.toStdString();
        EXPECT_NE(rejected.error().message.find("不能有"), std::string::npos) << text.toStdString();
    }
}

/**
 * @brief 钉住名单里有一项非法时整份配置失败，而不是悄悄丢弃那一项
 */
TEST_F(RewindConfigFixture, InvalidExclusionBlocksConfig)
{
    SettingsStore store(m_iniFile);
    ASSERT_TRUE(store.setValue(kRewindExcludeProcesses, QStringLiteral("C:/Tools/keepass.exe")).has_value());

    const auto config = readRewindConfig(store);
    ASSERT_FALSE(config.has_value());
    EXPECT_NE(config.error().message.find("keepass.exe"), std::string::npos);
}

/**
 * @brief 钉住开关写盘后换一个新实例读同一份文件仍然为开
 */
TEST_F(RewindConfigFixture, EnabledRoundTrips)
{
    SettingsStore store(m_iniFile);
    ASSERT_TRUE(store.setValue(kRewindEnabled, QVariant(true)).has_value());
    store.sync();

    SettingsStore reopened(m_iniFile);
    const auto    config = readRewindConfig(reopened);
    ASSERT_TRUE(config.has_value());
    EXPECT_TRUE(config->enabled);
}
