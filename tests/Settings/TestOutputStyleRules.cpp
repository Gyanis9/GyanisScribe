// 装饰三格的读取判据：没填过是默认档，填了就照收，某一格填歪只把那一格打回默认。
// 三格共用一个出口（Settings::readOutputDecorationStyle），画布尺寸与绘制拿到同一份参数才不会错位。

#include "Core/OutputDecoration.h"
#include "Settings/OutputStyleRules.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"

#include <QColor>
#include <QCoreApplication>
#include <QString>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>

using GyanisScribe::Core::kOutputBorderColor;
using GyanisScribe::Core::kOutputBorderPixels;
using GyanisScribe::Core::kOutputShadowPixels;
using GyanisScribe::Core::OutputDecorationStyle;
using GyanisScribe::Settings::readOutputDecorationStyle;
using GyanisScribe::Settings::SettingsStore;

namespace
{
    std::unique_ptr<QCoreApplication> application;

    class OutputStyleFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestOutputStyleRules";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QCoreApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            // 每条用例一份 ini：并行跑时两个进程写同一个设置文件会互相盖掉
            const auto caseName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            m_iniPath           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-OutputStyle-{}.ini", caseName);
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
 * @brief 钉住三格都没填过时交回默认档
 * @details 装饰是「要的时候才有」的一档，默认值不能依赖界面先写过一次：命令行与容器里没人开设置页。
 */
TEST_F(OutputStyleFixture, MissingCellsReadAsTheDefaultLayer)
{
    const auto style = readOutputDecorationStyle(*m_store);
    EXPECT_EQ(style.borderPixels, kOutputBorderPixels);
    EXPECT_EQ(style.borderColor, kOutputBorderColor);
    EXPECT_EQ(style.shadowPixels, kOutputShadowPixels);
}

/**
 * @brief 钉住填进去的三格原样读回来
 * @details 颜色那一路存的是文本、读回来要同一份 RGB；两格宽度不许被夹或被舍。
 */
TEST_F(OutputStyleFixture, StoredCellsRoundTrip)
{
    ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kOutputBorderWidth, 7)));
    ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kOutputBorderColor, QStringLiteral("#00ff00"))));
    ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kOutputShadowWidth, 30)));

    const auto style = readOutputDecorationStyle(*m_store);
    EXPECT_EQ(style.borderPixels, 7) << "边框宽没按这一档读出来";
    EXPECT_EQ(style.borderColor, QColor(0, 255, 0)) << "边框色没按这一档读出来";
    EXPECT_EQ(style.shadowPixels, 30) << "投影宽没按这一档读出来";
}

/**
 * @brief 钉住某一格填歪只把那一格打回默认，另外两格照收
 * @details 三格一起回落是最容易写错也最难看出的一种：用户改了颜色与投影宽，发现边框宽填歪之后
 *          另外两格也一起回了默认，会以为设置根本没保存。
 */
TEST_F(OutputStyleFixture, OneBrokenCellFallsBackOnItsOwn)
{
    ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kOutputBorderWidth, 999)));
    ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kOutputBorderColor, QStringLiteral("#0f0"))));
    ASSERT_TRUE(static_cast<bool>(m_store->setValue(GyanisScribe::Settings::kOutputShadowWidth, 24)));

    const auto style = readOutputDecorationStyle(*m_store);
    EXPECT_EQ(style.borderPixels, kOutputBorderPixels) << "越界的边框宽被照收进参数里了";
    EXPECT_EQ(style.borderColor, QColor(0, 255, 0)) << "别格的坏值把颜色一起打回了默认";
    EXPECT_EQ(style.shadowPixels, 24) << "别格的坏值把投影宽一起打回了默认";
}
