#include "Gui/Controls/HotkeyEdit.h"

#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <Qt>

#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QWidget>

#include <filesystem>
#include <format>
#include <memory>

using GyanisScribe::Gui::editableKeyFromEvent;
using GyanisScribe::Gui::HotkeyEdit;
using GyanisScribe::Gui::HotkeyEditScope;

namespace
{
    std::unique_ptr<QApplication> application;

    /// 录制框要真焦点才收得到 focusOut：起一份 QApplication，配置走本用例专属 INI
    class HotkeyEditFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestHotkeyEdit";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
            m_iniPath        = std::filesystem::temp_directory_path() / std::format("GyanisScribe-HotkeyEdit-{}.ini", info->name());
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

        std::filesystem::path                         m_iniPath; ///< 本用例独占的配置文件
        std::unique_ptr<GyanisScribe::Core::SettingsStore> m_store;   ///< 配置存储
    };
} // namespace


/**
 * @brief 钉住「修饰键本身不是热键的按键本体」
 * @details 只按到 Ctrl 时组合还不完整，收下它就会把半截组合写进配置，
 *          重启后那一档热键登记不上，症状离原因很远。
 */
TEST(HotkeyEditMapping, ModifierKeysAreNotHotkeyBodies)
{
    for (const auto key: {Qt::Key_Control, Qt::Key_Shift, Qt::Key_Alt, Qt::Key_AltGr, Qt::Key_Meta, Qt::Key_CapsLock, Qt::Key_NumLock, Qt::Key_ScrollLock})
    {
        EXPECT_FALSE(editableKeyFromEvent(static_cast<int>(key)).has_value()) << static_cast<int>(key);
    }
}

/**
 * @brief 钉住小键盘数字归一到主键盘
 * @details 同一颗键在两台机器上存成两种写法，用户换台机器就会发现热键「莫名其妙变了」；
 *          字母与功能键则原样通过，不该被这次归一误伤。
 */
TEST(HotkeyEditMapping, KeypadDigitsNormalizeToTheMainRow)
{
    // 小键盘数字在事件里是「数字键值 | KeypadModifier」，Qt6 把这个组合叫 QKeyCombination，
    // 这里按它的整数形态造出来，判据是归一之后回到主键盘那一枚键
    // 只当整数用：0x20000035 超出 Qt::Key 的取值范围，硬转成 Qt::Key 就是未定义行为
    // （UBSan 在容器里报的正是这个「load of value ... not a valid value for type 'Key'」）
    const int keypadFive = static_cast<int>(Qt::KeypadModifier) | static_cast<int>(Qt::Key_5);

    EXPECT_EQ(editableKeyFromEvent(Qt::Key_5), Qt::Key_5);
    EXPECT_EQ(editableKeyFromEvent(keypadFive), Qt::Key_5);
    EXPECT_EQ(editableKeyFromEvent(Qt::Key_A), Qt::Key_A);
    EXPECT_EQ(editableKeyFromEvent(Qt::Key_F7), Qt::Key_F7);
    EXPECT_EQ(editableKeyFromEvent(Qt::Key_Space), Qt::Key_Space);
}

/**
 * @brief 钉住「录制中失焦即取消」：显示回到绑定值，之后按下的键不再改写配置
 * @details 只重写鼠标与按键时，点走别处之后录制态仍留着；下一次 Tab 回来随手按一个键，
 *          那个键就被当成这次录制的成果写进配置，用户看到的是热键「自己变了」。
 */
