#include "Platform/Windows/Win32HotkeyBackend.h"

#include "Core/Logger.h"
#include "Core/Text.h"
#include "Platform/Windows/Win32HotkeyMapping.h"

#include <QCoreApplication>
#include <QWindow>

#include <Windows.h>

#include <format>

namespace GyanisScribe::Platform
{

    Win32HotkeyBackend::Win32HotkeyBackend()
    {
        // 隐藏窗口只为领一个 HWND：不 show、不接受焦点，托盘模式下也不会有任何可见东西
        m_sinkWindow = std::make_unique<QWindow>();
        m_sinkWindow->setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        m_sinkWindow->setTitle(QStringLiteral("GyanisScribeHotkeySink"));

        // winId() 会真的把平台窗口建出来，拿不到 HWND 时后续每一次登记都会在同一处失败，先记一条
        const WId windowHandle = m_sinkWindow->winId();
        if (windowHandle == 0)
        {
            Core::Logger::error("全局热键后端启动失败：隐藏窗口拿不到 HWND，热键登记将全部无效");
        }

        QCoreApplication::instance()->installNativeEventFilter(this);
    }

    Win32HotkeyBackend::~Win32HotkeyBackend()
    {
        // 顺序固定：先摘过滤器，避免注销过程中又回调进来；再注销热键，避免残留的组合键继续被占用。
        // 应用对象已析构（instance() 为空）时不必摘：分发器已经不在了，过滤器随进程一并失效
        if (auto *application = QCoreApplication::instance(); application != nullptr)
        {
            application->removeNativeEventFilter(this);
        }
        unregisterAll();
    }

    Core::Expected<void> Win32HotkeyBackend::registerHotkey(int identifier, Qt::KeyboardModifiers modifiers, Qt::Key key)
    {
        // 取值转换排在最前：出不了 Win32 词表的组合不该去占编号，也不该被翻译成相近的键顶上
        const auto modifierMask = toWin32ModifierMask(modifiers);
        if (!modifierMask)
        {
            return std::unexpected(modifierMask.error());
        }
        const auto virtualKey = toWin32VirtualKey(key);
        if (!virtualKey)
        {
            return std::unexpected(virtualKey.error());
        }

        if (m_registeredIdentifiers.find(identifier) != m_registeredIdentifiers.end())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("热键编号 {} 在本进程内已经登记过。请换一个编号，或先注销那一条"), identifier)});
        }

        const auto windowHandle = reinterpret_cast<HWND>(m_sinkWindow->winId());
        if (!RegisterHotKey(windowHandle, identifier, *modifierMask, *virtualKey))
        {
            const DWORD lastError = GetLastError();
            // ERROR_HOTKEY_ALREADY_REGISTERED 是这一条最常走到的分支，直说「被占用」比报错误码有用
            return std::unexpected(Core::Error{
                    Core::ErrorCode::Busy,
                    GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("全局热键登记失败（系统错误码 {}）：这个组合键多半已被其它程序占用。请在设置里换一个组合，或先退出占用它的程序"),
                                lastError)});
        }

        m_registeredIdentifiers.insert(identifier);
        return {};
    }

    Core::Expected<void> Win32HotkeyBackend::unregisterHotkey(int identifier)
    {
        const auto found = m_registeredIdentifiers.find(identifier);
        if (found == m_registeredIdentifiers.end())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("热键编号 {} 没有登记过，无需注销"), identifier)});
        }

        const auto windowHandle = reinterpret_cast<HWND>(m_sinkWindow->winId());
        if (!UnregisterHotKey(windowHandle, identifier))
        {
            return std::unexpected(Core::Error{
                    Core::ErrorCode::Busy, GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("热键注销失败（系统错误码 {}）：编号 {}。请重试或重启本程序"), GetLastError(), identifier)});
        }

        m_registeredIdentifiers.erase(found);
        return {};
    }

    void Win32HotkeyBackend::unregisterAll()
    {
        // 复制一份编号再清：注销会动到集合本身，边遍历边删会失效
        auto pending = m_registeredIdentifiers;
        for (const auto identifier: pending)
        {
            if (const auto result = unregisterHotkey(identifier); !result)
            {
                // 注销不掉也要继续清其余的，残留一条就等于一直占着用户的组合键
                Core::Logger::warning(std::format("退出时热键注销未成功：{}", result.error().message));
            }
        }
    }

    bool Win32HotkeyBackend::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
    {
        if (eventType != "windows_generic_MSG" || message == nullptr)
        {
            return false;
        }

        const auto *windowsMessage = static_cast<const MSG *>(message);
        if (windowsMessage->message != WM_HOTKEY)
        {
            return false;
        }

        const auto identifier = static_cast<int>(windowsMessage->wParam);
        if (m_registeredIdentifiers.find(identifier) == m_registeredIdentifiers.end())
        {
            // 编号不在册：别的应用的同类消息，不能替它消费
            return false;
        }

        emit activated(identifier);
        if (result != nullptr)
        {
            *result = 0;
        }
        return true;
    }

} // namespace GyanisScribe::Platform
