#include "Platform/Windows/WindowsIntegration.h"

#include "Platform/WindowGeometry.h"
#include "Platform/Windows/Win32HotkeyBackend.h"

#include "Core/Logger.h"
#include "Core/Text.h"

#include <windows.h>

#include <dwmapi.h>

#include <QGuiApplication>
#include <QScreen>
#include <QSize>
#include <QStringList>

#include <cmath>
#include <format>
#include <memory>
#include <vector>

namespace
{
    using GyanisScribe::Core::Error;
    using GyanisScribe::Core::ErrorCode;
    using GyanisScribe::Platform::MonitorMapping;
    using GyanisScribe::Platform::ScreenWindow;

    /// EnumWindows 回调收下来的一份原始读数：坐标还是平台给的全局物理像素
    struct RawWindow
    {
        HWND    handle;         ///< 窗口句柄
        QString title;          ///< 标题栏文字
        QRect   physicalGlobal; ///< 全局物理矩形
    };

    /// EnumDisplayMonitors 回调收下来的一块屏：只有平台那一份读数
    struct RawMonitor
    {
        QString name;     ///< 设备名，与 QScreen::name() 同一个口径
        QRect   physical; ///< 全局物理矩形
    };

    BOOL CALLBACK collectVisibleWindow(const HWND window, const LPARAM parameter)
    {
        auto *const collected = reinterpret_cast<std::vector<RawWindow> *>(parameter);
        if (!IsWindowVisible(window))
        {
            return TRUE;
        }

        // 工具窗不进候选：下拉列表的临时层、缩放提示那一类只在被点着的一瞬存在，
        // 把它们当「要截的那一块」会把用户带到一个根本截不动的目标上
        if ((GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0)
        {
            return TRUE;
        }

        // 被 DWM 藏起来的窗口（切到别的虚拟桌面那种）还躺在原坐标上，留着会得到一个看不见却能被选中的矩形
        DWORD cloaked = 0;
        if (DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)) == S_OK && cloaked != 0)
        {
            return TRUE;
        }

        // 优先取 DWM 报的实际可见边界：GetWindowRect 那一份带着几条看不见的可调边框，
        // 拿它画高亮框会比窗口本身宽出几格
        RECT frame{};
        if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))) && FAILED(GetWindowRect(window, &frame)))
        {
            return TRUE;
        }
        const QRect physical{frame.left, frame.top, frame.right - frame.left, frame.bottom - frame.top};
        if (physical.isEmpty())
        {
            return TRUE;
        }

        constexpr int kWindowTitleBufferLength              = 512;
        wchar_t       titleBuffer[kWindowTitleBufferLength] = {};
        const auto    written                               = GetWindowTextW(window, titleBuffer, kWindowTitleBufferLength);
        collected->push_back(RawWindow{window, written > 0 ? QString::fromWCharArray(titleBuffer, written) : QString(), physical});
        return TRUE;
    }

    BOOL CALLBACK collectMonitor(const HMONITOR monitor, const HDC, const LPRECT, const LPARAM parameter)
    {
        auto *const collected = reinterpret_cast<std::vector<RawMonitor> *>(parameter);
        // 设备名只在 MONITORINFOEX 里（MONITORINFO 只有矩形与工作区），而它的头几个字段与 MONITORINFO 同形，
        // 所以照旧交给 GetMonitorInfoW，只是缓冲区换成 EX 那一份
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info) != 0)
        {
            const auto &area = info.rcMonitor;
            collected->push_back(RawMonitor{QString::fromWCharArray(info.szDevice), QRect{area.left, area.top, area.right - area.left, area.bottom - area.top}});
        }
        return TRUE;
    }

    /**
     * @brief 把平台的屏清单与 Qt 的屏清单配成对
     * @return 每块屏一份「物理读数 + 逻辑读数 + 缩放因子」
     * @details 配对键是「逻辑尺寸乘以该屏缩放因子 = 平台给的那块屏的像素尺寸」：Qt 6.11 的
     *          QScreen::name() 交回来的是显示器型号（本机实测 H27T22S），而平台给的是 \\.\DISPLAY1，
     *          两个口径靠名字会一块都配不上。同尺寸的多块屏按各自枚举顺序先到先得，
     *          配不上的那块宁可跳过（它上面的窗口进不了候选）也不拿别块的缩放因子硬算——
     *          错算出来的矩形会落在另一块屏上。
     */
    std::vector<MonitorMapping> monitorMappings()
    {
        std::vector<RawMonitor> platformMonitors;
        EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&platformMonitors));

        const auto                  screens = QGuiApplication::screens();
        std::vector<bool>           claimed(platformMonitors.size(), false);
        std::vector<MonitorMapping> mappings;
        for (auto *screen: screens)
        {
            const auto ratio = screen->devicePixelRatio();
            if (ratio <= 0.0)
            {
                continue;
            }
            const QSize expected{static_cast<int>(std::llround(static_cast<double>(screen->geometry().width()) * ratio)),
                                 static_cast<int>(std::llround(static_cast<double>(screen->geometry().height()) * ratio))};
            for (std::size_t index = 0; index < platformMonitors.size(); ++index)
            {
                if (claimed[index] || platformMonitors[index].physical.size() != expected)
                {
                    continue;
                }
                claimed[index] = true;
                mappings.push_back(MonitorMapping{platformMonitors[index].name, platformMonitors[index].physical, screen->geometry(), ratio});
                break;
            }
        }

        // 配不上对要当场留下两份读数：这是「窗口候选为空」的唯一成因，而用户屏幕上只有一句「重开一次」，
        // 没有这行日志谁也说不清是哪一侧的读数改了口径
        if (mappings.size() != static_cast<std::size_t>(screens.size()))
        {
            QStringList platformNames;
            for (const auto &monitor: platformMonitors)
            {
                platformNames << QStringLiteral("%1 %2×%3").arg(monitor.name).arg(monitor.physical.width()).arg(monitor.physical.height());
            }
            QStringList screenNames;
            for (auto *screen: screens)
            {
                screenNames << QStringLiteral("%1 逻辑%2×%3 缩放%4")
                                       .arg(screen->name())
                                       .arg(screen->geometry().width())
                                       .arg(screen->geometry().height())
                                       .arg(screen->devicePixelRatio());
            }
            GyanisScribe::Core::Logger::warning(std::format("屏幕配对不完整：配上了 {} 块，平台给了 [{}]，界面给了 [{}]", mappings.size(),
                                                            platformNames.join(QStringLiteral(", ")).toStdString(), screenNames.join(QStringLiteral(", ")).toStdString()));
        }
        return mappings;
    }
} // namespace

