#include "Platform/IPlatformIntegration.h"
#include "Platform/UnsupportedHotkeyBackend.h"
#include "Platform/WindowGeometry.h"

#include <QApplication>
#include <QWindow>
#include <QWidget>

#include <gtest/gtest.h>

#ifdef Q_OS_WIN
#include <Windows.h>

#include "Platform/Windows/Win32Autostart.h"
#include "Platform/Windows/Win32HotkeyMapping.h"
#endif

#include <algorithm>
#include <chrono>
#include <thread>

using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Platform::createPlatformIntegration;
using GyanisScribe::Platform::ScreenWindow;
using GyanisScribe::Platform::UnsupportedHotkeyBackend;
#ifdef Q_OS_WIN
using GyanisScribe::Platform::autostartCommandLine;
#endif

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

/**
 * @brief 钉住「枚举窗口」的能力位与实际给果同解
 * @details 遮罩按这一位决定要不要给「悬停认出整扇窗口」。位与行为分家时，要么白少一项功能，
 *          要么放一条一按就报错的选项。非 Windows 的回绝文案必须指一条替代做法（直接框出来）。
 */
TEST(PlatformIntegration, WindowEnumerationCapabilityMatchesTheOutcome)
{
    const auto integration = createPlatformIntegration();
    ASSERT_NE(integration, nullptr);

    if (!integration->capabilities().windowEnumeration)
    {
        const auto windows = integration->screenWindows();
        ASSERT_FALSE(windows.has_value()) << "能力位说没有，却还是交出了一份候选";
        EXPECT_EQ(windows.error().code, ErrorCode::PlatformUnsupported);
        EXPECT_NE(QString::fromStdString(windows.error().message).indexOf(QStringLiteral("框")), -1) << "回绝时没指替代做法：实际是 " << windows.error().message;
        return;
    }

    const auto windows = integration->screenWindows();
    if (!windows)
    {
        EXPECT_NE(windows.error().code, ErrorCode::PlatformUnsupported) << "能力位说有，这一句却回绝：实际是 " << windows.error().message;
    }
}

/**
 * @brief 端到端钉住「枚举出来的窗口位置与 Qt 自己那一份读数是同一个点」
 * @details 两条独立来源对表：Qt 平台后端算的 frameGeometry()，与本仓「EnumWindows + DWM 可见边界 +
 *          按像素尺寸配对」算出的逻辑全局矩形。任一边改了取整、配对或过滤，这条就红。
 *          窗口是不是真的进了候选不能靠运气：用例自己有界地等它出现（最多两秒），等不到才判红。
 * @note 本机缩放是 100%，这一条钉住的是「枚举、过滤、配对与 DWM 边界」这一串；
 *       非 1 的缩放因子由 WindowGeometry 那组手算用例钉，两边各管一段，别拿这条的绿当那段的证据
 */
