#include "Platform/IPlatformIntegration.h"
#include "Platform/UnsupportedHotkeyBackend.h"

#include <gtest/gtest.h>

#ifdef Q_OS_WIN
#include <Windows.h>

#include "Platform/Windows/Win32HotkeyMapping.h"
#endif

using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Platform::createPlatformIntegration;
using GyanisScribe::Platform::UnsupportedHotkeyBackend;

/**
 * @brief 钉住桩后端的拒绝契约：不支持就说「不支持」，不静默成功
 * @details 这一条在任何平台都跑，因为桩本身是平台中立的实现——Linux 用它，
 *          Windows 上也要能验证「能力位为假时后端确实回绝」。
 */
TEST(UnsupportedHotkeyBackend, RejectsEveryOperationWithActionableReason)
{
    UnsupportedHotkeyBackend backend;

    const auto registered = backend.registerHotkey(1, Qt::ControlModifier | Qt::AltModifier, Qt::Key_A);
    ASSERT_FALSE(registered.has_value()) << "桩后端把登记当成了成功，用户会以为热键已经生效";
    EXPECT_EQ(registered.error().code, ErrorCode::PlatformUnsupported);
    EXPECT_NE(registered.error().message.find("本平台没有全局热键能力"), std::string::npos) << "文案要写出原因与替代做法，实际是：" << registered.error().message;

    const auto unregistered = backend.unregisterHotkey(1);
    ASSERT_FALSE(unregistered.has_value()) << "从没登记过的东西不能报注销成功";
    EXPECT_EQ(unregistered.error().code, ErrorCode::PlatformUnsupported);

    backend.unregisterAll(); // 空实现，但不能抛也不能留下任何登记
}

/**
 * @brief 钉住能力表与后端行为同解
 * @details 判据是「表说没有 → 后端必须回绝」。表说有时本用例不主动登记：
 *          Windows 那份后端会真的占住用户机器上的组合键，实机触发留给验收那一轮逐条核对。
 */
TEST(PlatformIntegration, CapabilityTableMatchesProvidedBackend)
{
    const auto integration = createPlatformIntegration();
    ASSERT_NE(integration, nullptr);
    EXPECT_FALSE(integration->displayName().isEmpty()) << "平台名为空时关于页与降级提示都没东西可显示";

#ifdef Q_OS_WIN
    EXPECT_TRUE(integration->capabilities().globalHotkeys) << "Windows 上 RegisterHotKey 是给得出了，能力表不许写假";
#else
    EXPECT_FALSE(integration->capabilities().globalHotkeys) << "非 Windows 平台没有统一的全局热键登记口，能力表不许写假";
    const auto backend = integration->createHotkeyBackend();
    ASSERT_NE(backend, nullptr);
    const auto registered = backend->registerHotkey(1, Qt::ControlModifier, Qt::Key_A);
    ASSERT_FALSE(registered.has_value()) << "能力表说没有全局热键，后端却报登记成功";
    EXPECT_EQ(registered.error().code, ErrorCode::PlatformUnsupported);
#endif
}

/**
 * @brief 钉住「取前台窗口」的能力位与实际给果同解
 * @details 托盘菜单按这一位决定放不放「截取当前窗口」，位与行为分家时要么白少一项、要么放一条按了报错的项。
 *          Windows 上不锁死结果：有没有前台窗口取决于用户此刻点了什么，这里只钉「不许报 PlatformUnsupported」
 *          （那等于说这台机器做不出来，与能力位矛盾）；非 Windows 必须回绝且要给出替代做法。
 */
