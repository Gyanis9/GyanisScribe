#include "App/TrayController.h"

#include "Base/Logger.h"
#include "Base/Text.h"
#include "Platform/IPlatformIntegration.h"
#include "Platform/PlatformCapabilities.h"
#include "Record/RecordingHud.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"
#include "Ui/IconProvider.h"
#include "Ui/ThemeManager.h"

#include <QAction>
#include <QCursor>
#include <QMenu>
#include <QSystemTrayIcon>

#include <format>

namespace
{
    using GyanisScribe::App::TrayAction;
    using GyanisScribe::App::TrayMenuEntry;

    /// 表里的快捷键文本缺省值：配置没写过这一项时菜单也要能提示用户按什么
    constexpr const char *kDefaultCaptureHotkey = "Ctrl+Alt+A";
    constexpr const char *kDefaultRecordHotkey  = "Ctrl+Alt+R";
    constexpr const char *kDefaultOcrHotkey     = "Ctrl+Alt+O";
} // namespace

namespace GyanisScribe::App
{
    QString trayTooltip(const int pinCount, const int recordedSeconds, const bool paused)
    {
        if (recordedSeconds >= 0)
        {
            // 控制条收起后这就是唯一的读数处：暂停与在录要分得开，否则用户不知道帧还在不在走
            const auto label = paused ? GyanisScribe::Base::uiText("智录 · 录制已暂停 %1") : GyanisScribe::Base::uiText("智录 · 正在录制 %1");
            return label.arg(GyanisScribe::Record::formatDuration(recordedSeconds));
        }
        if (pinCount <= 0)
        {
            return GyanisScribe::Base::uiText("智录");
        }
        return GyanisScribe::Base::uiText("智录 · 贴图 %1 张").arg(pinCount);
    }

    QList<TrayMenuEntry> trayMenuEntries()
    {
        QList<TrayMenuEntry> entries;
        entries.append(TrayMenuEntry{TrayAction::CaptureRegion, "CaptureRegion", GyanisScribe::Base::uiText("框选截图"), GyanisScribe::Settings::kHotkeyCapture});
        entries.append(TrayMenuEntry{TrayAction::CaptureFullScreen, "CaptureFullScreen", GyanisScribe::Base::uiText("整屏截图"), {}});
        entries.append(TrayMenuEntry{TrayAction::CaptureLastRegion, "CaptureLastRegion", GyanisScribe::Base::uiText("重复上次选区"), {}});
        // 图标用 CaptureWindow 那一枚：与「框选截图」共用同一枚会让两行在菜单里长得一模一样
        entries.append(TrayMenuEntry{TrayAction::CaptureWindow, "CaptureWindow", GyanisScribe::Base::uiText("截取当前窗口"), {}});
        entries.append(TrayMenuEntry{TrayAction::CaptureLongRegion, "CaptureRegion", GyanisScribe::Base::uiText("滚动长截图"), GyanisScribe::Settings::kHotkeyCaptureLong});
        // 不抓屏，直接摆一张白底画布进标注窗：写字、画框、拼几张截图说明时不必先随便截一块屏当底
        entries.append(TrayMenuEntry{TrayAction::BlankCanvas, "Annotation", GyanisScribe::Base::uiText("新建空白画布标注"), {}});
        entries.append(TrayMenuEntry{}); // 分隔线
        entries.append(TrayMenuEntry{TrayAction::ToggleRecord, "Record", GyanisScribe::Base::uiText("开始 / 停止录屏"), GyanisScribe::Settings::kHotkeyRecord});
        entries.append(TrayMenuEntry{TrayAction::Recognize, "Ocr", GyanisScribe::Base::uiText("识别文字"), GyanisScribe::Settings::kHotkeyOcr});
        entries.append(TrayMenuEntry{TrayAction::RecognizeFile, "OcrImage", GyanisScribe::Base::uiText("识别图片文件…"), {}});
        entries.append(TrayMenuEntry{TrayAction::PickColor, "ColorPicker", GyanisScribe::Base::uiText("屏幕取色"), GyanisScribe::Settings::kHotkeyPick});
        entries.append(TrayMenuEntry{TrayAction::CloseAllPins, "PinClose", GyanisScribe::Base::uiText("关闭全部贴图"), {}});
        entries.append(TrayMenuEntry{TrayAction::TogglePinsVisible, "Pin", GyanisScribe::Base::uiText("收起 / 摆回全部贴图"), GyanisScribe::Settings::kHotkeyTogglePins});
        entries.append(TrayMenuEntry{TrayAction::ClearPinsThrough, "PinMouseThrough", GyanisScribe::Base::uiText("取消全部贴图穿透"), {}});
        entries.append(TrayMenuEntry{TrayAction::PasteClipboard, "PinCopy", GyanisScribe::Base::uiText("把剪贴板里的图贴到屏幕上"), {}});
        // 关闭与销毁是两档：这一条只按得回「关闭」的那几张，销毁过的一张都不回来
        entries.append(TrayMenuEntry{TrayAction::RepinClosed, "Refresh", GyanisScribe::Base::uiText("重新贴出最近关掉的贴图"), {}});
        entries.append(TrayMenuEntry{TrayAction::NextPinGroup, "PinGroup", GyanisScribe::Base::uiText("切到下一组贴图"), GyanisScribe::Settings::kHotkeyNextPinGroup});
        // 图标与「切到下一组贴图」同族（都是分组这一件事），两行的文字各自表意，不会读成同一件事
        entries.append(TrayMenuEntry{TrayAction::ManagePinGroups, "PinGroup", GyanisScribe::Base::uiText("贴图组管理…"), {}});
        entries.append(TrayMenuEntry{});
        entries.append(TrayMenuEntry{TrayAction::OpenSettings, "Settings", GyanisScribe::Base::uiText("设置"), {}});
        entries.append(TrayMenuEntry{TrayAction::OpenAbout, "About", GyanisScribe::Base::uiText("关于智录"), {}});
        entries.append(TrayMenuEntry{});
        entries.append(TrayMenuEntry{TrayAction::Quit, "Exit", GyanisScribe::Base::uiText("退出"), {}});
        return entries;
    }

