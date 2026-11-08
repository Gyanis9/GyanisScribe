#include "Platform/Windows/WindowsIntegration.h"

#ifdef Q_OS_WIN
#include <QSignalSpy>

#include <gtest/gtest.h>

#include <windows.h>
#include <wtsapi32.h>

using GyanisScribe::Platform::WindowsIntegration;

/**
 * @brief 钉住会话消息与两条信号之间的接线：锁屏发 sessionLocked，解锁发 sessionUnlocked
 * @details 用例直接向消息专用窗口投递 WM_WTSSESSION_CHANGE，不要求测试机真锁一次屏；
 *          同时钉住能力位与窗口同解：窗口在时能力位必须为真，窗口建不出来时必须为假（此时判 SKIP）。
 */
TEST(SessionNotification, LockAndUnlockEmitSignals)
{
    WindowsIntegration integration;
    if (integration.sessionWindowHandleForTesting() == 0)
    {
        EXPECT_FALSE(integration.capabilities().sessionNotifications) << "会话通知窗口没建出来，能力位却说是真";
        GTEST_SKIP() << "本机没有建出会话通知窗口";
    }
    EXPECT_TRUE(integration.capabilities().sessionNotifications) << "会话通知窗口与 WTS 登记都成了，能力位却是假";

    QSignalSpy locked(&integration, &WindowsIntegration::sessionLocked);
    QSignalSpy unlocked(&integration, &WindowsIntegration::sessionUnlocked);

    SendMessageW(reinterpret_cast<HWND>(integration.sessionWindowHandleForTesting()), WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, 0);
    SendMessageW(reinterpret_cast<HWND>(integration.sessionWindowHandleForTesting()), WM_WTSSESSION_CHANGE, WTS_SESSION_UNLOCK, 0);

    EXPECT_EQ(locked.count(), 1);
    EXPECT_EQ(unlocked.count(), 1);
}
#endif
