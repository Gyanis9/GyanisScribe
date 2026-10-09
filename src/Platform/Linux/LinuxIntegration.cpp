#include "Platform/Linux/LinuxIntegration.h"

#include "Platform/UnsupportedHotkeyBackend.h"

#include <memory>

namespace GyanisScribe::Platform
{

    LinuxIntegration::LinuxIntegration() = default;

    LinuxIntegration::~LinuxIntegration() = default;

    PlatformCapabilities LinuxIntegration::capabilities() const noexcept
    {
        // 全局热键没有跨桌面会话的统一登记口，桩后端如实回绝；穿透、loopback 与系统 OCR 同理
        return PlatformCapabilities{
                .globalHotkeys   = false,
                .clickThrough    = false,
                .screenRecording = true,
                .audioLoopback   = false,
                .systemOcr       = false,
        };
    }

    QString LinuxIntegration::displayName() const
    {
        return QStringLiteral("Linux（降级实现）");
    }

    std::unique_ptr<IHotkeyBackend> LinuxIntegration::createHotkeyBackend()
    {
        return std::make_unique<UnsupportedHotkeyBackend>();
    }

} // namespace GyanisScribe::Platform