TEST(PlatformIntegration, EnumeratedWindowSitsWhereQtPutsIt)
{
    // argv 的缓冲要有静态存储期：Qt 只存指针不拷贝
    static int   argumentCount  = 1;
    static char  argumentZero[] = "TestPlatform";
    static char *arguments[]    = {argumentZero, nullptr};
    QApplication application{argumentCount, arguments};

    const auto integration = createPlatformIntegration();
    ASSERT_NE(integration, nullptr);
    if (!integration->capabilities().windowEnumeration)
    {
        GTEST_SKIP() << "本平台列不出窗口：这一格是降级出口，降级文案由上一条钉住";
    }

    QWidget probe;
    probe.setWindowTitle(QStringLiteral("智录窗口枚举探针"));
    probe.resize(260, 180);
    probe.show();
    // 句柄要在原生窗口真的建出来之后才拿得到：winId() 会把它同步建出来，
    // 在那之前 windowHandle()->handle() 是 0，比对出来的永远是「这扇窗不在候选里」
    const auto handle = static_cast<quintptr>(probe.winId());
    ASSERT_NE(handle, static_cast<quintptr>(0)) << "原生窗口句柄没建出来";

    std::vector<ScreenWindow> listed;
    const auto                deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    do
    {
        QCoreApplication::processEvents();
        const auto windows = integration->screenWindows();
        ASSERT_TRUE(windows.has_value()) << windows.error().message;
        listed = *windows;
        if (std::ranges::find(listed, handle, &ScreenWindow::handle) != listed.end())
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    } while (std::chrono::steady_clock::now() < deadline);

    const auto found = std::ranges::find(listed, handle, &ScreenWindow::handle);
    ASSERT_NE(found, listed.end()) << "探针窗口没出现在候选里：多半是可见性、工具窗或 DWM 隐藏那一串过滤挡掉的";

    // 互差一格之内，不是逐位相等：DWM 报的「可见边界」与 Qt 算的 frameGeometry 对边框的取整本来就差一格
    // （本机实测各边 ±1）。缩放或配对出错会是几十格，这条判据照样打得中
    const auto qtFrame = probe.frameGeometry();
    EXPECT_TRUE(found->logicalGlobal.adjusted(-1, -1, 1, 1).contains(qtFrame))
            << "枚举给的矩形包不住 Qt 那份：枚举 " << found->logicalGlobal.x() << ',' << found->logicalGlobal.y() << ' ' << found->logicalGlobal.width() << '×'
            << found->logicalGlobal.height() << "，Qt " << qtFrame.x() << ',' << qtFrame.y() << ' ' << qtFrame.width() << '×' << qtFrame.height();
    EXPECT_TRUE(qtFrame.adjusted(-1, -1, 1, 1).contains(found->logicalGlobal)) << "反过来也得分一结论：两条换算只改一边就是静默错位";
    EXPECT_EQ(found->title, probe.windowTitle()) << "标题是候选列表上唯一能认出窗口的东西";

    // 客户区是抓帧真正拿到的那一块：它必须比整窗小（差的是标题栏与边框），且与 Qt 自己那份客户区几何同系
    const auto qtClient = probe.geometry();
    ASSERT_FALSE(found->clientLogicalGlobal.isNull()) << "平台没给出客户区读数：窗口模式的光标高亮就只能继续不画";
    EXPECT_TRUE(found->clientLogicalGlobal.adjusted(-1, -1, 1, 1).contains(qtClient))
            << "客户区矩形包不住 Qt 那份：平台 " << found->clientLogicalGlobal.x() << ',' << found->clientLogicalGlobal.y() << ' ' << found->clientLogicalGlobal.width() << '×'
            << found->clientLogicalGlobal.height() << "，Qt " << qtClient.x() << ',' << qtClient.y() << ' ' << qtClient.width() << '×' << qtClient.height();
    EXPECT_TRUE(qtClient.adjusted(-1, -1, 1, 1).contains(found->clientLogicalGlobal)) << "互指要双向都成立，只补一套换算就是静默错位";
    EXPECT_LT(found->clientLogicalGlobal.height(), found->logicalGlobal.height()) << "客户区不比整窗矮，说明两份读数是同一个来源抄的，抓帧的原点会偏到标题栏上";
    EXPECT_GT(found->clientLogicalGlobal.height(), qtClient.height() - 2) << "客户区矮得离谱：多半把整窗矩形当成了客户区";
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

#ifdef Q_OS_WIN
/**
 * @brief 钉住开机启动项那条命令行的拼法：路径必须带引号
 * @details 装到 Program Files 这类带空格的目录下时，不加引号的路径会被 Windows 按空格切成两截，
 *          开机时起不来且不留任何提示；空路径要交回空串，由调用方去报「取不到自身路径」。
 */
TEST(Win32Autostart, QuotesTheExecutablePathForTheRunKey)
{
    const auto command = autostartCommandLine(QStringLiteral("C:\\Program Files\\GyanisScribe\\GyanisScribe.exe"));
    EXPECT_EQ(command, QStringLiteral("\"C:\\Program Files\\GyanisScribe\\GyanisScribe.exe\"")) << "带空格的路径必须整条被引号包住";

    EXPECT_TRUE(autostartCommandLine(QString{}).isEmpty()) << "取不到路径时交回空串，让调用方去报错";
    EXPECT_TRUE(autostartCommandLine(QStringLiteral("   ")).isEmpty()) << "纯空白不算路径";
}
#endif
