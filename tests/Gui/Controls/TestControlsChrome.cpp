// 控件外观判据：快捷键框与数字框要与圆角输入框同一套外观。
// 与 TestGuiControls 分成两个可执行体：那个目标里的 TestToast 自己持有一份 QApplication，
// 同一进程不能再造第二份。
#include "Gui/Controls/HotkeyEdit.h"
#include "Gui/Controls/SpinBox.h"

#include "Core/Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>

#include <filesystem>
#include <format>
#include <memory>

namespace
{
    std::unique_ptr<QApplication> application;

    class ControlsChromeEnvironment : public ::testing::Environment
    {
    public:
        void SetUp() override
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestControlsChrome";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        void TearDown() override
        {
            application.reset();
        }
    };

    [[maybe_unused]] ::testing::Environment *const environment = ::testing::AddGlobalTestEnvironment(new ControlsChromeEnvironment);

    class HotkeyEditChromeFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
            m_iniPath        = std::filesystem::temp_directory_path() / std::format("GyanisScribe-HotkeyEdit-{}-{}.ini", info->test_suite_name(), info->name());
            std::error_code ignored;
            std::filesystem::remove(m_iniPath, ignored);
            m_store = std::make_unique<GyanisScribe::Core::SettingsStore>(m_iniPath);
        }

        void TearDown() override
        {
            m_store.reset();
            std::error_code ignored;
            std::filesystem::remove(m_iniPath, ignored);
        }

        std::filesystem::path                              m_iniPath;
        std::unique_ptr<GyanisScribe::Core::SettingsStore> m_store;
    };
} // namespace

/**
 * @brief 钉住快捷键框走输入框同一套外观
 * @details 此前内部是 QFrame::Box 的裸 QLabel：方角、原生边框、居中文字，与圆角输入框完全是两件东西。
 */
TEST_F(HotkeyEditChromeFixture, DisplayUsesTheInputFieldChrome)
{
    GyanisScribe::Gui::HotkeyEdit edit{*m_store};
    auto                         *display = edit.findChild<QLabel *>(QStringLiteral("hotkeyDisplay"));
    ASSERT_NE(display, nullptr) << "快捷键框没有给显示标签命名，样式表挂不上";
    EXPECT_EQ(display->frameShape(), QFrame::NoFrame) << "还留着原生方框";
    EXPECT_TRUE(display->alignment().testFlag(Qt::AlignLeft)) << "输入框是左对齐，快捷键框还居中";
    EXPECT_EQ(display->sizePolicy().horizontalPolicy(), QSizePolicy::Expanding) << "没有吃满字段列";
}

/**
 * @brief 钉住录制态会翻出主色描边属性
 */
TEST_F(HotkeyEditChromeFixture, RecordingStateIsExposedToTheStyleSheet)
{
    GyanisScribe::Gui::HotkeyEdit edit{*m_store};
    auto                         *display = edit.findChild<QLabel *>(QStringLiteral("hotkeyDisplay"));
    ASSERT_NE(display, nullptr);
    EXPECT_FALSE(display->property("recording").toBool());

    QMouseEvent press{QEvent::MouseButtonPress, QPointF{5, 5}, QPointF{5, 5}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(&edit, &press);
    EXPECT_TRUE(display->property("recording").toBool()) << "点进录制态，样式表拿不到状态";

    QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
    QApplication::sendEvent(&edit, &escape);
    EXPECT_FALSE(display->property("recording").toBool()) << "Esc 取消后录制态属性没落回去";
}

namespace
{
    using GyanisScribe::Gui::SpinBox;

    /// SpinBox 判据与 HotkeyEdit 判据共用同一个进程级 QApplication（见文件头的 Environment）
    class SpinBoxFixture : public ::testing::Test
    {
    };
} // namespace

/**
 * @brief 钉住原生上下按钮已关掉、右上/右下两枚自绘按钮接管
 * @details 原生微调按钮与圆角输入框背景割裂；NoButtons 是这条修法的事实前提。
 */
TEST_F(SpinBoxFixture, UsesItsOwnStepperButtonsInsteadOfTheNativeOnes)
{
    SpinBox spin;
    spin.setRange(0, 10);
    spin.setAttribute(Qt::WA_DontShowOnScreen);
    spin.resize(120, 32);
    spin.show();
    QCoreApplication::processEvents();

    EXPECT_EQ(spin.buttonSymbols(), QAbstractSpinBox::NoButtons);
    auto *up   = spin.findChild<QAbstractButton *>(QStringLiteral("spinUp"));
    auto *down = spin.findChild<QAbstractButton *>(QStringLiteral("spinDown"));
    ASSERT_NE(up, nullptr);
    ASSERT_NE(down, nullptr);
    EXPECT_GE(up->geometry().left(), spin.width() - 24) << "自绘按钮没贴在右缘";
    EXPECT_LT(up->geometry().bottom(), down->geometry().top()) << "上下两枚叠在一起了";
}

/**
 * @brief 钉住点一下加一 / 减一，且键盘与范围逻辑仍由 QSpinBox 负责
 */
TEST_F(SpinBoxFixture, ClickingTheStepperStepsTheValue)
{
    SpinBox spin;
    spin.setRange(0, 10);
    spin.setValue(5);
    spin.setAttribute(Qt::WA_DontShowOnScreen);
    spin.resize(120, 32);
    spin.show();
    QCoreApplication::processEvents();

    auto *up   = spin.findChild<QAbstractButton *>(QStringLiteral("spinUp"));
    auto *down = spin.findChild<QAbstractButton *>(QStringLiteral("spinDown"));
    ASSERT_NE(up, nullptr);
    ASSERT_NE(down, nullptr);

    up->click();
    EXPECT_EQ(spin.value(), 6);
    down->click();
    down->click();
    EXPECT_EQ(spin.value(), 4);

    spin.setValue(10);
    up->click();
    EXPECT_EQ(spin.value(), 10) << "顶部没有夹住";
}