TEST(PlatformIntegration, WindowDiscoveryCapabilityMatchesTheOutcome)
{
    const auto integration = createPlatformIntegration();
    ASSERT_NE(integration, nullptr);

#ifdef Q_OS_WIN
    EXPECT_TRUE(integration->capabilities().windowDiscovery) << "Windows 上 GetForegroundWindow 是给得出了，能力表不许写假";
    const auto window = integration->foregroundWindow();
    if (!window)
    {
        EXPECT_NE(window.error().code, ErrorCode::PlatformUnsupported) << "能力位说有，这一句却回绝：实际是 " << window.error().message;
        EXPECT_FALSE(window.error().message.empty()) << "回绝时不许给空原因";
    } else
    {
        EXPECT_NE(window->handle, static_cast<quintptr>(0)) << "报成功却交了个空句柄";
    }
#else
    EXPECT_FALSE(integration->capabilities().windowDiscovery) << "本平台没有会话协议级的取窗口实现，能力表不许写假";
    const auto window = integration->foregroundWindow();
    ASSERT_FALSE(window.has_value()) << "能力位说没有，却还是交出了一个句柄";
    EXPECT_EQ(window.error().code, ErrorCode::PlatformUnsupported);
    EXPECT_NE(QString::fromStdString(window.error().message).indexOf(QStringLiteral("框选")), -1) << "回绝时没指替代做法：实际是 " << window.error().message;
#endif
}

#ifdef Q_OS_WIN
/**
 * @brief 钉住 Qt 取值到 Win32 取值域的转换与拒绝面
 */
TEST(Win32HotkeyMapping, ConvertsSupportedVocabulary)
{
    EXPECT_EQ(*GyanisScribe::Platform::toWin32ModifierMask(Qt::ControlModifier), static_cast<quint32>(MOD_CONTROL));
    EXPECT_EQ(*GyanisScribe::Platform::toWin32ModifierMask(Qt::AltModifier), static_cast<quint32>(MOD_ALT));
    EXPECT_EQ(*GyanisScribe::Platform::toWin32ModifierMask(Qt::ShiftModifier), static_cast<quint32>(MOD_SHIFT));
    EXPECT_EQ(*GyanisScribe::Platform::toWin32ModifierMask(Qt::MetaModifier), static_cast<quint32>(MOD_WIN));
    EXPECT_EQ(*GyanisScribe::Platform::toWin32ModifierMask(Qt::ControlModifier | Qt::AltModifier), static_cast<quint32>(MOD_CONTROL | MOD_ALT))
            << "两个修饰键并起来应当各占一位，少一位就注册成了别的组合";
    EXPECT_EQ(*GyanisScribe::Platform::toWin32ModifierMask(Qt::NoModifier), 0U) << "空集交回 0，缺修饰键那条由 Hotkey 层判";

    EXPECT_EQ(*GyanisScribe::Platform::toWin32VirtualKey(Qt::Key_A), static_cast<quint32>(0x41));
    EXPECT_EQ(*GyanisScribe::Platform::toWin32VirtualKey(Qt::Key_Z), static_cast<quint32>(0x5A));
    EXPECT_EQ(*GyanisScribe::Platform::toWin32VirtualKey(Qt::Key_0), static_cast<quint32>(0x30));
    EXPECT_EQ(*GyanisScribe::Platform::toWin32VirtualKey(Qt::Key_9), static_cast<quint32>(0x39));
    EXPECT_EQ(*GyanisScribe::Platform::toWin32VirtualKey(Qt::Key_Space), static_cast<quint32>(VK_SPACE));
    EXPECT_EQ(*GyanisScribe::Platform::toWin32VirtualKey(Qt::Key_F1), static_cast<quint32>(VK_F1));
    EXPECT_EQ(*GyanisScribe::Platform::toWin32VirtualKey(Qt::Key_F24), static_cast<quint32>(VK_F1 + 23)) << "功能键段步长不对，F24 会撞到别的键";

    // 拒绝面：键盘区修饰键与不在允许段的键都要回绝，不许就近取一个
    const auto unsupportedModifier = GyanisScribe::Platform::toWin32ModifierMask(Qt::KeypadModifier);
    ASSERT_FALSE(unsupportedModifier.has_value()) << "Keypad 在 RegisterHotKey 里没有对应位，静默丢掉会注册出另一个组合";
    EXPECT_EQ(unsupportedModifier.error().code, ErrorCode::InvalidArgument);

    const auto unsupportedKey = GyanisScribe::Platform::toWin32VirtualKey(Qt::Key_Question);
    ASSERT_FALSE(unsupportedKey.has_value()) << "符号键随键盘布局变位，不能当全局热键";
    EXPECT_EQ(unsupportedKey.error().code, ErrorCode::InvalidArgument);
}
#endif
