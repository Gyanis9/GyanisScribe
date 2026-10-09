#include "App/AppController.h"

#include "Capture/CaptureService.h"
#include "Capture/IScreenCaptureSource.h"
#include "Capture/QScreenCaptureSource.h"
#include "Core/Logger.h"
#include "Hotkey/HotkeyManager.h"
#include "Hotkey/IHotkeyManager.h"
#include "Platform/IPlatformIntegration.h"
#include "Settings/SettingsStore.h"
#include "Ui/IconProvider.h"
#include "Ui/ThemeManager.h"

#include <QClipboard>
#include <QGuiApplication>

#include <format>

namespace GyanisScribe::App
{

    AppController::AppController(QObject *parent) :
        QObject(parent), m_settings(std::make_unique<Settings::SettingsStore>()), m_integration(Platform::createPlatformIntegration()),
        m_captureSource(std::make_unique<Capture::QScreenCaptureSource>())
    {
        m_capture = std::make_unique<Capture::CaptureService>(*m_captureSource, *m_settings);
        m_hotkeys = std::make_unique<Hotkey::HotkeyManager>(*m_settings, *m_integration);

        // 主题与图标：样式表落下去、图标描边色跟着走。这里挂一次连接，之后所有取图标的地方都不用再过问主题
        m_theme = std::make_unique<Ui::ThemeManager>(*m_settings);
        m_icons = std::make_unique<Ui::IconProvider>();
        QObject::connect(m_theme.get(), &Ui::ThemeManager::themeChanged, this, [this] { m_icons->setTintColor(m_theme->iconTint()); });
        // 第一次生效必然带一次通知（构造时解算位是「还没落过」的哨兵），描边色就在通知里落定
        m_theme->applyFromSettings();

        // 热键 → 动作：唯一的接线处，界面加菜单项时也是发同一个信号而不是再走一条路径
        QObject::connect(m_hotkeys.get(), &Hotkey::IHotkeyManager::actionTriggered, this,
                         [this](Hotkey::HotkeyAction action)
                         {
                             switch (action)
                             {
                                 case Hotkey::HotkeyAction::Capture:
                                     startCapture();
                                     return;
                                 case Hotkey::HotkeyAction::Record:
                                     Core::Logger::info("录屏热键已按下，但录屏模块还没接进来");
                                     return;
                                 case Hotkey::HotkeyAction::Ocr:
                                     Core::Logger::info("识别热键已按下，但识别模块还没接进来");
                                     return;
                             }
                         });

        QObject::connect(m_capture.get(), &Capture::ICaptureService::captured, this, &AppController::handleCaptured);
        QObject::connect(m_capture.get(), &Capture::ICaptureService::captureCancelled, this, [] { Core::Logger::info("截图已取消"); });
        QObject::connect(m_capture.get(), &Capture::ICaptureService::captureFailed, this,
                         [](const Core::Error &error)
                         {
                             // 失败在这里落地成用户看得见的一句话；Toast 到位之后再换成气泡提示
                             Core::Logger::error(std::format("截图未完成：{}", error.message));
                         });
    }

    AppController::~AppController() = default;

    Core::Expected<Hotkey::HotkeyReloadReport> AppController::registerHotkeys()
    {
        return m_hotkeys->reload();
    }

    void AppController::startCapture()
    {
        m_capture->startInteractive();
    }

    Settings::SettingsStore &AppController::settings() const noexcept
    {
        return *m_settings;
    }

    Ui::ThemeManager &AppController::theme() const noexcept
    {
        return *m_theme;
    }

    Ui::IconProvider &AppController::icons() const noexcept
    {
        return *m_icons;
    }

    void AppController::handleCaptured(const Core::CapturedImage &image)
    {
        // 先交剪贴板：这一步在没有标注工具栏之前就是「截完能用」的那一半
        QGuiApplication::clipboard()->setImage(image.image);
        Core::Logger::info(std::format("截图已交剪贴板：{}×{} 像素", image.image.width(), image.image.height()));
    }

} // namespace GyanisScribe::App
