#include "Platform/Windows/WindowsStepEventSource.h"

#include "Base/Logger.h"
#include "Base/Text.h"

#include <QCursor>
#include <QDateTime>
#include <QMetaObject>

#include <cstdlib>
#include <format>

namespace
{
    /// 按下与抬起之间允许的最大曼哈顿距离（逻辑像素）：再远就是拖动，不是一次点击
    constexpr int kMaximumClickTravel = 6;
} // namespace

namespace GyanisScribe::Platform
{
    WindowsStepEventSource *WindowsStepEventSource::s_mouseOwner  = nullptr;
    WindowsStepEventSource *WindowsStepEventSource::s_windowOwner = nullptr;

    WindowsStepEventSource::~WindowsStepEventSource()
    {
        // 钩子不能活过本对象：回调里存着指向 this 的归属指针
        stop();
    }

    Base::Expected<void> WindowsStepEventSource::start()
    {
        // 重复调用按幂等处理：钩子已经装上，再装一份只会收到重复事件
        if (m_running)
        {
            return {};
        }

        // 回调只认一个归属实例：同一线程里已有别人在记录时当场拒绝，免得两边各收一半事件
        if (s_mouseOwner != nullptr || s_windowOwner != nullptr)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::Busy, GyanisScribe::Base::uiTextUtf8("本进程已经在记录操作步骤：请先停止上一次记录，再开始新的记录")});
        }

        s_mouseOwner  = this;
        s_windowOwner = this;

        m_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, &WindowsStepEventSource::mouseProc, GetModuleHandleW(nullptr), 0);
        // WINEVENT_SKIPOWNPROCESS 与回调里的进程判断互为兜底：前者挡系统层的通知，后者挡鼠标钩子里的点击
        m_winEventHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, &WindowsStepEventSource::winEventProc, 0, 0,
                                         WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

        if (m_mouseHook == nullptr || m_winEventHook == nullptr)
        {
            // 任一失败都要把另一份也摘掉：半套钩子会让「正在记录」悄悄只记一半
            stop();
            return std::unexpected(Base::Error{Base::ErrorCode::InternalError, GyanisScribe::Base::uiTextUtf8("系统没让装上全局输入监听：请检查安全软件或换一个桌面会话再试")});
        }

        m_running = true;
        return {};
    }

    void WindowsStepEventSource::stop()
    {
        if (m_mouseHook != nullptr)
        {
            if (UnhookWindowsHookEx(m_mouseHook) == 0)
            {
                // 摘不掉也要继续清状态：残留的句柄值比「以为还在监听」更危险
                Base::Logger::warning(std::format("摘除全局鼠标钩子失败：系统错误码 {}", GetLastError()));
            }
            m_mouseHook = nullptr;
        }

        if (m_winEventHook != nullptr)
        {
            if (UnhookWinEvent(m_winEventHook) == 0)
            {
                Base::Logger::warning(std::format("摘除前台窗口事件钩子失败：系统错误码 {}", GetLastError()));
            }
            m_winEventHook = nullptr;
        }

        if (s_mouseOwner == this)
        {
            s_mouseOwner = nullptr;
        }
        if (s_windowOwner == this)
        {
            s_windowOwner = nullptr;
        }

        m_running      = false;
        m_pressedValid = false;
    }

    bool WindowsStepEventSource::isRunning() const noexcept
    {
        return m_running;
    }

    LRESULT CALLBACK WindowsStepEventSource::mouseProc(int nCode, WPARAM wParam, LPARAM lParam)
    {
        // 钩子链的约定：负码表示系统要求原样放行，此时 lParam 也不能读
        if (nCode != HC_ACTION)
        {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        auto *const       owner = s_mouseOwner;
        const auto *const info  = reinterpret_cast<const MSLLHOOKSTRUCT *>(lParam);
        if (owner == nullptr || info == nullptr)
        {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        const bool isPress = wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN;

        // 合成事件（自动化工具、远控回放）不是用户点出来的，记进步骤只会误导；
        // 低完整性级别的注入同样不是用户的操作
        if ((info->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED)) != 0)
        {
            if (isPress)
            {
                // 按下被挡掉时清掉等待配对的状态：否则后面的抬起会配到这个点之前那一次旧按下
                owner->m_pressedValid = false;
            }
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        // 落在自家窗口上的点击要跳过：记录面板、审核窗与主窗都是本程序自己在响应用户
        const HWND target    = WindowFromPoint(info->pt);
        DWORD      processId = 0;
        GetWindowThreadProcessId(target, &processId);
        if (processId == GetCurrentProcessId())
        {
            if (isPress)
            {
                // 同注入事件：被挡掉的按下不能留下一个可被别处抬起配对成功的旧点
                owner->m_pressedValid = false;
            }
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        if (isPress)
        {
            // 逻辑坐标在按下的这一刻取：钩子给的是物理屏幕坐标，Qt 的坐标域才是消费方要的
            owner->m_pressedPoint = QCursor::pos();
            owner->m_pressedRight = wParam == WM_RBUTTONDOWN;
            owner->m_pressedValid = true;
        } else if (wParam == WM_LBUTTONUP || wParam == WM_RBUTTONUP)
        {
            const bool releasedRight = wParam == WM_RBUTTONUP;
            const bool sameButton    = releasedRight == owner->m_pressedRight;
            if (owner->m_pressedValid && sameButton)
            {
                const QPoint releasedPoint = QCursor::pos();
                const QPoint travel        = releasedPoint - owner->m_pressedPoint;
                if (std::abs(travel.x()) + std::abs(travel.y()) <= kMaximumClickTravel)
                {
                    const StepInputEvent inputEvent{StepInputEvent::Kind::MouseClick, releasedPoint, reinterpret_cast<quintptr>(target), releasedRight,
                                                    QDateTime::currentDateTime()};
                    // 回调跑在系统的钩子链上：只排队，抓屏与 UI Automation 留给 GUI 线程的主循环
                    QMetaObject::invokeMethod(owner, [owner, inputEvent] { emit owner->stepEvent(inputEvent); }, Qt::QueuedConnection);
                }
            }
            owner->m_pressedValid = false;
        }

        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    void CALLBACK WindowsStepEventSource::winEventProc(HWINEVENTHOOK, DWORD event, HWND window, LONG, LONG, DWORD, DWORD)
    {
        if (event != EVENT_SYSTEM_FOREGROUND || window == nullptr)
        {
            return;
        }

        auto *const owner = s_windowOwner;
        if (owner == nullptr)
        {
            return;
        }

        // 自家窗口成为前台（记录面板、HUD）不算用户换了应用
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId == GetCurrentProcessId())
        {
            return;
        }

        // 窗口切换没有「点击点」：坐标只作占位，消费方按 windowHandle 去找窗口
        const StepInputEvent inputEvent{StepInputEvent::Kind::ForegroundWindowChanged, QCursor::pos(), reinterpret_cast<quintptr>(window), false, QDateTime::currentDateTime()};
        QMetaObject::invokeMethod(owner, [owner, inputEvent] { emit owner->stepEvent(inputEvent); }, Qt::QueuedConnection);
    }

} // namespace GyanisScribe::Platform
