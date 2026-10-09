#include "App/AppController.h"

#include "Annotation/AnnotationEngine.h"
#include "Annotation/AnnotationToolbar.h"
#include "Annotation/AnnotationWindow.h"
#include "Capture/CaptureService.h"
#include "Capture/IScreenCaptureSource.h"
#include "Capture/QScreenCaptureSource.h"
#include "Capture/ScreenUtils.h"
#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Hotkey/HotkeyManager.h"
#include "Hotkey/IHotkeyManager.h"
#include "Ocr/IOcrEngine.h"
#include "Ocr/OcrEngineFactory.h"
#include "Ocr/OcrTypes.h"
#include "Pin/PinManager.h"
#include "Platform/IPlatformIntegration.h"
#include "Record/FfmpegRecordingEncoder.h"
#include "Record/RecordingService.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"
#include "Ui/IconProvider.h"
#include "Ui/ThemeManager.h"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>

#include <filesystem>
#include <format>

namespace
{
    /**
     * @brief 按截图记录的屏幕名找回那块屏幕
     * @param screenName 抓帧时记下的屏幕名
     * @return 找到的屏幕；拔屏或名字对不上时返回空指针
     */
    QScreen *screenFor(const QString &screenName)
    {
        for (auto *screen: QGuiApplication::screens())
        {
            if (screen->name() == screenName)
            {
                return screen;
            }
        }
        return nullptr;
    }
} // namespace

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

        // 标注一套：引擎跨轮次复用（载入新帧自己会清空历史），窗口与工具栏也只建一次
        m_annotation       = std::make_unique<Annotation::AnnotationEngine>();
        m_pins             = std::make_unique<Pin::PinManager>(*m_settings, *m_integration);
        m_ocr              = Ocr::createOcrEngine(*m_settings, *m_integration);
        m_annotationWindow = std::make_unique<Annotation::AnnotationWindow>(*m_annotation, std::make_unique<Annotation::AnnotationToolbar>(*m_icons));

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
                                     toggleRecording();
                                     return;
                                 case Hotkey::HotkeyAction::Ocr:
                                     // 识别入口需要一张图：没有选区时先框选，再走标注窗的「识别文字」
                                     Core::Logger::info("识别热键已按下：先框选，再在标注窗里点「识别文字」");
                                     startCapture();
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

        // 标注窗的四个出口：动作只有一个入口，界面不需要知道用户是点了按钮还是按了键盘
        QObject::connect(m_annotationWindow.get(), &Annotation::AnnotationWindow::copyRequested, this, &AppController::handleCopyRequested);
        QObject::connect(m_annotationWindow.get(), &Annotation::AnnotationWindow::saveRequested, this, &AppController::handleSaveRequested);
        QObject::connect(m_annotationWindow.get(), &Annotation::AnnotationWindow::pinRequested, this, &AppController::handlePinRequested);
        QObject::connect(m_annotationWindow.get(), &Annotation::AnnotationWindow::ocrRequested, this, &AppController::handleOcrRequested);

        // 录屏：编码器与服务各一个，热键再按一次就是「停止并编码」
        m_recorder  = std::make_unique<Record::FfmpegRecordingEncoder>();
        m_recording = std::make_unique<Record::RecordingService>(*m_captureSource, *m_recorder);
        QObject::connect(m_recording.get(), &Record::RecordingService::finished, this, &AppController::handleRecordingFinished);
        QObject::connect(m_recording.get(), &Record::RecordingService::failed, this, &AppController::handleRecordingFailed);
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
        m_lastCapture = image;

        auto *screen = screenFor(image.screenName);
        if (screen == nullptr)
        {
            // 截图那块屏幕已经不在了，标注窗摆不出可用位置；退回「直接交剪贴板」并说清原因，
            // 而不是把画面摆到一块不存在的屏幕上
            QGuiApplication::clipboard()->setImage(image.image);
            Core::Logger::warning(std::format("截图所在屏幕 {} 已不可用，标注没开启，画面已直接交剪贴板", image.screenName.toStdString()));
            return;
        }

        // 选区存的是「屏幕内」的逻辑坐标，回读后要加屏幕原点换成全局坐标；
        // 没有可用记录时按整块可用区处理，工具栏照摆不误
        auto selection = screen->availableGeometry();
        if (const auto parsed = Capture::regionFromText(m_settings->stringValue(Settings::kCaptureLastRegion, QString{}).toStdString()); parsed)
        {
            selection = QRect{parsed->topLeft() + screen->geometry().topLeft(), parsed->size()};
        }
        m_lastSelection = selection;

        m_annotationWindow->startAnnotation(image, screen->availableGeometry(), selection);
    }

    void AppController::handleCopyRequested(const QImage &image)
    {
        QGuiApplication::clipboard()->setImage(image);
        Core::Logger::info(std::format("标注结果已交剪贴板：{}×{} 像素", image.width(), image.height()));
    }

    void AppController::handleSaveRequested(const QImage &image)
    {
        const auto                  configured = m_settings->stringValue(Settings::kPathScreenshot, QString{});
        const std::filesystem::path directory  = configured.isEmpty() ? Core::Paths::defaultScreenshotDirectory() : std::filesystem::path{configured.toStdString()};

        if (const auto ensured = Core::Paths::ensureDirectory(directory); !ensured)
        {
            Core::Logger::error(std::format("标注结果没存下来：{}", ensured.error().message));
            return;
        }

        const auto fileName = Core::Paths::imageFileName(QDateTime::currentDateTime(), "png");
        const auto filePath = directory / fileName.toStdString();
        if (!image.save(QString::fromLocal8Bit(filePath.string().c_str())))
        {
            Core::Logger::error(std::format("标注结果写入失败：{}。请检查目录是否可写，或先用「复制」交剪贴板", filePath.string()));
            return;
        }
        Core::Logger::info(std::format("标注结果已保存：{}", filePath.string()));
    }

    void AppController::handlePinRequested(const QImage &image)
    {
        // 贴回原来那块位置：用户截哪儿就贴在哪儿，跳回主屏中央会打断「就地比对」这个用法
        m_pins->addPin(image, m_lastSelection.topLeft());
        Core::Logger::info(std::format("已贴出 {}×{} 像素", image.width(), image.height()));
    }

    void AppController::handleOcrRequested(const QImage &image)
    {
        if (!m_ocr->isAvailable())
        {
            Core::Logger::error(std::format("文字识别没能开始：{}", m_ocr->unavailableReason().toStdString()));
            return;
        }

        const auto language = m_settings->stringValue(Settings::kOcrLanguage, QStringLiteral("eng"));
        const auto result   = m_ocr->recognize(image, language);
        if (!result)
        {
            Core::Logger::error(std::format("文字识别失败：{}", result.error().message));
            return;
        }

        if (result->isEmpty())
        {
            Core::Logger::warning("文字识别完成，但一行都没认出来。可以试试在标注前先放大，或在设置里换一种语言");
            return;
        }

        // 结果对话框还没落地，先交剪贴板：这一步让「识别」这个动作对用户是可用的而不是一个按钮
        QGuiApplication::clipboard()->setText(result->text());
        Core::Logger::info(std::format("文字识别完成：{} 行，已交剪贴板", result->lines.size()));
    }

    Core::Expected<Record::RecorderConfig> AppController::makeRecordingConfig() const
    {
        auto *screen = QGuiApplication::primaryScreen();
        if (screen == nullptr)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::ScreenUnavailable, "当前没有可用屏幕，录屏没法开始。请接回显示器后重试"});
        }

        Record::RecorderConfig config;
        config.screenName      = screen->name();
        const auto configured  = m_settings->stringValue(Settings::kPathRecording, QString());
        config.outputDirectory = configured.isEmpty() ? Core::Paths::defaultRecordingDirectory() : std::filesystem::path{configured.toStdString()};
        return config;
    }

    void AppController::toggleRecording()
    {
        const auto state = m_recording->state();
        if (state == Record::RecordingState::Idle || state == Record::RecordingState::Done || state == Record::RecordingState::Failed)
        {
            const auto config = makeRecordingConfig();
            if (!config)
            {
                Core::Logger::error(std::format("录屏没能开始：{}", config.error().message));
                return;
            }
            if (auto started = m_recording->start(*config); !started)
            {
                Core::Logger::error(std::format("录屏没能开始：{}", started.error().message));
            }
            return;
        }

        // 还在录或还在倒计时/暂停：这一按是「停止并保存」，编码要等一会儿，先记一条让用户知道在忙
        Core::Logger::info("录屏停止中，正在编码成品");
        if (auto stopped = m_recording->stop(); !stopped)
        {
            Core::Logger::error(std::format("录屏收尾失败：{}", stopped.error().message));
        }
    }

    void AppController::handleRecordingFinished(const QString &outputPath)
    {
        Core::Logger::info(std::format("录屏成品：{}", outputPath.toStdString()));
    }

    void AppController::handleRecordingFailed(const QString &reason)
    {
        Core::Logger::error(std::format("录屏中止：{}", reason.toStdString()));
    }

} // namespace GyanisScribe::App
