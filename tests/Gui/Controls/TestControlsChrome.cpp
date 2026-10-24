// 控件外观判据：快捷键框与数字框要与圆角输入框同一套外观。
// 与 TestGuiControls 分成两个可执行体：那个目标里的 TestToast 自己持有一份 QApplication，
// 同一进程不能再造第二份。
#include "Gui/Controls/HotkeyEdit.h"
#include "Gui/Controls/SpinBox.h"
#include "Gui/Theme/DesignTokens.h"

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
#include <optional>

namespace
{
    std::unique_ptr<QApplication> application;

    class ControlsChromeEnvironment : public ::testing::Environment
    {
    public:
        void SetUp() override
        {
            // 焦点环与复选框的像素判据按逻辑像素取样点：真机 125%/150% 下 grab() 给出的是更大的图，
            // 同一个采样点会落到别处。这个二进制里的像素用例都在同一份 QApplication 下，强制倍比为 1。
            // 真缩放屏的观感仍留给人在实机上看。
            qputenv("QT_SCALE_FACTOR", "1");

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

/**
 * @brief 钉住数字框的数字锁定等宽：一秒一跳的读数不能左右抖
 */
TEST(SpinBoxChrome, UsesTabularFigures)
{
    GyanisScribe::Gui::SpinBox spin;
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    EXPECT_EQ(spin.font().featureValue(QFont::Tag("tnum")), 1u) << "数字框没锁等宽，读数会左右抖";
#endif
}

namespace
{
    using GyanisScribe::Gui::activeThemeKind;
    using GyanisScribe::Gui::applyTabularFigures;
    using GyanisScribe::Gui::applyTextRole;
    using GyanisScribe::Gui::reapplyTabularFigures;
    using GyanisScribe::Gui::reapplyTextRole;
    using GyanisScribe::Gui::setTextThemeKind;
    using GyanisScribe::Gui::setVariableMetricFamilyAvailableForTesting;
    using GyanisScribe::Gui::TextRole;
    using GyanisScribe::Gui::TextScript;
    using GyanisScribe::Gui::ThemeKind;
} // namespace

/**
 * @brief 钉住数字读数切回浅色档后回到基准字重
 * @details 暗色档的可变字族路径把 400 降到 360；重放时若只还原字族不还原字重，
 *          控件会一直停在 360，读数在整个浅色档里都偏细。可变字族的可用性由测试钩子钉死，
 *          不依赖跑测机器装没装 Segoe UI Variable Text。
 */
TEST(TabularFiguresChrome, RestoresBaseWeightWhenThemeReturnsToLight)
{
    const auto previousTheme = activeThemeKind();

    QLabel label;
    auto   font = label.font();
    font.setWeight(QFont::Normal);
    label.setFont(font);

    setVariableMetricFamilyAvailableForTesting(true);
    setTextThemeKind(ThemeKind::Dark);
    applyTabularFigures(label, TextScript::AsciiOnly);
    EXPECT_EQ(static_cast<int>(label.font().weight()), 360) << "暗色档没有按可变字族降重";

    setTextThemeKind(ThemeKind::Light);
    reapplyTabularFigures(label);
    EXPECT_EQ(static_cast<int>(label.font().weight()), 400) << "切回浅色档后字重没有还原";

    setVariableMetricFamilyAvailableForTesting(std::nullopt);
    setTextThemeKind(previousTheme);
}

/**
 * @brief 钉住暗色 Metric 只在 AsciiOnly 时换可变字族与 560，Mixed 停在默认字族 600
 * @details Mixed 读数里可能含中文，换字族与降重会把中文回退分家；切回浅色档后两种读数
 *          都要回到 600 与原始字族。可变字族的可用性由测试钩子钉死，不依赖跑测机器。
 */
TEST(TabularFiguresChrome, MetricRoleSwitchesFamilyOnlyForAsciiInDark)
{
    const auto previousTheme = activeThemeKind();
    setVariableMetricFamilyAvailableForTesting(true);
    setTextThemeKind(ThemeKind::Dark);

    QLabel label;
    auto   font = label.font();
    font.setWeight(QFont::Normal);
    label.setFont(font);
    const auto baseFamily = label.font().family();

    applyTextRole(label, TextRole::Metric, TextScript::Mixed);
    EXPECT_EQ(static_cast<int>(label.font().weight()), 600) << "Mixed 读数被降重";
    EXPECT_EQ(label.font().family(), baseFamily) << "Mixed 读数换了可变字族，中文回退会分家";

    applyTextRole(label, TextRole::Metric, TextScript::AsciiOnly);
    EXPECT_EQ(static_cast<int>(label.font().weight()), 560);
    EXPECT_EQ(label.font().family(), QStringLiteral("Segoe UI Variable Text"));

    setTextThemeKind(ThemeKind::Light);
    reapplyTextRole(label);
    EXPECT_EQ(static_cast<int>(label.font().weight()), 600) << "切回浅色档后没有回到 DemiBold";
    EXPECT_EQ(label.font().family(), baseFamily) << "切回浅色档后还挂着可变字族";

    setVariableMetricFamilyAvailableForTesting(std::nullopt);
    setTextThemeKind(previousTheme);
}
