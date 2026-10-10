#include "Platform/Windows/WindowsIntegration.h"

#include "Platform/Windows/Win32HotkeyBackend.h"

#include "Core/Text.h"

#include <windows.h>

#include <memory>

namespace GyanisScribe::Platform
{

    WindowsIntegration::WindowsIntegration() = default;

    WindowsIntegration::~WindowsIntegration() = default;

    PlatformCapabilities WindowsIntegration::capabilities() const noexcept
    {
        // 三项都按操作系统给得出的能力填：RegisterHotKey、WS_EX_TRANSPARENT、GetForegroundWindow。
        // 录屏采集、系统声音与系统 OCR 不在这张表里占位：那三步的可用性由各自模块在调用点现探
        // （采集探屏幕、编码看 ffmpeg 在不在、识别问系统装了哪几支语言包），静态位只会说假话
        return PlatformCapabilities{
                .globalHotkeys   = true,
                .clickThrough    = true,
                .windowDiscovery = true,
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

    Core::Expected<ForegroundWindow> WindowsIntegration::foregroundWindow() const
    {
        auto *const window = GetForegroundWindow();
        if (window == nullptr)
        {
            // 会话锁屏、刚开机还没人点过窗口都会走到这里：报「先点一下」比报「不支持」更可操作
            return std::unexpected(
                    Core::Error{Core::ErrorCode::ScreenUnavailable,
                                GyanisScribe::Core::uiTextUtf8("现在没有一个在前台的窗口可截（可能锁了屏或焦点在桌面上）。请先点一下要截的那个窗口，再回来用这一项")});
        }

        ForegroundWindow info;
        info.handle = reinterpret_cast<quintptr>(window);

        // 标题拿不到不算失败：有些窗口（如部分控制台与工具窗）就是不给标题
        constexpr int kWindowTitleBufferLength              = 512;
        wchar_t       titleBuffer[kWindowTitleBufferLength] = {};
        const int     written                               = GetWindowTextW(window, titleBuffer, kWindowTitleBufferLength);
        info.title                                          = written > 0 ? QString::fromWCharArray(titleBuffer, written) : QString();
        return info;
    }

} // namespace GyanisScribe::Platform
