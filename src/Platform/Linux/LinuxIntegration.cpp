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
        // 全局热键没有跨桌面会话的统一登记口，桩后端如实回绝；穿透、「取前台窗口」与「枚举可见窗口」同理
        // （后两者要靠 X11 的 _NET_ACTIVE_WINDOW / 窗口树查询，或 Wayland 的会话协议，本仓没接）
        return PlatformCapabilities{
                .globalHotkeys     = false,
                .clickThrough      = false,
                .windowDiscovery   = false,
                .windowEnumeration = false,
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

} // namespace GyanisScribe::Platform