TEST_F(HotkeyEditFixture, FocusOutCancelsRecording)
{
    QWidget container;
    auto *layout = new QVBoxLayout{&container};
    auto *edit   = new HotkeyEdit{*m_store, &container};
    auto *other  = new QWidget{&container};
    other->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(edit);
    layout->addWidget(other);
    container.show();
    container.activateWindow();
    QCoreApplication::processEvents();

    const auto key     = GyanisScribe::Core::kHotkeyCapture;
    const auto bound   = QStringLiteral("Ctrl+Alt+A");
    const auto written = m_store->setValue(key, bound);
    ASSERT_TRUE(written.has_value()) << written.error().message;
    edit->bindTo(key, QStringLiteral("截图"), QStringLiteral("Ctrl+Alt+B"), HotkeyEditScope::Global);

    auto *display = edit->findChild<QLabel *>(QStringLiteral("hotkeyDisplay"));
    ASSERT_NE(display, nullptr) << "录制框里没有显示组合的那一格";
    ASSERT_EQ(display->text(), bound);

    // 点一下进入录制态；焦点也要真的落上来，后面的失焦才有人收
    edit->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    ASSERT_TRUE(edit->hasFocus()) << "焦点没落到录制框，失焦判据没有落点";
    QMouseEvent press{QEvent::MouseButtonPress, QPointF{5, 5}, QPointF{5, 5}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QCoreApplication::sendEvent(edit, &press);
    ASSERT_TRUE(display->property("recording").toBool()) << "点一下没有进入录制态，判据没有落点";
    ASSERT_NE(display->text(), bound) << "进入录制态后还显示旧组合，用户不知道程序在等按键";

    // 点走别处：等于放弃这次录制
    other->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    ASSERT_FALSE(edit->hasFocus()) << "焦点没有转走，失焦事件根本没发生";
    EXPECT_FALSE(display->property("recording").toBool()) << "失焦后仍留在录制态：下一次 Tab 回来按的键会被当成这次录制的结果";
    EXPECT_EQ(display->text(), bound) << "失焦没有恢复显示值，录制中的提示停在框里";

    // Tab 回来再按一个普通键：这次录制已经取消，配置不许被改写
    edit->setFocus(Qt::TabFocusReason);
    QCoreApplication::processEvents();
    ASSERT_TRUE(edit->hasFocus());
    QKeyEvent keyPress{QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier};
    QCoreApplication::sendEvent(edit, &keyPress);
    QCoreApplication::processEvents();

    EXPECT_EQ(m_store->stringValue(key, QStringLiteral("missing")), bound) << "失焦取消后按下的键仍然写进了配置";
    EXPECT_EQ(display->text(), bound) << "取消之后显示被按下的键改掉了";
}

/**
 * @brief 钉住录制中按 Esc 还原现值、退出录制态，配置原样不动
 * @details Esc 的意思是「我不改这一项」，不是「把这一项清空」；录制提示与写入失败的提示
 *          都会改显示文本，取消时必须回到绑定值而不是回到空。
 */
TEST_F(HotkeyEditFixture, EscapeRestoresTheBoundValue)
{
    QWidget container;
    auto *layout = new QVBoxLayout{&container};
    auto *edit   = new HotkeyEdit{*m_store, &container};
    layout->addWidget(edit);
    container.show();
    container.activateWindow();
    QCoreApplication::processEvents();

    const auto key     = GyanisScribe::Core::kHotkeyCapture;
    const auto bound   = QStringLiteral("Ctrl+Alt+A");
    const auto written = m_store->setValue(key, bound);
    ASSERT_TRUE(written.has_value()) << written.error().message;
    edit->bindTo(key, QStringLiteral("截图"), QStringLiteral("Ctrl+Alt+B"), HotkeyEditScope::Global);

    auto *display = edit->findChild<QLabel *>(QStringLiteral("hotkeyDisplay"));
    ASSERT_NE(display, nullptr) << "录制框里没有显示组合的那一格";

    edit->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    ASSERT_TRUE(edit->hasFocus()) << "焦点没落到录制框，按键判据没有落点";
    QMouseEvent press{QEvent::MouseButtonPress, QPointF{5, 5}, QPointF{5, 5}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QCoreApplication::sendEvent(edit, &press);
    ASSERT_TRUE(display->property("recording").toBool()) << "点一下没有进入录制态，判据没有落点";
    ASSERT_NE(display->text(), bound) << "进入录制态后还显示旧组合，用户不知道程序在等按键";

    QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
    QCoreApplication::sendEvent(edit, &escape);
    QCoreApplication::processEvents();

    EXPECT_FALSE(display->property("recording").toBool()) << "按 Esc 之后仍留在录制态";
    EXPECT_EQ(display->text(), bound) << "Esc 没有把显示还原成绑定值，录制提示停在框里";
    EXPECT_EQ(m_store->stringValue(key, QStringLiteral("missing")), bound) << "Esc 改写了配置，本意是放弃这次录制";
}

