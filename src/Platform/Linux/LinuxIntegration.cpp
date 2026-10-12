#include "Platform/Linux/LinuxIntegration.h"

#include "Core/Text.h"
#include "Platform/UnsupportedHotkeyBackend.h"

#include <memory>

namespace GyanisScribe::Platform
{

    LinuxIntegration::LinuxIntegration() = default;

    LinuxIntegration::~LinuxIntegration() = default;

    PlatformCapabilities LinuxIntegration::capabilities() const noexcept
    {
        // 全局热键没有跨桌面会话的统一登记口，桩后端如实回绝；穿透、「取前台窗口」、「枚举可见窗口」
        // 与「界面元素检测」同理（前两者要靠 X11 的 _NET_ACTIVE_WINDOW / 窗口树查询或 Wayland 的会话协议，
        // 元素检测要靠系统无障碍树，本仓都没接）
        return PlatformCapabilities{
                .globalHotkeys     = false,
                .clickThrough      = false,
                .windowDiscovery   = false,
                .windowEnumeration = false,
                .uiElements        = false,
        };
    }

    QString LinuxIntegration::displayName() const
    {
        return GyanisScribe::Core::uiText("Linux（降级实现）");
    }

    std::unique_ptr<IHotkeyBackend> LinuxIntegration::createHotkeyBackend()
    {
        return std::make_unique<UnsupportedHotkeyBackend>();
    }

    Core::Expected<ForegroundWindow> LinuxIntegration::foregroundWindow() const
    {
        // 不是「忘了实现」而是这套会话协议本身分叉：X11 读 _NET_ACTIVE_WINDOW，Wayland 根本不允许
        // 客户端问别的窗口。所以给不出句柄时把替代做法一并说清，别让托盘项变成按了没反应
        return std::unexpected(
                Core::Error{Core::ErrorCode::PlatformUnsupported,
                            GyanisScribe::Core::uiTextUtf8("本平台取不出当前前台窗口（X11 与 Wayland 各有各的会话协议，本仓没接）。请在托盘改用「框选截图」把那个窗口框出来")});
    }

    Core::Expected<bool> LinuxIntegration::autostartEnabled() const
    {
        // 与开关同源：不假装「没开」，也不假装「开了」，两种都按没有这一能力回绝
        return std::unexpected(
                Core::Error{Core::ErrorCode::PlatformUnsupported,
                            GyanisScribe::Core::uiTextUtf8("本平台没有接开机启动（X11 与 Wayland 的会话各自管启动项，本仓没接）。可以把本程序自己加进桌面环境的「启动应用程序」")});
    }

    Core::Expected<void> LinuxIntegration::setAutostart(const bool enabled)
    {
        static_cast<void>(enabled);

        return std::unexpected(
                Core::Error{Core::ErrorCode::PlatformUnsupported,
                            GyanisScribe::Core::uiTextUtf8("本平台没有接开机启动（X11 与 Wayland 的会话各自管启动项，本仓没接）。可以把本程序自己加进桌面环境的「启动应用程序」")});
    }

    Core::Expected<std::vector<ScreenWindow>> LinuxIntegration::screenWindows() const
    {
        // 与取前台窗口同源：X11 要查窗口树并把每扇窗的客户端坐标换算到全局，Wayland 干脆不让客户端问别的窗口。
        // 这里不硬造一份「只有当前窗口」的候选，因为那会让遮罩的悬停识别在两个会话下给出不同形状的结果
        return std::unexpected(
                Core::Error{Core::ErrorCode::PlatformUnsupported,
                            GyanisScribe::Core::uiTextUtf8("本平台列不出屏上的窗口，遮罩上不认「悬停选中整扇窗口」。请直接把要的那一块框出来，或按一次截图键取整屏")});
    }

    Core::Expected<std::vector<QRect>> LinuxIntegration::elementChainAtPoint(const QPoint &globalLogicalPoint, const std::vector<quintptr> &excludeWindowHandles) const
    {
        static_cast<void>(globalLogicalPoint);
        static_cast<void>(excludeWindowHandles);

        // 这套能力要问系统的无障碍树，两个会话都没有统一可问的对等物：不硬造一份「整扇窗口」冒充元素，
        // 那会让遮罩在 Linux 上多出一档按了没差别的层级切换
        return std::unexpected(Core::Error{
                Core::ErrorCode::PlatformUnsupported,
                GyanisScribe::Core::uiTextUtf8(
                        "本平台认不出界面元素（要系统的无障碍树，X11 与 Wayland 都没有统一可问的入口，本仓没接）。遮罩上悬停仍会认整扇窗口，也可以直接框住要的那一块")});
    }

} // namespace GyanisScribe::Platform
