#include "Platform/Windows/Win32HotkeyMapping.h"

#include "Core/Text.h"

#include <Windows.h>

#include <format>

namespace GyanisScribe::Platform
{

    Core::Expected<quint32> toWin32ModifierMask(Qt::KeyboardModifiers modifiers)
    {
        // 逐个判定而不是整体比对：一次只丢一个位的话，调用方看不出少的是哪一个
        quint32 mask = 0;

        if (modifiers.testFlag(Qt::ControlModifier))
        {
            mask |= MOD_CONTROL;
        }
        if (modifiers.testFlag(Qt::AltModifier))
        {
            mask |= MOD_ALT;
        }
        if (modifiers.testFlag(Qt::ShiftModifier))
        {
            mask |= MOD_SHIFT;
        }
        if (modifiers.testFlag(Qt::MetaModifier))
        {
            // Qt 的 Meta 在 Windows 上就是 Win 键
            mask |= MOD_WIN;
        }

        // Qt 里剩下的那些位（键盘区、组切换、状态开关）在 Win32 热键里没有对应物，
        // 把它们悄悄当成没写就等于注册出一个和界面上不一样的组合
        const Qt::KeyboardModifiers unsupported = modifiers & ~(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
        if (unsupported != Qt::NoModifier)
        {
            return std::unexpected(Core::Error{
                    Core::ErrorCode::InvalidArgument,
                    GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("修饰键无法用于全局热键：掩码 0x{:X}。请只用 Ctrl、Alt、Shift、Win 四种组合"), static_cast<unsigned>(unsupported))});
        }
        return mask;
    }

    Core::Expected<quint32> toWin32VirtualKey(Qt::Key key)
    {
        const auto code = static_cast<int>(key);

        // Qt 对字母与大写字母表点位（Key_A == 'A'）与 Win32 的 VK_A 段完全重合，可以直接透传
        if (code >= static_cast<int>(Qt::Key_A) && code <= static_cast<int>(Qt::Key_Z))
        {
            return static_cast<quint32>(code);
        }
        if (code >= static_cast<int>(Qt::Key_0) && code <= static_cast<int>(Qt::Key_9))
        {
            return static_cast<quint32>(code);
        }
        if (code == static_cast<int>(Qt::Key_Space))
        {
            return VK_SPACE;
        }
        // F1 在 Qt 里从 0x01000030 起排，Win32 从 0x70 起排，两者步长都是 1
        if (code >= static_cast<int>(Qt::Key_F1) && code <= static_cast<int>(Qt::Key_F24))
        {
            return static_cast<quint32>(VK_F1 + (code - static_cast<int>(Qt::Key_F1)));
        }

        return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                           GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("按键 0x{:X} 不能用作全局热键。请改用字母、数字、空格或 F1-F24 之一"), code)});
    }

} // namespace GyanisScribe::Platform