    QList<TrayMenuEntry> visibleTrayMenuEntries(const Platform::PlatformCapabilities &capabilities)
    {
        QList<TrayMenuEntry> visible;
        for (const auto &entry: trayMenuEntries())
        {
            // 「截取当前窗口」要靠平台给出前台窗口，给不出的档做不出来：
            // 留着它就是让用户点出一条报错，所以按能力直接不放
            if (entry.action == TrayAction::CaptureWindow && !capabilities.windowDiscovery)
            {
                continue;
            }
            // 没有穿透能力的平台上这张出口永远只会回一句「没有贴图开着鼠标穿透」：
            // 一条按下去永远不会有用处的菜单项不该占这一格
            if (entry.action == TrayAction::ClearPinsThrough && !capabilities.clickThrough)
            {
                continue;
            }
            visible.append(entry);
        }
        return visible;
    }

    TrayController::TrayController(GyanisScribe::Ui::IconProvider &icons, GyanisScribe::Ui::ThemeManager &theme, Settings::ISettingsStore &settings,
                                   Platform::IPlatformIntegration &integration, QObject *parent) :
        QObject(parent), m_icons(icons), m_theme(theme), m_settings(settings), m_integration(integration)
    {
        // 换主题时把托盘图标与菜单重取一遍：这两样都是 install() 里按当前描边色拍的快照，
        // 不重取的话深色档留着深色描边，等于托盘图标看不见（install() 开头就 clear 菜单，可重复调）
        QObject::connect(&m_theme, &Ui::ThemeManager::themeChanged, this, [this] { install(); });
    }

    TrayController::~TrayController()
    {
        // 图标先摘：QSystemTrayIcon 还挂着菜单时把它销毁，退出路径上会留一个悬空指针
        if (m_icon != nullptr)
        {
            m_icon->setVisible(false);
        }
    }

