#include "Platform/UnsupportedHotkeyBackend.h"

namespace GyanisScribe::Platform
{

    UnsupportedHotkeyBackend::UnsupportedHotkeyBackend() = default;

    UnsupportedHotkeyBackend::~UnsupportedHotkeyBackend() = default;

    Core::Expected<void> UnsupportedHotkeyBackend::registerHotkey(int, Qt::KeyboardModifiers, Qt::Key)
    {
        return std::unexpected(Core::Error{Core::ErrorCode::PlatformUnsupported, "本平台没有全局热键能力：登记不会生效。请在程序窗口内使用普通快捷键，或等平台适配后再开启这一项"});
    }

    Core::Expected<void> UnsupportedHotkeyBackend::unregisterHotkey(int)
    {
        return std::unexpected(Core::Error{Core::ErrorCode::PlatformUnsupported, "本平台没有全局热键能力：没有任何登记需要注销"});
    }

    void UnsupportedHotkeyBackend::unregisterAll()
    {
        // 空实现：这份后端从不登记任何东西，没有需要清场的残留
    }

} // namespace GyanisScribe::Platform
