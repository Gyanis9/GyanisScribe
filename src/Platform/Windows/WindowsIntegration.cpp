#include "Platform/Windows/WindowsIntegration.h"

#include "Platform/WindowGeometry.h"
#include "Platform/Windows/Win32Autostart.h"
#include "Platform/Windows/Win32HotkeyBackend.h"

#include "Base/Logger.h"
#include "Base/Text.h"

#include <windows.h>

#include <dwmapi.h>

#include <UIAutomation.h>

#include <wrl/client.h>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QSize>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <vector>

namespace
{
    using GyanisScribe::Base::Error;
    using GyanisScribe::Base::ErrorCode;
    using GyanisScribe::Platform::MonitorMapping;
    using GyanisScribe::Platform::ScreenWindow;

    /// EnumWindows 回调收下来的一份原始读数：坐标还是平台给的全局物理像素
    struct RawWindow
    {
        HWND    handle;               ///< 窗口句柄
        QString title;                ///< 标题栏文字
        QString processName;          ///< 进程的可执行文件名（不含路径），取不到时是空串
        QRect   physicalGlobal;       ///< 整窗的全局物理矩形（含标题栏与边框）
        QRect   clientPhysicalGlobal; ///< 客户区的全局物理矩形；取不到时为空矩形
    };

    /// EnumDisplayMonitors 回调收下来的一块屏：只有平台那一份读数
    struct RawMonitor
    {
        QString name;     ///< 设备名，与 QScreen::name() 同一个口径
        QRect   physical; ///< 全局物理矩形
    };

    /// 元素链下钻的两道上限：深度挡住惰性树里越套越深的包装层，子项预算挡住虚拟化列表一次暴露成千上万个条目
    constexpr int kElementChainMaximumDepth   = 24;
    constexpr int kElementChainChildrenBudget = 256;

    /// 被 DWM 藏起来的窗口（切到别的虚拟桌面那种）还躺在原坐标上，留着会得到一个看不见却能命中的矩形
    [[nodiscard]] bool windowIsCloaked(const HWND window)
    {
        DWORD cloaked = 0;
        return DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)) == S_OK && cloaked != 0;
    }

    /// 找「光标下那扇窗」要用的现场：z 序扫到第一扇算数的就停
    struct AnchorSearch
    {
        QPoint                       physicalPoint; ///< 光标位置，全局物理像素
        const std::vector<quintptr> *excluded;      ///< 排除名单：命中的窗口直接跳过（截图时是自家遮罩窗）
        HWND                         found = nullptr;
    };

    BOOL CALLBACK findWindowUnderPoint(const HWND window, const LPARAM parameter)
    {
        auto *const search = reinterpret_cast<AnchorSearch *>(parameter);
        if (!IsWindowVisible(window) || windowIsCloaked(window))
        {
            return TRUE;
        }
        if (std::ranges::find(*search->excluded, reinterpret_cast<quintptr>(window)) != search->excluded->end())
        {
            return TRUE;
        }

        // 与窗口候选用同一份读数口径（优先 DWM 实际可见边界）；这里刻意不过滤 WS_EX_TOOLWINDOW——
        // 候选名单跳过工具窗是「不拿浮动面板当整扇要截的窗」，而这一步问的是「光标下看得见的是什么」，
        // 浮动面板、贴图这类工具窗就摆在屏幕上，跳过它会让认出来的元素与用户看到的对不上
        RECT frame{};
        if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))) && FAILED(GetWindowRect(window, &frame)))
        {
            return TRUE;
        }
        const QRect physical{frame.left, frame.top, frame.right - frame.left, frame.bottom - frame.top};
        if (physical.isEmpty() || !physical.contains(search->physicalPoint))
        {
            return TRUE;
        }

        // EnumWindows 的顺序就是 z 序：第一扇含住光标的算数，当场停表，别看底下的
        search->found = window;
        return FALSE;
    }

    /**
     * @brief 窗口所属进程的可执行文件名（不含路径）
     * @param window 窗口句柄
     * @return 文件名（如 chrome.exe）；取不到权限或读不出时返回空串
     * @details 只留文件名不留全路径：排除清单里写 `chrome.exe` 就够了，写全路径换一台机器就对不上；
     *          权限只要 QUERY_LIMITED_INFORMATION——列出别人进程的窗口不需要更多，也要不过来。
     */
    [[nodiscard]] QString processFileName(const HWND window)
    {
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId == 0)
        {
            return {};
        }

        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (process == nullptr)
        {
            return {};
        }

        wchar_t    path[32768] = {}; // 长路径的实测上限
        DWORD      length      = static_cast<DWORD>(std::size(path));
        const auto read        = QueryFullProcessImageNameW(process, 0, path, &length);
        CloseHandle(process);
        if (read == 0 || length == 0)
        {
            return {};
        }
        // 0x5C 是反斜杠：写字符字面量在这一行里要被转义两层（VS 的 /utf-8 与这里的字符串），用码点更稳
        return QString::fromWCharArray(path, static_cast<qsizetype>(length)).section(QChar(0x5C), -1);
    }

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

        if (windowIsCloaked(window))
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

        // 客户区的读数单独取：抓帧拿到的是客户区的像素，而整窗矩形带着标题栏与边框，
        // 用后者当帧原点会把光标高亮偏出去几十格。取不到就留空矩形，不拿整窗那一份顶替
        QRect clientPhysical;
        RECT  client{};
        POINT clientCorner{};
        if (GetClientRect(window, &client) != 0 && ClientToScreen(window, &clientCorner) != 0)
        {
            clientPhysical = QRect{clientCorner.x, clientCorner.y, client.right - client.left, client.bottom - client.top};
            if (clientPhysical.isEmpty())
            {
                clientPhysical = QRect{};
            }
        }

        collected->push_back(RawWindow{window, written > 0 ? QString::fromWCharArray(titleBuffer, written) : QString(), processFileName(window), physical, clientPhysical});
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
            GyanisScribe::Base::Logger::warning(std::format("屏幕配对不完整：配上了 {} 块，平台给了 [{}]，界面给了 [{}]", mappings.size(),
                                                            platformNames.join(QStringLiteral(", ")).toStdString(), screenNames.join(QStringLiteral(", ")).toStdString()));
        }
        return mappings;
    }
} // namespace

