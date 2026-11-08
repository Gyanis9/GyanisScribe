#include "Gui/Controls/HotkeyEdit.h"

#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"
#include "Gui/Controls/SurfaceWidget.h"
#include "Gui/Theme/ControlPalette.h"

#include <gtest/gtest.h>

#include <Qt>

#include <QApplication>
#include <QCoreApplication>
#include <QImage>
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

/**
 * @brief 钉住「配置被别处改掉时，这一格跟着变」
 * @details 热键管理器把恢复失败的新组合退回旧组合时只改配置：设置页上的录制框不跟，
 *          用户看到的就是新组合，实际按得动的却是旧组合。录制中这一格显示的是提示，
 *          外部改动只更新现值，不许把提示顶掉；取消后要显示最新的现值。
 */
TEST_F(HotkeyEditFixture, FollowsTheStoredValueWhenItChangesElsewhere)
{
    QWidget container;
    auto *layout = new QVBoxLayout{&container};
    auto *edit   = new HotkeyEdit{*m_store, &container};
    layout->addWidget(edit);
    container.show();
    container.activateWindow();
    QCoreApplication::processEvents();

    const auto key = GyanisScribe::Core::kHotkeyCapture;
    ASSERT_TRUE(m_store->setValue(key, QStringLiteral("Ctrl+Alt+A")).has_value());
    edit->bindTo(key, QStringLiteral("截图"), QStringLiteral("Ctrl+Alt+B"), HotkeyEditScope::Global);

    auto *display = edit->findChild<QLabel *>(QStringLiteral("hotkeyDisplay"));
    ASSERT_NE(display, nullptr) << "录制框里没有显示组合的那一格";
    ASSERT_EQ(display->text(), QStringLiteral("Ctrl+Alt+A"));

    ASSERT_TRUE(m_store->setValue(key, QStringLiteral("Ctrl+Alt+D")).has_value());
    EXPECT_EQ(display->text(), QStringLiteral("Ctrl+Alt+D")) << "配置在别处改了，这一格还显示旧值：用户看到的热键不是实际生效的那颗";

    // 点一下进入录制态；外部再改配置只更新现值，不许把「按下快捷键」的提示顶掉
    edit->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    ASSERT_TRUE(edit->hasFocus()) << "焦点没落到录制框，录制判据没有落点";
    QMouseEvent press{QEvent::MouseButtonPress, QPointF{5, 5}, QPointF{5, 5}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QCoreApplication::sendEvent(edit, &press);
    ASSERT_TRUE(display->property("recording").toBool()) << "点一下没有进入录制态，判据没有落点";

    ASSERT_TRUE(m_store->setValue(key, QStringLiteral("Ctrl+Alt+E")).has_value());
    EXPECT_TRUE(display->property("recording").toBool()) << "外部改配置把录制态弄丢了";
    EXPECT_NE(display->text(), QStringLiteral("Ctrl+Alt+E")) << "录制中的提示被外部改动顶掉了，用户不知道还在等按键";
    EXPECT_TRUE(display->text().contains(QStringLiteral("按下快捷键"))) << "录制中的提示文本不对：" << display->text().toStdString();

    QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
    QCoreApplication::sendEvent(edit, &escape);
    QCoreApplication::processEvents();

    EXPECT_FALSE(display->property("recording").toBool()) << "按 Esc 之后仍留在录制态";
    EXPECT_EQ(display->text(), QStringLiteral("Ctrl+Alt+E")) << "取消录制后没有显示外部改出的最新现值";
    EXPECT_EQ(m_store->stringValue(key, QStringLiteral("missing")), QStringLiteral("Ctrl+Alt+E")) << "Esc 改写了配置，本意是放弃这次录制";
}

/**
 * @brief 钉住「写入被管理器回滚后，这一格显示的是回读出来的旧组合」
 * @details setValue 同步发 valueChanged：监听器把旧组合写回配置时，applyCombination 若照抄刚提交的
 *          新文本，界面就停在被拒的组合上，配置里却是旧的。这一条让监听器模拟管理器回滚，
 *          断言配置、显示与录制态都以回读结果为准。
 */
TEST_F(HotkeyEditFixture, RolledBackCombinationStaysOnScreen)
{
    QWidget container;
    auto *layout = new QVBoxLayout{&container};
    auto *edit   = new HotkeyEdit{*m_store, &container};
    layout->addWidget(edit);
    container.show();
    container.activateWindow();
    QCoreApplication::processEvents();

    const auto key       = GyanisScribe::Core::kHotkeyCapture;
    const auto keyText   = QString::fromUtf8(key.data(), static_cast<int>(key.size()));
    const auto older     = QStringLiteral("Ctrl+Alt+A");
    const auto attempted = QStringLiteral("Ctrl+Alt+D");
    ASSERT_TRUE(m_store->setValue(key, older).has_value());
    edit->bindTo(key, QStringLiteral("截图"), QStringLiteral("Ctrl+Alt+B"), HotkeyEditScope::Global);

    auto *display = edit->findChild<QLabel *>(QStringLiteral("hotkeyDisplay"));
    ASSERT_NE(display, nullptr) << "录制框里没有显示组合的那一格";
    ASSERT_EQ(display->text(), older);

    // 模拟管理器：新组合被别的程序占走时同步把旧组合写回。守卫只让第一次回滚生效，防止自己触发自己
    bool rollingBack = false;
    bool rolledBack  = false;
    QObject::connect(m_store.get(), &GyanisScribe::Core::ISettingsStore::valueChanged, edit,
                     [&](const QString &changedKey)
                     {
                         if (rollingBack || changedKey != keyText)
                         {
                             return;
                         }
                         if (m_store->stringValue(key, QStringLiteral("missing")) != attempted)
                         {
                             return;
                         }
                         rollingBack = true;
                         rolledBack  = m_store->setValue(key, older).has_value();
                         rollingBack = false;
                     });

    edit->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    ASSERT_TRUE(edit->hasFocus()) << "焦点没落到录制框，按键判据没有落点";
    QMouseEvent press{QEvent::MouseButtonPress, QPointF{5, 5}, QPointF{5, 5}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QCoreApplication::sendEvent(edit, &press);
    ASSERT_TRUE(display->property("recording").toBool()) << "点一下没有进入录制态，判据没有落点";

    QKeyEvent record{QEvent::KeyPress, Qt::Key_D, Qt::ControlModifier | Qt::AltModifier};
    QCoreApplication::sendEvent(edit, &record);
    QCoreApplication::processEvents();

    ASSERT_TRUE(rolledBack) << "回滚监听没有被触发，这一条判据没有落点";
    EXPECT_EQ(m_store->stringValue(key, QStringLiteral("missing")), older) << "配置里不是回滚后的旧组合，回滚模拟没有成立";
    EXPECT_EQ(display->text(), older) << "写入被回滚后，这一格仍显示被拒的新组合：用户看到的热键不是实际生效的那颗";
    EXPECT_FALSE(display->property("recording").toBool()) << "写完一次组合后仍留在录制态";
}

/**
 * @brief 钉住「取消绑定后显示回落到默认文本，键从配置里删掉」
 * @details 界面按键那一档每一格都有默认键：删掉绑定后若留一块空白，用户会以为这颗键不能用了。
 */
TEST_F(HotkeyEditFixture, ClearBindingFallsBackToTheDefaultText)
{
    QWidget container;
    auto *layout = new QVBoxLayout{&container};
    auto *edit   = new HotkeyEdit{*m_store, &container};
    layout->addWidget(edit);
    container.show();
    container.activateWindow();
    QCoreApplication::processEvents();

    const auto key      = GyanisScribe::Core::kHotkeyCapture;
    const auto bound    = QStringLiteral("Ctrl+Alt+Q");
    const auto fallback = QStringLiteral("Ctrl+Alt+B");
    ASSERT_TRUE(m_store->setValue(key, bound).has_value());
    edit->bindTo(key, QStringLiteral("截图"), fallback, HotkeyEditScope::InWindow);

    auto *display = edit->findChild<QLabel *>(QStringLiteral("hotkeyDisplay"));
    ASSERT_NE(display, nullptr) << "录制框里没有显示组合的那一格";
    ASSERT_EQ(display->text(), bound);

    edit->clearBinding();
    QCoreApplication::processEvents();

    EXPECT_EQ(display->text(), fallback) << "删除绑定后没有回落到默认文本：界面按键那一格留在了空白";
    EXPECT_EQ(m_store->stringValue(key, QStringLiteral("missing")), QStringLiteral("missing")) << "取消绑定没有把键从配置里删掉";
}

/**
 * @brief 钉住显示框是自绘连续曲率底的 SurfaceLabel，并按录制/禁用换铺底角色
 * @details 录制态铺 accentSoft、禁用态铺 mutedFill、恢复后回到 controlFill；
 *          这三个颜色只存在于 SurfaceLabel 的 fillRole 里，QSS 已不再给 hotkeyDisplay 铺底。
 *          取样点留在右侧内边距：文字左对齐，那里只有底色。
 */
TEST_F(HotkeyEditFixture, DisplayPaintsRecordingAndDisabledFillRoles)
{
    QWidget container;
    auto   *edit = new HotkeyEdit{*m_store, &container};
    edit->setGeometry(0, 0, 320, 60);
    container.resize(320, 60);
    container.show();
    container.activateWindow();
    QCoreApplication::processEvents();

    auto *display = edit->findChild<QLabel *>(QStringLiteral("hotkeyDisplay"));
    ASSERT_NE(display, nullptr) << "录制框里没有显示组合的那一格";
    ASSERT_NE(dynamic_cast<GyanisScribe::Gui::SurfaceLabel *>(display), nullptr) << "显示框没有改用 SurfaceLabel，连续曲率底没有落笔处";

    const auto fillAt = [display]
    {
        const QImage image = display->grab().toImage();
        return image.pixelColor(display->width() - 8, display->height() / 2);
    };
    const auto &palette = GyanisScribe::Gui::activePalette();
    EXPECT_EQ(fillAt(), palette.controlFill) << "静息态没有铺 controlFill";

    QMouseEvent press{QEvent::MouseButtonPress, QPointF{5, 5}, QPointF{5, 5}, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QCoreApplication::sendEvent(edit, &press);
    QCoreApplication::processEvents();
    EXPECT_EQ(fillAt(), palette.accentSoft) << "录制态没有铺 accentSoft";

    QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
    QCoreApplication::sendEvent(edit, &escape);
    QCoreApplication::processEvents();
    EXPECT_EQ(fillAt(), palette.controlFill) << "退出录制后没有回到 controlFill";

    edit->setEnabled(false);
    QCoreApplication::processEvents();
    EXPECT_EQ(fillAt(), palette.mutedFill) << "禁用态没有铺 mutedFill";

    edit->setEnabled(true);
    QCoreApplication::processEvents();
    EXPECT_EQ(fillAt(), palette.controlFill) << "恢复启用后没有回到 controlFill";
}

/**
 * @brief 钉住「不合格的组合被拒之后，显示回到绑定值」
 * @details 把被拒的那颗组合留在框里，看起来像是已经改成了它；下一次别人看到这一格会以为
 *          配置里就是那一颗。配置不许动，显示要回到现值。
 */
TEST_F(HotkeyEditFixture, RejectedCombinationRestoresTheBoundValue)
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

    // 全局热键不收裸键：这一颗一定被拒
    QKeyEvent bareKey{QEvent::KeyPress, Qt::Key_K, Qt::NoModifier};
    QCoreApplication::sendEvent(edit, &bareKey);
    QCoreApplication::processEvents();

    EXPECT_FALSE(display->property("recording").toBool()) << "被拒之后仍留在录制态";
    EXPECT_EQ(display->text(), bound) << "被拒的组合留在了框里，看起来像是改成功了";
    EXPECT_EQ(m_store->stringValue(key, QStringLiteral("missing")), bound) << "被拒的组合写进了配置";
}