namespace GyanisScribe::Platform
{

    WindowsIntegration::WindowsIntegration() = default;

    WindowsIntegration::~WindowsIntegration() = default;

    PlatformCapabilities WindowsIntegration::capabilities() const noexcept
    {
        // 四项都按操作系统给得出的能力填：RegisterHotKey、WS_EX_TRANSPARENT、GetForegroundWindow、EnumWindows。
        // 录屏采集、系统声音与系统 OCR 不在这张表里占位：那三步的可用性由各自模块在调用点现探
        // （采集探屏幕、编码看 ffmpeg 在不在、识别问系统装了哪几支语言包），静态位只会说假话
        return PlatformCapabilities{
                .globalHotkeys     = true,
                .clickThrough      = true,
                .windowDiscovery   = true,
                .windowEnumeration = true,
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

    Core::Expected<std::vector<ScreenWindow>> WindowsIntegration::screenWindows() const
    {
        const auto mappings = monitorMappings();
        if (mappings.empty())
        {
            // 一块都没配上时不猜缩放：拿别块的因子算会把窗口摆到另一块屏上，
            // 用户看到的是高亮框跑偏，比没有候选更难解释
            return std::unexpected(Error{
                    ErrorCode::ScreenUnavailable,
                    GyanisScribe::Core::uiTextUtf8("认不出桌面上有哪些窗口：平台的屏幕清单与界面的屏幕清单配不上对。请先用整屏或框选截图，重开一次智录再试"),
            });
        }

        // EnumWindows 的枚举顺序就是 z 序（从最上面那块开始），这份顺序后面直接当候选优先级用
        std::vector<RawWindow> raw;
        EnumWindows(collectVisibleWindow, reinterpret_cast<LPARAM>(&raw));

        std::vector<ScreenWindow> windows;
        windows.reserve(raw.size());
        for (const auto &entry: raw)
        {
            // 跨屏的窗口按中心点认主：中心在哪块屏，它就主要显示在哪块屏上
            const auto ownerIndex = monitorIndexForPoint(entry.physicalGlobal.center(), mappings);
            if (ownerIndex == mappings.size())
            {
                continue;
            }
            if (const auto logical = GyanisScribe::Platform::physicalToLogicalGlobal(entry.physicalGlobal, mappings[ownerIndex]); logical)
            {
                windows.push_back(ScreenWindow{reinterpret_cast<quintptr>(entry.handle), entry.title, *logical});
            }
        }
        return windows;
    }

} // namespace GyanisScribe::Platform
