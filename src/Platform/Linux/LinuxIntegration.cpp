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
        // 全局热键没有跨桌面会话的统一登记口，桩后端如实回绝；穿透与「取前台窗口」同理
        // （后者要靠 X11 的 _NET_ACTIVE_WINDOW 或 Wayland 的会话协议，本仓没接）
        return PlatformCapabilities{
                .globalHotkeys   = false,
                .clickThrough    = false,
                .windowDiscovery = false,
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

} // namespace GyanisScribe::Platform