    bool TrayController::install()
    {
        if (!QSystemTrayIcon::isSystemTrayAvailable())
        {
            // 容器与部分精简桌面环境没有托盘：这不是致命错误，界面层要改用别的入口
            Base::Logger::warning("这台机器上没有可用的系统托盘，菜单功能只能改由主窗口提供");
            return false;
        }

        if (m_icon == nullptr)
        {
            m_icon = new QSystemTrayIcon{this};
            m_menu = std::make_unique<QMenu>();
            // 左键与中键各按设置里那一格走；这条连接只在这里挂一次——install() 会随主题重跑，
            // 挂在外面每次重跑就多接一条，一次点击会发出好几个动作
            QObject::connect(m_icon, &QSystemTrayIcon::activated, this, &TrayController::onActivated);
        }
        m_menu->clear();

        // 托盘描边色跟着主题走：深色任务栏上的深色图标会完全看不见
        m_icons.setTintColor(m_theme.iconTint());
        m_icon->setIcon(m_icons.icon("Tray"));
        m_icon->setToolTip(trayTooltip(m_pinCount, m_recordedSeconds, m_recordingPaused));

        for (const auto &entry: visibleTrayMenuEntries(m_integration.capabilities()))
        {
            if (!entry.action)
            {
                m_menu->addSeparator();
                continue;
            }

            const auto action = m_menu->addAction(entry.label);
            if (!entry.iconName.empty())
            {
                action->setIcon(m_icons.icon(entry.iconName));
            }
            if (!entry.hotkeyKey.empty())
            {
                const auto fallback = entry.hotkeyKey == GyanisScribe::Settings::kHotkeyRecord
                                              ? kDefaultRecordHotkey
                                              : (entry.hotkeyKey == GyanisScribe::Settings::kHotkeyOcr ? kDefaultOcrHotkey : kDefaultCaptureHotkey);
                const auto text     = m_settings.stringValue(entry.hotkeyKey, QString::fromUtf8(fallback));
                // 菜单项上带快捷键提示是需求第五节的原话；显示的是配置里的真值，不是写死的样例。
                // 用制表符分开：Qt 的菜单会把 \t 之后的文字右对齐，这正是系统菜单里快捷键提示的排法
                action->setText(QStringLiteral("%1\t%2").arg(entry.label, text));
                action->setToolTip(GyanisScribe::Base::uiText("快捷键：%1，可在设置页改").arg(text));
            }
            const auto trayAction = *entry.action;
            QObject::connect(action, &QAction::triggered, this, [this, trayAction] { emit actionRequested(trayAction); });
        }

        m_icon->setContextMenu(m_menu.get());
        m_icon->show();
        return true;
    }

    void TrayController::onActivated(const QSystemTrayIcon::ActivationReason reason)
    {
        // 右键、双击与未知原因换成空档：那几种不归这两下管，平台自己那一套不去抢
        if (const auto role = trayClickRoleForReason(reason); role)
        {
            trayClicked(*role);
        }
    }

    void TrayController::trayClicked(const TrayClickRole role)
    {
        std::string complaint;
        const auto  decision = resolveTrayClick(m_settings, role, complaint);
        if (!complaint.empty())
        {
            // 填坏了照样按回落的那一档走：一声不响地不做事，用户分不清「没配上」与「配的是什么都不做」
            Base::Logger::warning(std::format("托盘点击读不出：{}", complaint));
        }

        switch (decision.outcome)
        {
            case TrayClickOutcome::Nothing:
                return;
            case TrayClickOutcome::ShowMenu:
                // 菜单没装出来（这台机器没有托盘，或还没走到 install()）时按不出东西，但不能崩
                if (m_menu != nullptr)
                {
                    m_menu->popup(QCursor::pos());
                }
                return;
            case TrayClickOutcome::RunAction:
                if (decision.action)
                {
                    emit actionRequested(*decision.action);
                }
                return;
        }
    }


    void TrayController::setPinCount(const int pinCount)
    {
        m_pinCount = pinCount;
        if (m_icon != nullptr)
        {
            m_icon->setToolTip(trayTooltip(m_pinCount, m_recordedSeconds, m_recordingPaused));
        }
    }

    void TrayController::setRecordingStatus(const int recordedSeconds, const bool paused)
    {
        m_recordedSeconds = recordedSeconds;
        m_recordingPaused = paused;
        if (m_icon != nullptr)
        {
            m_icon->setToolTip(trayTooltip(m_pinCount, m_recordedSeconds, m_recordingPaused));
        }
    }


} // namespace GyanisScribe::App
