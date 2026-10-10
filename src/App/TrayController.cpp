#include "App/TrayController.h"

#include "Core/Logger.h"
#include "Core/Text.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"
#include "Ui/IconProvider.h"
#include "Ui/ThemeManager.h"

#include <QAction>
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
    QString trayTooltip(const int pinCount)
    {
        if (pinCount <= 0)
        {
            return GyanisScribe::Core::uiText("智录");
        }
        return GyanisScribe::Core::uiText("智录 · 贴图 %1 张").arg(pinCount);
    }

    QList<TrayMenuEntry> trayMenuEntries()
    {
        QList<TrayMenuEntry> entries;
        entries.append(TrayMenuEntry{TrayAction::CaptureRegion, "CaptureRegion", GyanisScribe::Core::uiText("框选截图"), GyanisScribe::Settings::kHotkeyCapture});
        entries.append(TrayMenuEntry{TrayAction::CaptureFullScreen, "CaptureFullScreen", GyanisScribe::Core::uiText("整屏截图"), {}});
        entries.append(TrayMenuEntry{TrayAction::CaptureLastRegion, "CaptureLastRegion", GyanisScribe::Core::uiText("重复上次选区"), {}});
        entries.append(TrayMenuEntry{}); // 分隔线
        entries.append(TrayMenuEntry{TrayAction::ToggleRecord, "Record", GyanisScribe::Core::uiText("开始 / 停止录屏"), GyanisScribe::Settings::kHotkeyRecord});
        entries.append(TrayMenuEntry{TrayAction::Recognize, "Ocr", GyanisScribe::Core::uiText("识别文字"), GyanisScribe::Settings::kHotkeyOcr});
        entries.append(TrayMenuEntry{TrayAction::RecognizeFile, "OcrImage", GyanisScribe::Core::uiText("识别图片文件…"), {}});
        entries.append(TrayMenuEntry{TrayAction::PickColor, "ColorPicker", GyanisScribe::Core::uiText("屏幕取色"), GyanisScribe::Settings::kHotkeyPick});
        entries.append(TrayMenuEntry{});
        entries.append(TrayMenuEntry{TrayAction::OpenSettings, "Settings", GyanisScribe::Core::uiText("设置"), {}});
        entries.append(TrayMenuEntry{TrayAction::OpenAbout, "About", GyanisScribe::Core::uiText("关于智录"), {}});
        entries.append(TrayMenuEntry{});
        entries.append(TrayMenuEntry{TrayAction::Quit, "Exit", GyanisScribe::Core::uiText("退出"), {}});
        return entries;
    }

    TrayController::TrayController(GyanisScribe::Ui::IconProvider &icons, GyanisScribe::Ui::ThemeManager &theme, Settings::ISettingsStore &settings, QObject *parent) :
        QObject(parent), m_icons(icons), m_theme(theme), m_settings(settings)
    {
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
            Core::Logger::warning("这台机器上没有可用的系统托盘，菜单功能只能改由主窗口提供");
            return false;
        }

        if (m_icon == nullptr)
        {
            m_icon = new QSystemTrayIcon{this};
            m_menu = std::make_unique<QMenu>();
        }
        m_menu->clear();

        // 托盘描边色跟着主题走：深色任务栏上的深色图标会完全看不见
        m_icons.setTintColor(m_theme.iconTint());
        m_icon->setIcon(m_icons.icon("Tray"));
        m_icon->setToolTip(trayTooltip(m_pinCount));

        for (const auto &entry: trayMenuEntries())
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
                action->setToolTip(GyanisScribe::Core::uiText("快捷键：%1，可在设置页改").arg(text));
            }
            const auto trayAction = *entry.action;
            QObject::connect(action, &QAction::triggered, this, [this, trayAction] { emit actionRequested(trayAction); });
        }

        m_icon->setContextMenu(m_menu.get());
        m_icon->show();
        return true;
    }

    void TrayController::notify(const QString &title, const QString &body)
    {
        if (m_icon == nullptr || !m_icon->isVisible())
        {
            Core::Logger::info(std::format("通知（无托盘）：{} — {}", title.toStdString(), body.toStdString()));
            return;
        }
        m_icon->showMessage(title, body, QSystemTrayIcon::Information, 3000);
    }

    void TrayController::setPinCount(const int pinCount)
    {
        m_pinCount = pinCount;
        if (m_icon != nullptr)
        {
            m_icon->setToolTip(trayTooltip(m_pinCount));
        }
    }

    bool TrayController::isInstalled() const noexcept
    {
        return m_icon != nullptr && m_icon->isVisible();
    }

    QString TrayController::labelFor(const TrayAction action) const
    {
        for (const auto &entry: trayMenuEntries())
        {
            if (entry.action == action)
            {
                return entry.label;
            }
        }
        return {};
    }

} // namespace GyanisScribe::App
