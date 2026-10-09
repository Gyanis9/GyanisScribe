#include "Platform/Windows/WindowsIntegration.h"

#include "Platform/Windows/Win32HotkeyBackend.h"

#include <memory>

namespace GyanisScribe::Platform
{

    WindowsIntegration::WindowsIntegration() = default;

    WindowsIntegration::~WindowsIntegration() = default;

    PlatformCapabilities WindowsIntegration::capabilities() const noexcept
    {
        // 五项都按操作系统能给的能力填：RegisterHotKey、WS_EX_TRANSPARENT、桌面重复/WGC、WASAPI loopback、
        // WinRT OCR。哪一项的本仓实现还没落地，由那一个模块在调用点报「尚未适配」
        return PlatformCapabilities{
                .globalHotkeys   = true,
                .clickThrough    = true,
                .screenRecording = true,
                .audioLoopback   = true,
                .systemOcr       = true,
        };
    }

    QString WindowsIntegration::displayName() const
    {
        return QStringLiteral("Windows");
    }

    std::unique_ptr<IHotkeyBackend> WindowsIntegration::createHotkeyBackend()
    {
        return std::make_unique<Win32HotkeyBackend>();
    }

} // namespace GyanisScribe::Platform
