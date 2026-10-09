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
#include "Ocr/OcrResultWindow.h"
#include "Ocr/OcrTypes.h"
#include "Pin/PinManager.h"
#include "App/TrayController.h"
#include "Platform/IPlatformIntegration.h"
#include "Record/FfmpegRecordingEncoder.h"
#include "Record/RecordingService.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"
#include "Ui/IconProvider.h"
#include "Ui/ThemeManager.h"
#include "Ui/Toast.h"

#include <QClipboard>
#include <QCoreApplication>
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
        m_annotation = std::make_unique<Annotation::AnnotationEngine>();
        m_pins       = std::make_unique<Pin::PinManager>(*m_settings, *m_integration);
        m_ocr        = Ocr::createOcrEngine(*m_settings, *m_integration);
        m_tray       = std::make_unique<TrayController>(*m_icons, *m_theme, *m_settings);
        m_mainWindow = std::make_unique<MainWindow>(*m_settings, *m_theme, *m_icons);
        m_toast      = std::make_unique<Ui::Toast>(*m_icons, *m_theme);
        QObject::connect(m_tray.get(), &TrayController::actionRequested, this, &AppController::handleTrayAction);
        // 没有托盘的桌面环境（容器、精简安装）照样能跑：只是少一个入口，主窗口仍然可用
        m_tray->install();
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
                                     // 识别热键：截完直接进结果窗口，中间不弹标注窗
                                     startCapture(CaptureIntent::Recognize);
                                     return;
                             }
                         });

        QObject::connect(m_capture.get(), &Capture::ICaptureService::captured, this, &AppController::handleCaptured);
        QObject::connect(m_capture.get(), &Capture::ICaptureService::captureCancelled, this, [] { Core::Logger::info("截图已取消"); });
        QObject::connect(m_capture.get(), &Capture::ICaptureService::captureFailed, this,
                         [this](const Core::Error &error)
                         {
                             Core::Logger::error(std::format("截图未完成：{}", error.message));
                             notifyUser(QString::fromUtf8(std::format("截图未完成：{}。请重试一次", error.message).c_str()), Ui::ToastKind::Danger);
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

    void AppController::startCapture(const CaptureIntent intent)
    {
        m_captureIntent = intent;
        m_capture->startInteractive();
    }

    void AppController::startFullScreenCapture(const CaptureIntent intent)
    {
        m_captureIntent = intent;
        m_capture->captureFullScreen();
    }

    void AppController::startLastRegionCapture(const CaptureIntent intent)
    {
        m_captureIntent = intent;
        m_capture->captureLastRegion();
    }

    void AppController::openMainWindow(const MainWindowPage page)
    {
        m_mainWindow->open(page);
    }

    void AppController::handleTrayAction(const TrayAction action)
    {
        switch (action)
        {
            case TrayAction::CaptureRegion:
                startCapture();
                return;
            case TrayAction::CaptureFullScreen:
                startFullScreenCapture();
                return;
            case TrayAction::CaptureLastRegion:
                startLastRegionCapture();
                return;
            case TrayAction::ToggleRecord:
                toggleRecording();
                return;
            case TrayAction::Recognize:
                // 识别入口先框选：没有一张图，后端无事可做
                startCapture(CaptureIntent::Recognize);
                return;
            case TrayAction::OpenSettings:
                openMainWindow(MainWindowPage::Settings);
                return;
            case TrayAction::OpenAbout:
                openMainWindow(MainWindowPage::About);
                return;
            case TrayAction::Quit:
                // 退出交给事件循环：托盘图标随对象析构消失，这里不自己 quit 以免绕开析构里的日志冲刷
                QCoreApplication::exit(0);
                return;
        }
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

        if (m_captureIntent == CaptureIntent::Recognize)
        {
            // 识别这条不弹标注窗：用户点的是「识别文字」，先弹一个要再点一次的界面就是多一跳
            handleOcrRequested(image.image);
            return;
        }

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
        m_ocrImage = image;
        // 语言取上一次的选择：换过一次就该记住，而不是每轮都退回默认档
        recognizeWithLanguage(m_settings->stringValue(Settings::kOcrLanguage, QString::fromUtf8(Ocr::kDefaultOcrLanguage)));
    }

    void AppController::recognizeWithLanguage(const QString &language)
    {
        if (!m_ocr->isAvailable())
        {
            const auto reason = m_ocr->unavailableReason();
            Core::Logger::error(std::format("文字识别没能开始：{}", reason.toStdString()));
            notifyUser(reason, Ui::ToastKind::Danger);
            return;
        }

        if (m_ocrImage.isNull())
        {
            notifyUser(QStringLiteral("还没有可识别的图：先按 Ctrl+Alt+A 框选一块屏幕"), Ui::ToastKind::Information);
            return;
        }

        const auto result = m_ocr->recognize(m_ocrImage, language);
        if (!result)
        {
            const auto message = QString::fromUtf8(std::format("文字识别失败：{}。请重试一次，或换一种语言再试", result.error().message).c_str());
            Core::Logger::error(message.toStdString());
            notifyUser(message, Ui::ToastKind::Danger);
            return;
        }

        // 认出多少算多少：空结果也要开窗，摘要里那句「这一张没认出文字」比一条三秒就消失的提示有用
        if (auto stored = m_settings->setValue(Settings::kOcrLanguage, language); !stored)
        {
            Core::Logger::warning(std::format("识别语言没能记住：{}", stored.error().message));
        }

        if (m_ocrWindow == nullptr)
        {
            m_ocrWindow = new Ocr::OcrResultWindow{m_ocrImage, *result, *m_icons, *m_theme};
            // 关掉就自毁，控制器只持 QPointer：下一次识别会重新建一个，不会往已析构的对象里塞结果
            m_ocrWindow->setAttribute(Qt::WA_DeleteOnClose);
            QObject::connect(m_ocrWindow, &Ocr::OcrResultWindow::recognizeRequested, this, &AppController::recognizeWithLanguage);
            QObject::connect(m_ocrWindow, &Ocr::OcrResultWindow::messageForUser, this, [this](const QString &message, Ui::ToastKind kind) { notifyUser(message, kind); });
        } else
        {
            m_ocrWindow->setResult(m_ocrImage, *result);
        }

        m_ocrWindow->show();
        m_ocrWindow->raise();
        m_ocrWindow->activateWindow();
        Core::Logger::info(std::format("文字识别完成：{} 行，语言 {}", result->lines.size(), language.toStdString()));
    }

    void AppController::notifyUser(const QString &message, const Ui::ToastKind kind)
    {
        if (m_toast != nullptr)
        {
            m_toast->show(message, kind);
            return;
        }
        // 气泡还没建起来的那一段（构造早期）不该把话吞掉：日志是兜底出口
        Core::Logger::info(std::format("提示：{}", message.toStdString()));
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
