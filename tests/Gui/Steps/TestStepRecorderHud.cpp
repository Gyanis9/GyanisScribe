// 录制控制条的两件事：读数与暂停态跟着录制方走，按钮各自发信号；摆位在可用区顶部居中。
// 控制条是工具窗且不激活：用户接着要操作的是别的程序，它一抢焦点第一次点击就会落空。

#include "Gui/Steps/StepRecorderHud.h"

#include "Core/Settings/SettingsStore.h"
#include "Gui/Theme/ThemeManager.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>

using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Gui::StepRecorderHud;
using GyanisScribe::Gui::ThemeManager;

namespace
{
    class StepRecorderHudFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const auto name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            m_ini           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-StepsHud-{}.ini", name);
            std::error_code ignored;
            std::filesystem::remove(m_ini, ignored);
            m_settings = std::make_unique<SettingsStore>(m_ini);
            m_theme    = std::make_unique<ThemeManager>(*m_settings);
        }

        void TearDown() override
        {
            m_theme.reset();
            m_settings.reset();
            std::error_code ignored;
            std::filesystem::remove(m_ini, ignored);
        }

        std::filesystem::path          m_ini;
        std::unique_ptr<SettingsStore> m_settings;
        std::unique_ptr<ThemeManager>  m_theme;
    };
} // namespace

/// HUD 显示步数/暂停态，按钮发信号；位置在可用区顶部居中
TEST_F(StepRecorderHudFixture, ShowsCountAndEmitsControls)
{
    StepRecorderHud hud{*m_theme};
    auto           *count = hud.findChild<QLabel *>(QStringLiteral("stepsHudCount"));
    auto           *pause = hud.findChild<QPushButton *>(QStringLiteral("stepsHudPause"));
    auto           *stop  = hud.findChild<QPushButton *>(QStringLiteral("stepsHudStop"));
    ASSERT_NE(count, nullptr);
    ASSERT_NE(pause, nullptr);
    ASSERT_NE(stop, nullptr);

    hud.setStepCount(-3);
    EXPECT_EQ(count->text(), QStringLiteral("步骤记录 · 0")) << "负数步数要按 0 显示";
    hud.setStepCount(7);
    EXPECT_EQ(count->text(), QStringLiteral("步骤记录 · 7"));
    hud.setPaused(true);
    EXPECT_EQ(pause->text(), QStringLiteral("继续"));
    EXPECT_TRUE(pause->toolTip().contains(QStringLiteral("继续"))) << "暂停后提示还写着重按暂停";
    hud.setPaused(false);
    EXPECT_EQ(pause->text(), QStringLiteral("暂停"));
    EXPECT_TRUE(pause->toolTip().contains(QStringLiteral("暂停"))) << "恢复后提示没有跟着回到暂停";

    QSignalSpy paused{&hud, &StepRecorderHud::pauseToggled};
    QSignalSpy stopped{&hud, &StepRecorderHud::stopRequested};
    pause->click();
    stop->click();
    EXPECT_EQ(paused.count(), 1);
    EXPECT_EQ(stopped.count(), 1);
    EXPECT_EQ(StepRecorderHud::positionFor(QRect{0, 0, 1920, 1080}, QSize{300, 48}), QPoint(810, 0));
}

/// 工具窗、无边框、置顶、显示时不激活；高度固定 48，控件以约定的对象名暴露给录制方
TEST_F(StepRecorderHudFixture, StaysOutOfTheWayAndKeepsItsGeometry)
{
    StepRecorderHud hud{*m_theme};

    EXPECT_EQ(hud.objectName(), QStringLiteral("stepsHud"));
    EXPECT_TRUE(hud.windowFlags().testFlag(Qt::Tool)) << "控制条会进任务栏或 Alt+Tab";
    EXPECT_TRUE(hud.windowFlags().testFlag(Qt::FramelessWindowHint));
    EXPECT_TRUE(hud.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    EXPECT_TRUE(hud.testAttribute(Qt::WA_ShowWithoutActivating)) << "显示控制条会抢走前台程序的焦点";
    EXPECT_EQ(hud.minimumHeight(), 48);
    EXPECT_EQ(hud.maximumHeight(), 48);
    EXPECT_EQ(hud.preferredSize().height(), 48);
    EXPECT_GE(hud.preferredSize().width(), 300) << "建议宽度低于下限，步数跳位时控制条会抽动";
}

/// 落点跟着可用区原点走，不是屏幕绝对坐标的一半
TEST_F(StepRecorderHudFixture, PositionFollowsTheAvailableAreaTopCenter)
{
    EXPECT_EQ(StepRecorderHud::positionFor(QRect{100, 40, 1920, 1040}, QSize{300, 48}), QPoint(910, 40));
    EXPECT_EQ(StepRecorderHud::positionFor(QRect{-1920, 0, 1920, 1080}, QSize{300, 48}), QPoint(-1110, 0));
}

/// 暂停时状态圆点转灰；恢复后回到危险色。圆点是自绘控件，颜色经 dotColorForTesting 读出
TEST_F(StepRecorderHudFixture, DotTurnsGreyWhilePaused)
{
    StepRecorderHud hud{*m_theme};
    auto           *dot = hud.findChild<QWidget *>(QStringLiteral("stepsHudDot"));
    ASSERT_NE(dot, nullptr) << "控制条上没有状态圆点";
    const QSize dotDiameter{10, 10};
    EXPECT_EQ(dot->size(), dotDiameter) << "状态圆点直径没有固定在 10 逻辑像素";

    const auto palette = m_theme->palette();
    EXPECT_EQ(hud.dotColorForTesting(), palette.danger) << "录制中圆点没取主题的危险色";

    hud.setPaused(true);
    EXPECT_EQ(hud.dotColorForTesting(), palette.secondaryText) << "暂停后圆点没有转灰";
    EXPECT_NE(hud.dotColorForTesting(), palette.danger);

    hud.setPaused(false);
    EXPECT_EQ(hud.dotColorForTesting(), palette.danger) << "恢复后圆点没有回到危险色";
    EXPECT_NE(hud.dotColorForTesting(), palette.secondaryText);
}