namespace GyanisScribe::Platform
{

    WindowsIntegration::WindowsIntegration() = default;

    WindowsIntegration::~WindowsIntegration()
    {
        // 缓存的那一个入口对象自己放掉：COM 对象没人替它管引用计数
        if (m_uia != nullptr)
        {
            m_uia->Release();
        }
    }

    PlatformCapabilities WindowsIntegration::capabilities() const noexcept
    {
        // 五项都按操作系统给得出的能力填：RegisterHotKey、WS_EX_TRANSPARENT、GetForegroundWindow、
        // EnumWindows、UI Automation。录屏采集、系统声音与系统 OCR 不在这张表里占位：
        // 那三步的可用性由各自模块在调用点现探（采集探屏幕、编码看 ffmpeg 在不在、识别问系统装了哪几支语言包），
        // 静态位只会说假话
        return PlatformCapabilities{
                .globalHotkeys     = true,
                .clickThrough      = true,
                .windowDiscovery   = true,
                .windowEnumeration = true,
                .uiElements        = true,
                .autostart         = true,
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

    Base::Expected<ForegroundWindow> WindowsIntegration::foregroundWindow() const
    {
        auto *const window = GetForegroundWindow();
        if (window == nullptr)
        {
            // 会话锁屏、刚开机还没人点过窗口都会走到这里：报「先点一下」比报「不支持」更可操作
            return std::unexpected(
                    Base::Error{Base::ErrorCode::ScreenUnavailable,
                                GyanisScribe::Base::uiTextUtf8("现在没有一个在前台的窗口可截（可能锁了屏或焦点在桌面上）。请先点一下要截的那个窗口，再回来用这一项")});
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

    Base::Expected<std::vector<ScreenWindow>> WindowsIntegration::screenWindows() const
    {
        const auto mappings = monitorMappings();
        if (mappings.empty())
        {
            // 一块都没配上时不猜缩放：拿别块的因子算会把窗口摆到另一块屏上，
            // 用户看到的是高亮框跑偏，比没有候选更难解释
            return std::unexpected(Error{
                    ErrorCode::ScreenUnavailable,
                    GyanisScribe::Base::uiTextUtf8("认不出桌面上有哪些窗口：平台的屏幕清单与界面的屏幕清单配不上对。请先用整屏或框选截图，重开一次智录再试"),
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
                // 客户区跟着同一块屏的读数走：两次换算必须用同一份缩放，否则整窗与客户区会互相错开
                const auto clientLogical =
                        entry.clientPhysicalGlobal.isNull() ? QRect{} : physicalToLogicalGlobal(entry.clientPhysicalGlobal, mappings[ownerIndex]).value_or(QRect{});
                windows.push_back(ScreenWindow{reinterpret_cast<quintptr>(entry.handle), entry.title, entry.processName, *logical, clientLogical});
            }
        }
        return windows;
    }

    IUIAutomation *WindowsIntegration::uiAutomation() const
    {
        if (m_uia != nullptr)
        {
            return m_uia;
        }

        Microsoft::WRL::ComPtr<IUIAutomation> created;
        if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&created))))
        {
            return nullptr;
        }
        // 转成裸指针存起来由析构放掉：悬停每动一格都会问一次，COM 对象不能每次重建
        m_uia = created.Detach();
        return m_uia;
    }

    Base::Expected<std::vector<QRect>> WindowsIntegration::elementChainAtPoint(const QPoint &globalLogicalPoint, const std::vector<quintptr> &excludeWindowHandles) const
    {
        const auto mappings = monitorMappings();
        if (mappings.empty())
        {
            // 与窗口枚举同一道闸：配不上对就不猜缩放，认得出来的元素框会落在另一块屏上
            return std::unexpected(Error{ErrorCode::ScreenUnavailable,
                                         GyanisScribe::Base::uiTextUtf8("认不出光标下的界面元素：平台的屏幕清单与界面的屏幕清单配不上对。可以继续直接框住要截的那一块")});
        }

        // 点按它所在的那块屏换算：多显示器下拿别块的缩放因子算会整体打偏
        std::optional<std::size_t> owner;
        for (std::size_t index = 0; index < mappings.size(); ++index)
        {
            if (mappings[index].logicalGlobal.contains(globalLogicalPoint))
            {
                owner = index;
                break;
            }
        }
        if (!owner)
        {
            return std::unexpected(Error{ErrorCode::InvalidArgument,
                                         GyanisScribe::Base::uiTextUtf8("这个光标位置不落在任何一块屏幕上（多半是拔屏或改分辨率之后的陈旧坐标）。请把鼠标动一下再试")});
        }
        const auto physicalPoint = logicalPointToPhysicalGlobal(globalLogicalPoint, mappings[*owner]);
        if (!physicalPoint)
        {
            return std::unexpected(physicalPoint.error());
        }

        // 先认「光标下第一扇不在排除名单里的可见窗」：UIA 的命中测试穿不过置顶遮罩，
        // 只有先拿到那扇窗的句柄，才问得到遮罩底下的界面
        AnchorSearch search{*physicalPoint, &excludeWindowHandles};
        EnumWindows(findWindowUnderPoint, reinterpret_cast<LPARAM>(&search));
        if (search.found == nullptr)
        {
            // 光标下没有可认的窗口：交空链，让遮罩退回「只认窗口候选」，这里不报错
            return std::vector<QRect>{};
        }

        const auto uia = uiAutomation();
        if (uia == nullptr)
        {
            return std::unexpected(Error{ErrorCode::DependencyMissing,
                                         GyanisScribe::Base::uiTextUtf8("系统的 UI Automation 起不来，认不出界面元素。可以继续用悬停认整扇窗口，或直接框住要的那一块")});
        }

        Microsoft::WRL::ComPtr<IUIAutomationElement> element;
        if (FAILED(uia->ElementFromHandle(search.found, &element)) || element == nullptr)
        {
            // 这扇窗没有提供器（自绘界面、老程序）：交空链而不是报错——悬停退回只认整扇窗口正是该有的样子
            return std::vector<QRect>{};
        }

        // 找子元素要显式给一个「恒真」条件：C++ 侧的 FindAll 不收空条件（.NET 的 TrueCondition 在那边是常量）
        Microsoft::WRL::ComPtr<IUIAutomationCondition> anyChild;
        if (FAILED(uia->CreateTrueCondition(&anyChild)) || anyChild == nullptr)
        {
            return std::vector<QRect>{};
        }

        // 从窗口根往下按「子元素含住这个点」钻：先按从外到内收，收完翻过来交（消费方按「第一档 = 最深」用）
        std::vector<QRect> physicalChain;
        int                childrenBudget = kElementChainChildrenBudget;
        for (int depth = 0; depth < kElementChainMaximumDepth; ++depth)
        {
            RECT bounds{};
            if (FAILED(element->get_CurrentBoundingRectangle(&bounds)))
            {
                break;
            }
            // 零宽高的中间层不进链：消费方按键一步步选的是「块」，放一个空矩形进去就是一步死档
            const QRect physical{bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top};
            if (!physical.isEmpty())
            {
                physicalChain.push_back(physical);
            }

            if (childrenBudget <= 0)
            {
                // 撞上预算：多半是虚拟化列表，停在当前这一层比拖着界面等几百次跨进程调用好
                break;
            }
            Microsoft::WRL::ComPtr<IUIAutomationElementArray> children;
            if (FAILED(element->FindAll(TreeScope_Children, anyChild.Get(), &children)) || children == nullptr)
            {
                break;
            }
            int length = 0;
            if (FAILED(children->get_Length(&length)))
            {
                break;
            }

            Microsoft::WRL::ComPtr<IUIAutomationElement> next;
            for (int index = 0; index < length && childrenBudget > 0; ++index)
            {
                --childrenBudget;
                Microsoft::WRL::ComPtr<IUIAutomationElement> child;
                if (FAILED(children->GetElement(index, &child)) || child == nullptr)
                {
                    continue;
                }
                RECT childBounds{};
                if (FAILED(child->get_CurrentBoundingRectangle(&childBounds)))
                {
                    continue;
                }
                const QRect childRect{childBounds.left, childBounds.top, childBounds.right - childBounds.left, childBounds.bottom - childBounds.top};
                if (childRect.contains(*physicalPoint))
                {
                    // 树序里靠后的画在上面：重叠的兄弟取最后一个含住点的
                    next = child;
                }
            }
            if (next == nullptr)
            {
                break;
            }
            element = next;
        }
        std::ranges::reverse(physicalChain);

        std::vector<QRect> chain;
        chain.reserve(physicalChain.size());
        for (const auto &physical: physicalChain)
        {
            // 每个元素按自己的中心认屏：跨屏窗口的元素串在别的屏上时，拿锚点那块屏的缩放硬算会串位
            const auto ownerIndex = monitorIndexForPoint(physical.center(), mappings);
            if (ownerIndex == mappings.size())
            {
                continue;
            }
            if (const auto logical = physicalToLogicalGlobal(physical, mappings[ownerIndex]); logical)
            {
                chain.push_back(*logical);
            }
        }
        return chain;
    }

    Base::Expected<bool> WindowsIntegration::autostartEnabled() const
    {
        QSettings  runKey{QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)"), QSettings::NativeFormat};
        const auto stored = runKey.value(QString::fromLatin1(kAutostartValueName.data(), static_cast<qsizetype>(kAutostartValueName.size()))).toString();
        if (stored.isEmpty())
        {
            return false;
        }

        // 值指着别的副本或写法变了都按「本份没开」算：这一格问的是「开机时会不会起我这一份」，
        // 界面据此重新勾选一次就能纠正，而不是让用户对着一个勾着的框却等不到程序起来
        return QString::compare(stored, autostartCommandLine(QCoreApplication::applicationFilePath()), Qt::CaseInsensitive) == 0;
    }

    Base::Expected<void> WindowsIntegration::setAutostart(const bool enabled)
    {
        const auto command = autostartCommandLine(QCoreApplication::applicationFilePath());
        if (command.isEmpty())
        {
            return std::unexpected(
                    Base::Error{Base::ErrorCode::InvalidArgument, GyanisScribe::Base::uiTextUtf8("取不到本程序的完整路径，没法写开机启动项。请从安装目录启动本程序后再试一次")});
        }

        // 写在当前用户的 Run 键下：不需要管理员权限，也不需要计划任务
        QSettings  runKey{QStringLiteral(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run)"), QSettings::NativeFormat};
        const auto valueName = QString::fromLatin1(kAutostartValueName.data(), static_cast<qsizetype>(kAutostartValueName.size()));
        if (enabled)
        {
            runKey.setValue(valueName, command);
        } else
        {
            runKey.remove(valueName);
        }
        runKey.sync();
        if (runKey.status() != QSettings::NoError)
        {
            return std::unexpected(
                    Base::Error{Base::ErrorCode::FileWriteFailed,
                                GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8(
                                                                     "开机启动项没能{}：写系统设置失败（可能是权限或安全软件拦截）。可以在系统的「启动」设置里把本程序自己加进去"),
                                                             enabled ? GyanisScribe::Base::uiTextUtf8("开启") : GyanisScribe::Base::uiTextUtf8("关闭"))});
        }
        return {};
    }

} // namespace GyanisScribe::Platform
