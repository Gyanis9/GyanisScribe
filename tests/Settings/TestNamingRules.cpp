// 文件名模板的读取判据：读不出、空着、写歪三条路各自落到哪儿。
// 读取与回落只有 Settings::nameTemplateOrDefault 这一处，四个写盘口都吃它的输出。

#include "Settings/NamingRules.h"

#include "Base/Paths.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"

#include <QCoreApplication>
#include <QString>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>

using GyanisScribe::Base::Paths;
using GyanisScribe::Settings::kSaveNameTemplate;
using GyanisScribe::Settings::nameTemplateOrDefault;
using GyanisScribe::Settings::SettingsStore;

namespace
{
    std::unique_ptr<QCoreApplication> application;

    /// 每条用例一份 ini：并行跑时两个进程写同一个设置文件会互相盖掉
    class NamingFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestNamingRules";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QCoreApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            const auto caseName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            m_iniPath           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Naming-{}.ini", caseName);
            std::error_code ignored;
            std::filesystem::remove(m_iniPath, ignored);
            m_store = std::make_unique<SettingsStore>(m_iniPath);
        }

        void TearDown() override
        {
            m_store.reset();
            std::error_code ignored;
            std::filesystem::remove(m_iniPath, ignored);
        }

        std::filesystem::path          m_iniPath;
        std::unique_ptr<SettingsStore> m_store;
    };
} // namespace

/**
 * @brief 钉住没填过这一格时交回默认档
 * @details 留空是合法选择而不是读失败：这一路不记警告，否则每次截图都往日志里塞一条。
 */
TEST_F(NamingFixture, MissingValueMeansTheDefaultTemplate)
{
    EXPECT_EQ(nameTemplateOrDefault(*m_store), Paths::defaultNameTemplate());
}

/**
 * @brief 钉住填歪了也交回默认档，而不是把带分隔符的模板交给写盘那一侧
 */
TEST_F(NamingFixture, BrokenValueFallsBackToTheDefaultTemplate)
{
    ASSERT_TRUE(static_cast<bool>(m_store->setValue(kSaveNameTemplate, QStringLiteral("../逃出去"))));
    EXPECT_EQ(nameTemplateOrDefault(*m_store), Paths::defaultNameTemplate()) << "带路径分隔符的模板不该被交出去";
}

/**
 * @brief 钉住填对了就用填的那一份，并先去掉首尾空白
 */
TEST_F(NamingFixture, StoredTemplateIsTrimmedAndHandedBack)
{
    ASSERT_TRUE(static_cast<bool>(m_store->setValue(kSaveNameTemplate, QStringLiteral("  截图_%date%  "))));
    EXPECT_EQ(nameTemplateOrDefault(*m_store), QStringLiteral("截图_%date%"));
}
