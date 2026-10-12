#include "App/AppController.h"

#include "Annotation/AnnotationEngine.h"
#include "Annotation/AnnotationToolbar.h"
#include "Annotation/AnnotationWindow.h"
#include "App/ClipboardPin.h"
#include "Capture/CaptureService.h"
#include "Capture/IScreenCaptureSource.h"
#include "Capture/RegionHistory.h"
#include "Capture/ScreenCaptureSourceFactory.h"
#include "Capture/ScreenUtils.h"
#include "Core/ImageFit.h"
#include "Core/ImageFormat.h"
#include "Core/Logger.h"
#include "Core/OutputDecoration.h"
#include "Core/Paths.h"
#include "Core/Text.h"
#include "History/HistoryRules.h"
#include "History/HistoryStore.h"
#include "History/HistoryTypes.h"
#include "Hotkey/HotkeyManager.h"
#include "Hotkey/IHotkeyManager.h"
#include "Ocr/IOcrEngine.h"
#include "Ocr/OcrEngineFactory.h"
#include "Pick/ColorFormat.h"
#include "Pick/RecentColors.h"
#include "Pick/ColorPickerOverlay.h"
#include "Ocr/OcrResultWindow.h"
#include "Ocr/OcrTypes.h"
#include "Scan/BarcodeDecoder.h"
#include "Ocr/RecognitionRunner.h"
#include "Pin/PinManager.h"
#include "App/TrayController.h"
#include "Platform/IPlatformIntegration.h"
#include "Record/FfmpegRecordingEncoder.h"
#include "Record/RecorderConfig.h"
#include "Record/RecordingHud.h"
#include "Record/RecordingService.h"
#include "Settings/NamingRules.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"
#include "Ui/IconProvider.h"
#include "Ui/Language.h"
#include "Ui/ThemeManager.h"
#include "Ui/Toast.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QCursor>
#include <QGuiApplication>
#include <QMimeData>
#include <QPageLayout>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QScreen>
#include <QImageReader>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QImage>
#include <QStringList>

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

    /**
     * @brief 按有没有便携标记决定配置落在哪儿
     * @return 便携模式下是程序目录旁那份 INI，否则是系统默认位置（Windows 是注册表）
     * @details 只有配置跟着程序走：日志、历史、缓存与成品目录仍在各自的位置，README 里写清了边界。
     */
    std::unique_ptr<Settings::SettingsStore> createSettingsStore()
    {
        const auto portableFile = Core::Paths::portableConfigFile();
        if (portableFile.isEmpty())
        {
            return std::make_unique<Settings::SettingsStore>();
        }

        Core::Logger::info(std::format("便携模式：配置读写 {}", portableFile.toStdString()));
        return std::make_unique<Settings::SettingsStore>(Core::Paths::fromDisplayText(portableFile));
    }


    AppController::AppController(QObject *parent) :
        QObject(parent), m_settings(createSettingsStore()), m_integration(Platform::createPlatformIntegration()), m_captureSource(Capture::createScreenCaptureSource())
    {
        // 语言要排在所有界面对象之前：translator 装晚了，已经建好的控件仍然拿源码文案
        const auto defaultLanguageText = QString::fromUtf8(std::string{Ui::appLanguageText(Ui::AppLanguage::Chinese)});
        const auto storedLanguage      = m_settings->stringValue(Settings::kUiLanguage, defaultLanguageText);
        if (const auto parsed = Ui::parseAppLanguage(storedLanguage.toStdString()); parsed)
        {
            if (auto applied = Ui::applyLanguage(*parsed); !applied)
            {
                // 语言包缺席只降级不阻断：按中文继续跑，原因里已经给出该把它放哪儿
                Core::Logger::warning(std::format("界面语言没能切换：{}", applied.error().message));
            }
        } else
        {
            // 配置里的值读不出时不猜：按中文继续，把原话报进日志
            Core::Logger::warning(std::format("界面语言的配置值读不出：{}，按中文继续", parsed.error().message));
        }

        m_capture = std::make_unique<Capture::CaptureService>(*m_captureSource, *m_settings, *m_integration);
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
        // Runner 拿着引擎的引用，它的析构必须排在引擎之前：成员的析构按声明反序，头文件里那份声明就排在 m_ocr 后面
        m_ocrRunner = std::make_unique<Ocr::RecognitionRunner>(*m_ocr);
        // 识别在工作线程上跑，回来的两条槽仍然跑在界面线程：结果窗口的建立与刷新不能交给别的线程
        QObject::connect(m_ocrRunner.get(), &Ocr::RecognitionRunner::succeeded, this, &AppController::handleOcrSucceeded);
        QObject::connect(m_ocrRunner.get(), &Ocr::RecognitionRunner::failed, this, &AppController::handleOcrFailed);
        m_tray       = std::make_unique<TrayController>(*m_icons, *m_theme, *m_settings, *m_integration);
        m_history    = std::make_unique<History::HistoryStore>(History::HistoryStore::defaultStoragePath());
        m_mainWindow = std::make_unique<MainWindow>(*m_settings, *m_theme, *m_icons, *m_history, m_integration->capabilities(), *m_integration);
        m_toast      = std::make_unique<Ui::Toast>(*m_icons, *m_theme);
        QObject::connect(m_tray.get(), &TrayController::actionRequested, this, &AppController::handleTrayAction);
        // 装没装上要留到接线全部完成之后再处置：现身的这一刻起用户就能点，每个入口都得已经接好
        const bool trayInstalled = m_tray->install();
        m_annotationWindow       = std::make_unique<Annotation::AnnotationWindow>(*m_annotation, std::make_unique<Annotation::AnnotationToolbar>(*m_settings, *m_icons));

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
                                 case Hotkey::HotkeyAction::CaptureLong:
                                     // 与托盘那一行同一条路：热键不该有一条自己的截图逻辑
                                     startLongCapture();
                                     return;
                                 case Hotkey::HotkeyAction::Pick:
                                     startColorPick();
                                     return;
                                 case Hotkey::HotkeyAction::Ocr:
                                     // 识别热键：截完直接进结果窗口，中间不弹标注窗
                                     startCapture(CaptureIntent::Recognize);
                                     return;
                                 case Hotkey::HotkeyAction::TogglePins:
                                     // 走托盘那一条：开关的方向与提示文案都只有一份
                                     handleTrayAction(TrayAction::TogglePinsVisible);
                                     return;
                                 case Hotkey::HotkeyAction::NextPinGroup:
                                     // 同上：切组只有一条实现，热键与托盘那一行不发散
                                     handleTrayAction(TrayAction::NextPinGroup);
                                     return;
                                 case Hotkey::HotkeyAction::QuickSave:
                                     // 快速保存走的就是「截完之后」那一档的实体：热键只是替用户把这一档定死，
                                     // 不另写一条截图→存盘的捷径，两条各写一遍就会两边行为漂移
                                     startCapture(CaptureIntent::QuickSave);
                                     return;
                             }
                         });

        QObject::connect(m_capture.get(), &Capture::ICaptureService::captured, this, &AppController::handleCaptured);
        QObject::connect(m_capture.get(), &Capture::ICaptureService::captureCancelled, this, [] { Core::Logger::info("截图已取消"); });
        QObject::connect(m_capture.get(), &Capture::ICaptureService::captureFailed, this,
                         [this](const Core::Error &error)
                         {
                             Core::Logger::error(std::format("截图未完成：{}", error.message));
                             notifyUser(GyanisScribe::Core::uiText("截图未完成：%1。请重试一次").arg(QString::fromStdString(error.message)), Ui::ToastKind::Danger);
                             // 命令行那次没有界面可看提示：失败原因另外从 commandLineActionFailed 交回一份
                             if (m_commandLineActive)
                             {
                                 emit commandLineActionFailed(QString::fromStdString(error.message));
                             }
                         });

        // 标注窗的四个出口：动作只有一个入口，界面不需要知道用户是点了按钮还是按了键盘
        QObject::connect(m_annotationWindow.get(), &Annotation::AnnotationWindow::copyRequested, this, &AppController::handleCopyRequested);
        QObject::connect(m_annotationWindow.get(), &Annotation::AnnotationWindow::saveRequested, this, &AppController::handleSaveRequested);
        QObject::connect(m_annotationWindow.get(), &Annotation::AnnotationWindow::pinRequested, this, &AppController::handlePinRequested);
        QObject::connect(m_annotationWindow.get(), &Annotation::AnnotationWindow::ocrRequested, this, &AppController::handleOcrRequested);
        QObject::connect(m_annotationWindow.get(), &Annotation::AnnotationWindow::scanRequested, this, &AppController::handleScanRequested);
        QObject::connect(m_annotationWindow.get(), &Annotation::AnnotationWindow::printRequested, this, &AppController::handlePrintRequested);
        QObject::connect(m_annotationWindow.get(), &Annotation::AnnotationWindow::refreshRequested, this, &AppController::refreshLastCapture);

        // 录屏：编码器与服务各一个，热键再按一次就是「停止并编码」
        m_recorder  = std::make_unique<Record::FfmpegRecordingEncoder>();
        m_recording = std::make_unique<Record::RecordingService>(*m_captureSource, *m_recorder, *m_integration);
        QObject::connect(m_recording.get(), &Record::RecordingService::finished, this, &AppController::handleRecordingFinished);
        QObject::connect(m_recording.get(), &Record::RecordingService::failed, this, &AppController::handleRecordingFailed);
        QObject::connect(m_recording.get(), &Record::RecordingService::aborted, this, &AppController::handleRecordingAborted);
        QObject::connect(m_recording.get(), &Record::RecordingService::countdownChanged, this,
                         [this](int remainingSeconds)
                         {
                             m_countdownRemaining = remainingSeconds;
                             updateRecordingHud();
                         });
        QObject::connect(m_recording.get(), &Record::RecordingService::frameStored, this, [this] { updateRecordingHud(); });

        // 历史页的三个出口：贴图与识别都要先把成品读回内存，删除只动清单不动文件
        QObject::connect(m_mainWindow.get(), &MainWindow::historyPinRequested, this, &AppController::handleHistoryPin);
        QObject::connect(m_mainWindow.get(), &MainWindow::historyOcrRequested, this, &AppController::recognizeImageFile);
        QObject::connect(m_mainWindow.get(), &MainWindow::historyRemoveRequested, this, &AppController::handleHistoryRemove);
        QObject::connect(m_mainWindow.get(), &MainWindow::historyOpenFolderRequested, this, &AppController::handleHistoryOpenFolder);
        QObject::connect(m_mainWindow.get(), &MainWindow::historyClearRequested, this, &AppController::handleHistoryClear);
        QObject::connect(m_mainWindow.get(), &MainWindow::messageForUser, this, [this](const QString &message, Ui::ToastKind kind) { notifyUser(message, kind); });
        // 主窗口动作条与托盘菜单发的是同一个枚举，所以出口也只有这一个 handleTrayAction
        QObject::connect(m_mainWindow.get(), &MainWindow::actionRequested, this, &AppController::handleTrayAction);
        // 贴图张数走托盘悬停提示：贴图常被压在别的窗口后面，这是唯一还能看出「挂着几张」的地方
        QObject::connect(m_pins.get(), &Pin::PinManager::pinCountChanged, m_tray.get(), &TrayController::setPinCount);
        // 贴图菜单里的「识别文字」与热键、标注窗走的是同一个出口：一处接线管所有张（含启动恢复的那些）
        QObject::connect(m_pins.get(), &Pin::PinManager::pinOcrRequested, this, &AppController::handleOcrRequested);
        // 贴图上取的色与屏幕取色共用写法和复制那两句：两处各写一份，复制到的文本格式就会分家
        QObject::connect(m_pins.get(), &Pin::PinManager::pinColorPicked, this, &AppController::handlePinColorPicked);
        // 贴图标注时的「另贴一张」：与回车复制出去的结果一样走 addPin，一处入口管所有新贴图
        QObject::connect(m_pins.get(), &Pin::PinManager::pinAnotherRequested, this,
                         [this](const QImage &image, const QPoint &globalTopLeft)
                         {
                             // 留底再贴一份：不动原来那张，新的一张摆在它右下一点
                             m_pins->addPin(image, globalTopLeft);
                         });
        // 拖到贴图上的文件/链接/文本：与「把剪贴板里的内容贴到屏幕上」共用同一套内容渲染
        QObject::connect(m_pins.get(), &Pin::PinManager::pinDropRequested, this,
                         [this](const QMimeData *data, const QPoint &globalTopLeft)
                         {
                             const auto dropped = pinDroppedContent(*m_pins, data, globalTopLeft, QGuiApplication::font());
                             if (!dropped)
                             {
                                 notifyUser(QString::fromUtf8(dropped.error().message), Ui::ToastKind::Information);
                                 return;
                             }
                             notifyPinnedDrop(*dropped);
                         });
        // 移到别的组那一下它就从桌上消失了：说清它去了哪、怎么切过去，免得像「贴图自己没了」
        QObject::connect(
                m_pins.get(), &Pin::PinManager::pinMovedToGroup, this,
                [this](int group)
                {
                    if (m_pins->activeGroup() == group)
                    {
                        notifyUser(GyanisScribe::Core::uiText("这张贴图已移到当前分组，就摆在原处"), Ui::ToastKind::Information);
                        return;
                    }
                    // 热键文本从设置现读：用户改过组合键之后，提示里那个键还得是按得动的那个
                    notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("贴图已移到分组 {}，先收了起来；按 {} 或从托盘切到那一组就能看见"),
                                                                              group, m_hotkeys->bindingText(Hotkey::HotkeyAction::NextPinGroup).toStdString())
                                                         .c_str()),
                               Ui::ToastKind::Information);
                });

        // 启动把上一次会话留下的那几张摆回来；退出前把在存的这批连像素一起记下
        if (const auto restored = m_pins->restoreLayout(); !restored)
        {
            Core::Logger::warning(std::format("上次留下的贴图没摆回来：{}", restored.error().message));
        } else if (*restored > 0)
        {
            Core::Logger::info(std::format("启动恢复了 {} 张贴图", *restored));
        }
        // 挂在 aboutToQuit 而不是托盘那一条上：主窗口关闭、命令行退出与崩溃守卫收尾走的是同一个出口
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, this,
                         [this]
                         {
                             if (const auto saved = m_pins->saveLayout(); !saved)
                             {
                                 Core::Logger::error(std::format("贴图布局没能存下：{}", saved.error().message));
                             }
                         });

        // 没有托盘就没有第二个入口，而全局热键在缺后端的平台上也不生效：主窗口得自己现身，
        // 并把「关掉窗子就退出」打开。Application 里那句 setQuitOnLastWindowClosed(false) 是为
        // 「缩回托盘继续跑」准备的，没有托盘可缩时它只留下一个活着却无处可点的进程。
        if (!trayInstalled)
        {
            QGuiApplication::setQuitOnLastWindowClosed(true);
            openMainWindow(MainWindowPage::General);
        }
    }

    AppController::~AppController() = default;

    Core::Expected<Hotkey::HotkeyReloadReport> AppController::registerHotkeys()
    {
        auto report = m_hotkeys->reload();
        if (report && !report->failures.empty())
        {
            // 登记结果只写进日志的话，用户看到的就只是「按了没反应」：要当场说出来，
            // 而且把每一条组合键与它各自的原因一起说——四条未必是同一个理由。
            QStringList lines;
            for (const auto &failure: report->failures)
            {
                lines << QStringLiteral("%1（%2）").arg(failure.combination, QString::fromStdString(failure.error.message));
            }
            notifyUser(GyanisScribe::Core::uiText("这些全局热键没能登记上：") + lines.join(QStringLiteral("；")), Ui::ToastKind::Danger);
        }
        return report;
    }

    void AppController::startCapture(const std::optional<CaptureIntent> intent)
    {
        m_captureIntent = intent.value_or(settingsCaptureIntent());
        scheduleCapture([this] { m_capture->startInteractive(); });
    }

    void AppController::startFullScreenCapture(const std::optional<CaptureIntent> intent)
    {
        m_captureIntent = intent.value_or(settingsCaptureIntent());
        scheduleCapture([this] { m_capture->captureFullScreen(); });
    }

    void AppController::startLastRegionCapture(const std::optional<CaptureIntent> intent)
    {
        m_captureIntent = intent.value_or(settingsCaptureIntent());
        scheduleCapture([this] { m_capture->captureLastRegion(); });
    }

    void AppController::startLongCapture()
    {
        m_captureIntent = settingsCaptureIntent();
        scheduleCapture([this] { m_capture->startLongCapture(); });
    }

    void AppController::scheduleCapture(std::function<void()> begin)
    {
        const auto stored = m_settings->intValue(Settings::kCaptureDelaySeconds, 0);
        const auto delay  = parseCaptureDelay(stored);
        if (!delay)
        {
            // 读不出就按不延时继续：拦下这一次按键比让用户对着一个莫名其妙的等待更好解释
            Core::Logger::warning(std::format("延时截图的秒数读不出：{}，按不延时继续", delay.error().message));
        }
        const auto seconds = delay ? *delay : 0;
        if (seconds <= 0)
        {
            begin();
            return;
        }

        if (m_captureDelayTimer != nullptr)
        {
            notifyUser(GyanisScribe::Core::uiText("上一次延时截图还在等，等它弹出来之后再按"), Ui::ToastKind::Information);
            return;
        }

        m_captureDelayTimer = new QTimer{this};
        m_captureDelayTimer->setSingleShot(true);
        QObject::connect(m_captureDelayTimer, &QTimer::timeout, this,
                         [this]
                         {
                             m_captureDelayTimer->deleteLater();
                             m_captureDelayTimer = nullptr;
                             m_pendingCapture();
                         });
        // 成员里存的是这一轮要做的动作：定时器响时用户可能已经切走了窗口，走的是同一条开始截图的路
        m_pendingCapture = std::move(begin);
        m_captureDelayTimer->start(seconds * 1000);
        notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("{} 秒后开始截图：把要截的菜单或悬停状态摆好，这一会儿别动"), seconds).c_str()),
                   Ui::ToastKind::Information);
    }

    CaptureIntent AppController::settingsCaptureIntent() const
    {
        const auto stored = m_settings->stringValue(Settings::kCaptureAfterAction, QString{});
        if (stored.isEmpty())
        {
            return defaultCaptureIntent();
        }
        const auto parsed = parseCaptureIntent(stored);
        if (!parsed)
        {
            // 读不出就按默认档走，但原因要落进日志：只把界面悄悄拨回「进标注窗」，
            // 用户会以为自己那一档没保存成功，而实际是配置里那一格的值写歪了
            Core::Logger::warning(std::format("设置里的「截完之后」读不出：{}", parsed.error().message));
            return defaultCaptureIntent();
        }
        return *parsed;
    }

    void AppController::captureForegroundWindow()
    {
        // 走与其他截图入口同一条延时通道：到点才取前台窗口，才截得到「等的那几秒里摆出来的」那一扇
        scheduleCapture(
                [this]
                {
                    const auto window = m_integration->foregroundWindow();
                    if (!window)
                    {
                        // 菜单按能力位过滤过这一项，走到这里说明能力位与实际结果不一致（或以后新增了别的入口）：
                        // 原因照样发给用户，不静默什么都不做
                        notifyUser(QString::fromStdString(window.error().message), Ui::ToastKind::Danger);
                        return;
                    }

                    m_capture->captureWindow(window->handle);
                });
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
            case TrayAction::CaptureWindow:
                captureForegroundWindow();
                return;
            case TrayAction::CaptureLongRegion:
                // 长截图的入口只做「去框那一条区域」，接与收口都在截图服务里
                startLongCapture();
                return;
            case TrayAction::ToggleRecord:
                toggleRecording();
                return;
            case TrayAction::Recognize:
                // 识别入口先框选：没有一张图，后端无事可做
                startCapture(CaptureIntent::Recognize);
                return;
            case TrayAction::RecognizeFile:
                // 需求给 OCR 列了三种来路：框选区域、贴图、以及盘上的一张图，这一条走最后那种
                pickImageFileAndRecognize();
                return;
            case TrayAction::PickColor:
                startColorPick();
                return;
            case TrayAction::ClearPinsThrough:
            {
                const auto restored = m_pins->clearMouseThrough();
                // 一张都没恢复也要说一声：用户按这条是因为他觉得贴图「点不动」，
                // 没有这句他就会一直以为是程序卡住了
                notifyUser(restored > 0 ? QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("已让 {} 张贴图重新吃鼠标"), restored).c_str())
                                        : GyanisScribe::Core::uiText("没有贴图开着鼠标穿透"),
                           restored > 0 ? Ui::ToastKind::Success : Ui::ToastKind::Information);
                return;
            }
            case TrayAction::TogglePinsVisible:
            {
                // 看得见就先收、一张都看不见就摆回：这一条是开关，不是单向的隐藏。
                // 收着的贴图用户点不到，摆回这一格只能由托盘与热键给
                const auto show    = m_pins->visiblePinCount() == 0;
                const auto changed = m_pins->setAllPinsConcealed(!show);
                notifyUser(changed > 0 ? QString::fromUtf8(GyanisScribe::Core::uiFormat(show ? GyanisScribe::Core::uiTextUtf8("已摆回 {} 张贴图")
                                                                                             : GyanisScribe::Core::uiTextUtf8("已收起 {} 张贴图"),
                                                                                        changed)
                                                                   .c_str())
                                       : GyanisScribe::Core::uiText("桌上没有贴图"),
                           changed > 0 ? Ui::ToastKind::Success : Ui::ToastKind::Information);
                return;
            }
            case TrayAction::PasteClipboard:
                pasteClipboardAsPin();
                return;
            case TrayAction::NextPinGroup:
            {
                const auto switched = m_pins->switchToNextGroup();
                if (!switched)
                {
                    // 只有一组时把「怎么多出一组」说清楚：热键按下去毫无动静最容易被当成坏了
                    notifyUser(QString::fromUtf8(switched.error().message.c_str()), Ui::ToastKind::Information);
                    return;
                }
                notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("已切到贴图分组 {}，桌上是这一组"), *switched).c_str()),
                           Ui::ToastKind::Success);
                return;
            }
            case TrayAction::CloseAllPins:
                // 一次点关掉全部：张数由 pinCountChanged 通知托盘刷新，这里只留一条日志，不再弹提示吵人
                Core::Logger::info(std::format("托盘要求关闭全部贴图：{} 张", m_pins->count()));
                m_pins->closeAll();
                return;
            case TrayAction::OpenSettings:
                openMainWindow(MainWindowPage::General);
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

        // 音效与自动保存接在「截图完成」这一刻：不管之后去哪一步，先响一声、先在盘上留一份。
        // 自动保存走「存成文件」同一条落盘路（装饰与历史记账都在里面），只是不弹成功提示
        if (m_settings->boolValue(Settings::kCaptureSound, false))
        {
            QApplication::beep();
        }
        if (m_settings->boolValue(Settings::kCaptureAutoSave, false) && m_captureIntent != CaptureIntent::SaveToFile && m_captureIntent != CaptureIntent::QuickSave)
        {
            if (const auto saved = writeImageToDisk(image.image); saved)
            {
                Core::Logger::info(std::format("已自动保存：{}", saved->toStdString()));
            } else
            {
                // 自动保存失败要说出来：用户开着这一档就是指望盘上那份在，静默会把「没有备份」瞒到最后
                notifyUser(QString::fromStdString(saved.error().message), Ui::ToastKind::Danger);
            }
        }

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

        // 交完帧之后做什么由那一档决定；开关覆盖到每一个枚举值，新加一档忘了接会在这里就编不过
        switch (m_captureIntent)
        {
            case CaptureIntent::Recognize:
                // 识别这条不弹标注窗：用户点的是「识别文字」，先弹一个要再点一次的界面就是多一跳
                handleOcrRequested(image.image);
                return;
            case CaptureIntent::CopyToClipboard:
                handleCopyRequested(image.image);
                return;
            case CaptureIntent::SaveToFile:
                handleSaveRequested(image.image);
                return;
            case CaptureIntent::QuickSave:
            {
                // 快速保存：落盘 + 交剪贴板，一句提示收尾。失败时不做剪贴板那一半——
                // 「存下了也复制了」不能说半句真话，用户会以为盘上有一份
                const auto saved = writeImageToDisk(image.image);
                if (!saved)
                {
                    notifyUser(QString::fromStdString(saved.error().message), Ui::ToastKind::Danger);
                    return;
                }
                QGuiApplication::clipboard()->setImage(decorateOutput(image.image, false));
                notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("已保存并复制：{}"), saved->toStdString()).c_str()),
                           Ui::ToastKind::Success);
                return;
            }
            case CaptureIntent::PinToDesk:
                handlePinRequested(image.image);
                return;
            case CaptureIntent::Annotate:
                break;
        }
        m_annotationWindow->startAnnotation(image, screen->availableGeometry(), selection);
    }

    void AppController::handleCopyRequested(const QImage &image)
    {
        // 剪贴板带得了透明通道：装饰按「不垫白」来（垫了白，贴到深色底上的投影会围一圈白框）
        const auto decorated = decorateOutput(image, false);
        QGuiApplication::clipboard()->setImage(decorated);
        Core::Logger::info(std::format("标注结果已交剪贴板：{}×{} 像素", decorated.width(), decorated.height()));
        // 说一句：交剪贴板这一步本来没有任何界面变化，「按了没反应」与「已经进去了」只有这一处能分
        notifyUser(GyanisScribe::Core::uiText("已复制到剪贴板"), Ui::ToastKind::Success);
    }

    QImage AppController::decorateOutput(const QImage &image, const bool fillBackground) const
    {
        const auto border = m_settings->boolValue(Settings::kOutputBorder, false);
        const auto shadow = m_settings->boolValue(Settings::kOutputShadow, false);
        return Core::decorateForOutput(image, border, shadow, fillBackground);
    }

    void AppController::handleSaveRequested(const QImage &image)
    {
        const auto saved = writeImageToDisk(image);
        if (!saved)
        {
            notifyUser(QString::fromStdString(saved.error().message), Ui::ToastKind::Danger);
            return;
        }
        notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("已存成 {}"), saved->toStdString()).c_str()), Ui::ToastKind::Success);
    }

    Core::Expected<QString> AppController::writeImageToDisk(const QImage &image)
    {
        // 失败一律带上「没能存下图片」这句头：三条调用路共用一处出口，用户看到的措辞才不会三处各说各的
        const auto fail = [](const Core::Error &reason)
        {
            const auto message = GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("没能存下图片：{}"), reason.message);
            Core::Logger::error(message);
            return std::unexpected(Core::Error{reason.code, message});
        };

        const auto                  configured = m_settings->stringValue(Settings::kPathScreenshot, QString{});
        const std::filesystem::path directory  = configured.isEmpty() ? Core::Paths::defaultScreenshotDirectory() : Core::Paths::fromDisplayText(configured);
        if (const auto ensured = Core::Paths::ensureDirectory(directory); !ensured)
        {
            return fail(ensured.error());
        }

        const auto storedFormat = m_settings->stringValue(Settings::kSaveImageFormat, QStringLiteral("Png"));
        const auto format       = Core::parseImageFormat(storedFormat);
        if (!format)
        {
            // 不猜一档：猜错就是把成品按另一种压缩写出去，事后无从找回原样
            return fail(format.error());
        }
        const auto quality = m_settings->intValue(Settings::kSaveImageQuality, Core::kDefaultImageQuality);
        if (const auto checked = Core::parseImageQuality(quality); !checked)
        {
            return fail(checked.error());
        }
        if (!Core::isImageFormatWritable(*format))
        {
            // 这一档的编码器没装：说清是格式的问题，别让用户去查目录权限
            return fail(Core::Error{
                    Core::ErrorCode::PlatformUnsupported,
                    GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("这台机器的 Qt 写不出「{}」这一档。请在设置的截图页改选一档"), storedFormat.toStdString())});
        }

        // 装饰按目标端要不要透明通道来定：JPG 垫白，PNG/WebP 保持透明（写出去不丢那一层）
        const auto decorated = decorateOutput(image, !Core::imageFormatHasAlpha(*format));

        // 名字规则来自设置里那一格（留空即默认档）；起好名再让一次位：模板里去掉 %ms% 之后
        // 同一秒连拍会撞名，直接写就是把前一张悄悄盖掉
        const auto fileName = Core::Paths::imageFileName(QDateTime::currentDateTime(), Core::imageFormatExtension(*format), Settings::nameTemplateOrDefault(*m_settings));
        const auto filePath = Core::Paths::uniqueFilePath(directory, fileName);
        // 格式不单独传给 Qt：它按后缀挑编码器，再传一次格式名就是第二个出处
        if (!decorated.save(Core::Paths::toDisplayText(filePath), nullptr, quality))
        {
            return fail(Core::Error{Core::ErrorCode::FileWriteFailed, GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("「{}」写不进去。请检查这个目录能不能写"),
                                                                                                   Core::Paths::toDisplayText(filePath).toStdString())});
        }
        Core::Logger::info(std::format("成品已保存：{}", Core::Paths::toDisplayText(filePath).toStdString()));
        // 历史记的是盘上那份的尺寸：开着装饰时成品比原图大一围，记小了缩略图会裁边
        recordHistory(filePath, History::HistoryKind::Screenshot, decorated.size());
        // 三条落盘路（存文件/快速保存/自动保存）共用这一处：交出去一次路径，命令行的脚本靠它接着干活
        emit imageFileSaved(Core::Paths::toDisplayText(filePath));
        return fileName;
    }

    void AppController::refreshLastCapture(const QString &screenName, const QRect &region)
    {
        // 区域未知（整屏抓来的那几路）就没法对着重抓：说一声，而不是抓一块错的盖上去
        if (region.isEmpty())
        {
            notifyUser(GyanisScribe::Core::uiText("这一轮没记录选区，刷新截图用不了"), Ui::ToastKind::Information);
            return;
        }

        const auto frame = m_capture->grabRegionNow(region, screenName);
        if (!frame)
        {
            notifyUser(QString::fromStdString(frame.error().message), Ui::ToastKind::Danger);
            return;
        }
        m_annotationWindow->refreshCapture(*frame);
    }

    void AppController::handlePrintRequested(const QImage &image)
    {
        if (image.isNull())
        {
            notifyUser(GyanisScribe::Core::uiText("还没有可打印的画面"), Ui::ToastKind::Information);
            return;
        }

        QPrinter     printer{QPrinter::HighResolution};
        QPrintDialog dialog{&printer, m_annotationWindow.get()};
        dialog.setWindowTitle(GyanisScribe::Core::uiText("打印标注结果"));
        if (dialog.exec() != QDialog::Accepted)
        {
            // 取消是一种正常收场：什么都没打印，也什么都不用说
            return;
        }

        QPainter painter;
        if (!painter.begin(&printer))
        {
            Core::Logger::error("打印没能开始：打印设备没有准备好");
            notifyUser(GyanisScribe::Core::uiText("打印没能开始：系统打印设备没有准备好。可以先存成文件再从别处打"), Ui::ToastKind::Danger);
            return;
        }
        // 纸面按没有透明通道处理（投影底下垫白）；等比居中用的就是「图片摆进控件」那条既有换算，
        // 打印的摆放不需要第二套适配算法
        const auto decorated = decorateOutput(image, true);
        const auto page      = printer.pageLayout().paintRectPixels(printer.resolution());
        const auto target    = Core::displayRectFor(page.size(), decorated.size()).translated(page.topLeft());
        painter.drawImage(target, decorated);
        painter.end();
        Core::Logger::info(std::format("已送往打印机：{}×{} 像素", decorated.width(), decorated.height()));
        notifyUser(GyanisScribe::Core::uiText("已送往打印机"), Ui::ToastKind::Success);
    }

    void AppController::handlePinRequested(const QImage &image)
    {
        // 贴回原来那块位置：用户截哪儿就贴在哪儿，跳回主屏中央会打断「就地比对」这个用法
        m_pins->addPin(image, m_lastSelection.topLeft());
        Core::Logger::info(std::format("已贴出 {}×{} 像素", image.width(), image.height()));
    }

    void AppController::notifyPinnedDrop(const DropPinOutcome &outcome)
    {
        if (!outcome.rejectedNames.isEmpty())
        {
            notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("已贴出 {} 张；{} 不是图片，没贴"), outcome.pinnedCount,
                                                                      outcome.rejectedNames.join(QStringLiteral("、")).toStdString())
                                                 .c_str()),
                       Ui::ToastKind::Information);
            return;
        }
        notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("已贴出 {} 张"), outcome.pinnedCount).c_str()), Ui::ToastKind::Success);
    }

    void AppController::pasteClipboardAsPin()
    {
        const auto *data = QGuiApplication::clipboard()->mimeData();
        // 剪贴板里是文件（资源管理器里复制的那一份）：走拖放那条内容转换——图片读像素、动图连来源带走，
        // 一次可以贴多件；判据只有「能不能当图片贴」这一条，与拖进来的那一份共用
        if (data != nullptr && data->hasUrls() && !data->hasImage())
        {
            const auto outcome = pinDroppedContent(*m_pins, data, QCursor::pos(), QApplication::font());
            if (!outcome)
            {
                notifyUser(QString::fromUtf8(outcome.error().message), Ui::ToastKind::Information);
                return;
            }
            if (outcome->pinnedCount == 0)
            {
                notifyUser(GyanisScribe::Core::uiText("复制的文件里没有能当图片贴的：请复制图片文件本身，或复制图片内容"), Ui::ToastKind::Information);
                return;
            }
            notifyPinnedDrop(*outcome);
            return;
        }

        const auto image = pinContentFrom(data, QGuiApplication::font());
        if (!image)
        {
            // 取不到就直说原因与替代做法：这一条的入口在托盘上，附近没有任何输入框能提示
            notifyUser(QString::fromUtf8(image.error().message), Ui::ToastKind::Information);
            return;
        }

        // 落在光标那一处：剪贴板来的图没有「原来那块位置」，跟着鼠标走才是用户此刻的落点
        m_pins->addPin(*image, QCursor::pos());
        Core::Logger::info(std::format("剪贴板里的图已贴出：{}×{} 像素", image->width(), image->height()));
    }

    void AppController::handleOcrRequested(const QImage &image)
    {
        m_ocrImage = image;
        // 语言取上一次的选择：换过一次就该记住，而不是每轮都退回默认档
        recognizeWithLanguage(m_settings->stringValue(Settings::kOcrLanguage, QString::fromUtf8(Ocr::kDefaultOcrLanguage)));
    }

    void AppController::handleScanRequested(const QImage &image)
    {
        const auto readings = Scan::readBarcodes(image);
        if (!readings)
        {
            // 后端没编进来这类原因原样播出去：文案里已经写明出路
            notifyUser(QString::fromStdString(readings.error().message), Ui::ToastKind::Danger);
            return;
        }
        if (readings->empty())
        {
            notifyUser(GyanisScribe::Core::uiText("这一张里没认出二维码或条形码"), Ui::ToastKind::Information);
            return;
        }

        // 认出多个就报第一个二维的（一维在二维码花纹里偶发假阳性，见 preferredReading）；
        // 提示里说明数量
        const auto preferred = Scan::preferredReading(*readings);
        if (!preferred)
        {
            return; // 空列表在上面已经报过了，这里只是不变量兜底
        }
        QGuiApplication::clipboard()->setText(preferred->text);
        if (readings->size() == 1)
        {
            notifyUser(GyanisScribe::Core::uiText("已复制识别结果（%1 个字符）").arg(preferred->text.size()), Ui::ToastKind::Success);
        } else
        {
            notifyUser(GyanisScribe::Core::uiText("认出 %1 个码，已复制最上面那条").arg(static_cast<int>(readings->size())), Ui::ToastKind::Success);
        }
    }

    std::vector<QColor> AppController::recentPickedColors() const
    {
        const auto parsed = Pick::parseRecentColors(m_settings->stringListValue(Settings::kPickRecentColors, {}));
        if (!parsed)
        {
            Core::Logger::warning(std::format("最近取色清单读不出，按空清单继续：{}", parsed.error().message));
            return {};
        }
        return *parsed;
    }

    void AppController::rememberPickedColor(const QColor &color)
    {
        const auto updated = Pick::pushRecentColor(recentPickedColors(), color, Pick::kPickColorHistoryCapacity);
        if (auto written = m_settings->setValue(Settings::kPickRecentColors, Pick::serializeRecentColors(updated)); !written)
        {
            Core::Logger::warning(std::format("最近取色清单没能写入：{}", written.error().message));
        }
    }

    void AppController::startCommandLineCapture(const CommandLineAction action)
    {
        // 命令行那条路没有界面可等：去处定死为「快速保存」（落盘 + 交剪贴板），也不走延时那道闸——
        // 延时是给手动截菜单留的，脚本要的是按下即截
        m_commandLineActive = true;
        m_captureIntent     = CaptureIntent::QuickSave;
        switch (action)
        {
            case CommandLineAction::CaptureFullScreen:
                m_capture->captureFullScreen();
                return;
            case CommandLineAction::CaptureLastRegion:
                m_capture->captureLastRegion();
                return;
            case CommandLineAction::CaptureWindow:
            {
                const auto window = m_integration->foregroundWindow();
                if (!window)
                {
                    Core::Logger::error(std::format("命令行截窗口没成：{}", window.error().message));
                    emit commandLineActionFailed(QString::fromStdString(window.error().message));
                    return;
                }
                m_capture->captureWindow(window->handle);
                return;
            }
            default:
                emit commandLineActionFailed(GyanisScribe::Core::uiText("这个命令行动作不走截图这条路"));
                return;
        }
    }

    Core::Expected<void> AppController::pinImageFile(const QString &filePath)
    {
        const QFileInfo info{filePath};
        if (!info.exists() || !info.isFile())
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("没找到这个文件：{}"), info.absoluteFilePath().toStdString())});
        }

        // 复用拖放那条内容转换：图片读像素（GIF 连来源带走）、链接渲染成卡片，判据只有一份
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(info.absoluteFilePath())});
        const auto outcome = pinDroppedContent(*m_pins, &mime, QCursor::pos(), QApplication::font());
        if (!outcome)
        {
            return std::unexpected(outcome.error());
        }
        if (outcome->pinnedCount == 0)
        {
            return std::unexpected(Core::Error{
                    Core::ErrorCode::InvalidArgument,
                    GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("「{}」不是能贴的图片：支持 PNG / JPG / WebP / GIF 这类格式"), info.fileName().toStdString())});
        }
        return {};
    }

    void AppController::handlePinColorPicked(const QColor &color)
    {
        // 贴图上取的色也进同一份「最近取色」：下一轮屏幕取色时色块行上就有它
        rememberPickedColor(color);

        // 与屏幕取色同一套：写法从设置现读，读坏了按 HEX 顶上并把原因说给用户
        std::string complaint;
        const auto  format = Pick::colorFormatOrDefault(m_settings->stringValue(Settings::kPickFormat, QStringLiteral("Hex")).toStdString(), complaint);
        if (!complaint.empty())
        {
            notifyUser(QString::fromStdString(complaint), Ui::ToastKind::Danger);
        }

        const auto text = Pick::formatColor(color, format);
        QGuiApplication::clipboard()->setText(text);
        notifyUser(GyanisScribe::Core::uiText("已复制颜色 ") + text, Ui::ToastKind::Success);
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
            notifyUser(GyanisScribe::Core::uiText("还没有可识别的图：先按 Ctrl+Alt+A 框选一块屏幕"), Ui::ToastKind::Information);
            return;
        }

        // 一次识别要跑几十到上千毫秒，放在这条线程上整个程序就停在那儿：交出去之前先说一句，
        // 否则结果回来之前界面看着像「点了没反应」
        notifyUser(GyanisScribe::Core::uiText("正在识别这一张…"), Ui::ToastKind::Information);
        m_ocrRunner->request(m_ocrImage, language);
    }

    void AppController::handleOcrSucceeded(const QImage &image, const QString &language, const Ocr::OcrResult &result)
    {
        // 认出多少算多少：空结果也要开窗，摘要里那句「这一张没认出文字」比一条三秒就消失的提示有用
        if (auto stored = m_settings->setValue(Settings::kOcrLanguage, language); !stored)
        {
            Core::Logger::warning(std::format("识别语言没能记住：{}", stored.error().message));
        }

        // 底图用发出请求时那一张：期间用户可能又截了新图，而方框是按送去认的那一张算的，换了图就整体错位
        if (m_ocrWindow == nullptr)
        {
            m_ocrWindow = new Ocr::OcrResultWindow{image, result, *m_icons, *m_theme, Settings::nameTemplateOrDefault(*m_settings)};
            // 关掉就自毁，控制器只持 QPointer：下一次识别会重新建一个，不会往已析构的对象里塞结果
            m_ocrWindow->setAttribute(Qt::WA_DeleteOnClose);
            QObject::connect(m_ocrWindow, &Ocr::OcrResultWindow::recognizeRequested, this, &AppController::recognizeWithLanguage);
            QObject::connect(m_ocrWindow, &Ocr::OcrResultWindow::messageForUser, this, [this](const QString &message, Ui::ToastKind kind) { notifyUser(message, kind); });
        } else
        {
            m_ocrWindow->setResult(image, result);
        }

        m_ocrWindow->show();
        m_ocrWindow->raise();
        m_ocrWindow->activateWindow();
        Core::Logger::info(std::format("文字识别完成：{} 行，语言 {}", result.lines.size(), language.toStdString()));
    }

    void AppController::handleOcrFailed(const Core::Error &error)
    {
        const auto message =
                QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("文字识别失败：{}。请重试一次，或换一种语言再试"), error.message).c_str());
        Core::Logger::error(message.toStdString());
        notifyUser(message, Ui::ToastKind::Danger);
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

    void AppController::startColorPick()
    {
        auto *screen = QGuiApplication::screenAt(QCursor::pos());
        if (screen == nullptr)
        {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen == nullptr)
        {
            notifyUser(GyanisScribe::Core::uiText("这台机器上没有可用的屏幕，取色没法开始"), Ui::ToastKind::Danger);
            return;
        }

        const auto frame = m_captureSource->grabScreen(screen->name());
        if (!frame)
        {
            notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("这一屏抓不下来：{}。请重试一次"), frame.error().message).c_str()),
                       Ui::ToastKind::Danger);
            return;
        }

        // 写法读坏了不拦取色：按默认写法继续，并把这一条告诉用户，免得他复制到的格式莫名其妙变了
        std::string complaint;
        const auto  format = Pick::colorFormatOrDefault(m_settings->stringValue(Settings::kPickFormat, QStringLiteral("Hex")).toStdString(), complaint);
        if (!complaint.empty())
        {
            notifyUser(QString::fromStdString(complaint), Ui::ToastKind::Danger);
        }

        m_picker = new Pick::ColorPickerOverlay{*frame, screen->geometry(), format, *m_theme};
        // 最近取色跟着这一轮摆上遮罩：点色块或按数字键就能重新取回，不必回原处对像素
        m_picker->setRecentColors(recentPickedColors());
        QObject::connect(m_picker, &Pick::ColorPickerOverlay::colorPicked, this,
                         [this](const QColor &color, const QString &text)
                         {
                             rememberPickedColor(color);
                             QGuiApplication::clipboard()->setText(text);
                             notifyUser(GyanisScribe::Core::uiText("已复制颜色 ") + text, Ui::ToastKind::Success);
                         });
        QObject::connect(m_picker, &Pick::ColorPickerOverlay::cancelled, this, [] { Core::Logger::info("取色已取消"); });
        m_picker->showFullScreen();
        m_picker->raise();
        m_picker->activateWindow();
    }

    Core::Expected<Record::RecorderConfig> AppController::makeRecordingConfig() const
    {
        // 录哪块屏跟着光标走：多屏机器上人在副屏干活，只盯着主屏就是把用户要的画面录丢了；
        // 抓帧那一侧（QScreenCaptureSource）本来就是按光标选屏的，两处口径要一致。
        auto *screen = QGuiApplication::screenAt(QCursor::pos());
        if (screen == nullptr)
        {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen == nullptr)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::ScreenUnavailable, GyanisScribe::Core::uiTextUtf8("当前没有可用屏幕，录屏没法开始。请接回显示器后重试")});
        }

        Record::RecorderConfig config;
        config.screenName      = screen->name();
        const auto configured  = m_settings->stringValue(Settings::kPathRecording, QString());
        config.outputDirectory = configured.isEmpty() ? Core::Paths::defaultRecordingDirectory() : Core::Paths::fromDisplayText(configured);

        // 帧率取用户记下的那一档；读不出时记一条并按 RecorderConfig 的初值走，
        // 不拿一个"看起来能用"的值替用户决定（validateRecorderConfig 会在真正开始前再判一次范围）
        if (const auto parsed = Record::parseFrameRateSetting(m_settings->intValue(Settings::kRecordDefaultFrameRate, Record::kDefaultFrameRate)); parsed)
        {
            config.frameRate = *parsed;
        } else
        {
            Core::Logger::warning(std::format("录屏默认帧率读不出：{}，本次按 {} 帧开始", parsed.error().message, config.frameRate));
        }
        // 画面码率与帧率同一条形状：读不出就记一条并按初值（按质量优先）走，不替用户挑一档
        if (const auto parsed = Record::parseVideoBitrateSetting(m_settings->intValue(Settings::kRecordVideoBitrate, Record::kDefaultVideoBitrateKbps)); parsed)
        {
            config.videoBitrateKbps = *parsed;
        } else
        {
            Core::Logger::warning(std::format("录屏画面码率读不出：{}，本次按质量优先开始", parsed.error().message));
        }
        // 光标高亮是个开关，没有「读不出」这一档：配置里没有就按关；到了画不出位置的那一种模式，RecordingService 会自己报一句
        config.cursorHighlight = m_settings->boolValue(Settings::kRecordCursorHighlight, false);
        // 编码器钉子：选的是「自动」或没填就是空串（全自动逐探）；钉的是具体名字就原样交给编码器那一层去探
        const auto pinnedEncoder = m_settings->stringValue(Settings::kRecordVideoEncoder, QString::fromUtf8(std::string{Settings::kRecordVideoEncoderAutoToken}));
        if (pinnedEncoder != QString::fromUtf8(std::string{Settings::kRecordVideoEncoderAutoToken}) && !pinnedEncoder.isEmpty())
        {
            config.videoEncoder = pinnedEncoder.toStdString();
        }
        // 成品文件名模板：与截图、贴图保存、识别导出同一格，几个写盘口各取一次、各按同一规则回落
        config.nameTemplate = Settings::nameTemplateOrDefault(*m_settings);

        // 取声的两路：留空就是不取那一路。存的是 dshow 的稳定标识，换系统显示语言也不漂
        config.microphoneDevice  = m_settings->stringValue(Settings::kRecordAudioMicrophone, QString());
        config.systemSoundDevice = m_settings->stringValue(Settings::kRecordAudioSystemSound, QString());

        // 输出容器按用户选的那一档来：GIF 走两遍调色板，帧率上限比 MP4 低，
        // 那一档的天花板由 validateRecorderConfig 在真正开录之前判
        const auto storedFormat = m_settings->stringValue(Settings::kRecordFormat, QString::fromUtf8(std::string{Record::recordFormatText(config.format)}));
        const auto parsedFormat = Record::parseRecordFormat(storedFormat);
        if (!parsedFormat)
        {
            return std::unexpected(parsedFormat.error());
        }
        config.format = *parsedFormat;

        // 录制范围按用户选的那一档来；读不出或做不出来时把原因交回去，不悄悄换成整屏——
        // 那会让用户以为录的是他选的那片，实际录下整个桌面
        const auto storedScope = m_settings->stringValue(Settings::kRecordScope, QString::fromUtf8(std::string{Record::recordScopeText(config.mode)}));
        const auto parsedScope = Record::parseRecordScope(storedScope);
        if (!parsedScope)
        {
            return std::unexpected(parsedScope.error());
        }
        config.mode = *parsedScope;

        if (config.mode == Record::RecordMode::Region)
        {
            // 「上次选区」取历史首条：与截图那边「重复上次选区」同一条（选区历史见 Capture/RegionHistory.h）
            const auto history = Capture::loadRegionHistory(*m_settings);
            if (!history)
            {
                return std::unexpected(history.error());
            }
            if (history->empty())
            {
                return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                                   GyanisScribe::Core::uiTextUtf8("还没有可录的选区：先按截图热键框一次选区，再开始录屏；也可以把录制范围改回整屏")});
            }
            config.region = history->front().region;
        } else if (config.mode == Record::RecordMode::Window)
        {
            const auto window = m_integration->foregroundWindow();
            if (!window)
            {
                return std::unexpected(window.error());
            }
            config.windowHandle = window->handle;
        }
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
            showRecordingHud();
            // 倒计时取用户记下的那一档；读不出时记一条并回到默认 3 秒（validate 那道不在这里，值本身是 0..10 的整数）
            auto countdown = Record::kDefaultCountdownSeconds;
            if (const auto parsed = Record::parseCountdownSetting(m_settings->intValue(Settings::kRecordCountdownSeconds, Record::kDefaultCountdownSeconds)); parsed)
            {
                countdown = *parsed;
            } else
            {
                Core::Logger::warning(std::format("录屏倒计时读不出：{}，本次按 {} 秒开始", parsed.error().message, countdown));
            }
            if (auto started = m_recording->start(*config, countdown); !started)
            {
                hideRecordingHud();
                Core::Logger::error(std::format("录屏没能开始：{}", started.error().message));
                notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("录屏没能开始：{}"), started.error().message).c_str()),
                           Ui::ToastKind::Danger);
                return;
            }
            updateRecordingHud();
            return;
        }

        // 还在录或还在倒计时/暂停：这一按是「停止并保存」。编码已经搬到工作线程，界面不再停在控制条上——
        // 录像是真的停了，控制条与托盘那行读数再留着就是句假话，所以先收掉，再告诉用户成品还在编
        Core::Logger::info("录屏停止中，正在编码成品");
        if (auto stopped = m_recording->stop(); !stopped)
        {
            hideRecordingHud();
            Core::Logger::error(std::format("录屏收尾失败：{}", stopped.error().message));
            notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("录屏收尾失败：{}"), stopped.error().message).c_str()), Ui::ToastKind::Danger);
            return;
        }
        hideRecordingHud();
        notifyUser(GyanisScribe::Core::uiText("正在编成品，好了会说一声"), Ui::ToastKind::Information);
    }

    void AppController::showRecordingHud()
    {
        auto *screen = QGuiApplication::primaryScreen();
        if (screen == nullptr)
        {
            return;
        }

        m_hud = new Record::RecordingHud(*m_icons, screen->availableGeometry());
        QObject::connect(m_hud, &Record::RecordingHud::pauseRequested, this,
                         [this]
                         {
                             m_recording->pause();
                             updateRecordingHud();
                         });
        QObject::connect(m_hud, &Record::RecordingHud::resumeRequested, this,
                         [this]
                         {
                             m_recording->resume();
                             updateRecordingHud();
                         });
        QObject::connect(m_hud, &Record::RecordingHud::stopRequested, this, &AppController::toggleRecording);
        QObject::connect(m_hud, &Record::RecordingHud::cancelRequested, this, &AppController::cancelRecording);
        QObject::connect(m_hud, &Record::RecordingHud::minimized, this,
                         [this]
                         {
                             // 最小化只是收起这一层：托盘菜单的录屏项还在，用户照样能停
                             notifyUser(GyanisScribe::Core::uiText("控制条已收起；按 Ctrl+Alt+R 或托盘里的录屏项可停止并保存"), Ui::ToastKind::Information);
                         });

        // 时长与状态每秒推一次：服务只在帧落地时发信号，而控制条要的是「现在过了多久」
        m_hudTicker = new QTimer(this);
        m_hudTicker->setInterval(1000);
        QObject::connect(m_hudTicker, &QTimer::timeout, this, &AppController::updateRecordingHud);
        m_hudTicker->start();
        m_hud->show();
        // 一开始就把托盘那一行接管过来：第一秒的 ticker 还没跑，别让用户看到「贴图 N 张」以为没在录
        m_tray->setRecordingStatus(0, false);
    }

    void AppController::updateRecordingHud()
    {
        if (m_hud == nullptr)
        {
            return;
        }
        m_hud->applyState(m_recording->state(), m_countdownRemaining, m_recording->recordedSeconds());
        // 控制条收进托盘后，这一行读数就是用户唯一的出口：每秒推一次，暂停与在录分开显示
        m_tray->setRecordingStatus(m_recording->recordedSeconds(), m_recording->state() == Record::RecordingState::Paused);
    }

    void AppController::hideRecordingHud()
    {
        if (m_hudTicker != nullptr)
        {
            m_hudTicker->stop();
            m_hudTicker->deleteLater();
            m_hudTicker = nullptr;
        }
        if (m_hud != nullptr)
        {
            m_hud->close();
        }
        m_countdownRemaining = 0;
        // 交还给贴图张数那一档：录制结束后托盘还写着「正在录制」就是句假话
        m_tray->setRecordingStatus(-1, false);
    }

    void AppController::handleRecordingFinished(const QString &outputPath, const QSize &frameSize)
    {
        hideRecordingHud();
        Core::Logger::info(std::format("录屏成品：{}", outputPath.toStdString()));
        // 中途丢了声音要当面说一句：只写日志等于让用户拿到一个安静的成品，再自己发现没录上
        if (m_recording->audioOutcome() == Record::AudioCaptureOutcome::Failed)
        {
            notifyUser(GyanisScribe::Core::uiText("成品已出，但没带上声音：取声那一步中途失败了，原因写在日志里"), Ui::ToastKind::Information);
        }
        // 尺寸取信号里这一轮自己那份：编码在后台跑的时候用户可以再开一轮，回读成员拿到的会是后一轮的
        recordHistory(Core::Paths::fromDisplayText(outputPath), History::HistoryKind::Recording, frameSize);
    }

    void AppController::recordHistory(const std::filesystem::path &filePath, const History::HistoryKind kind, const QSize &size)
    {
        const auto entry = History::HistoryEntry{
                .moment   = QDateTime::currentDateTime(),
                .kind     = kind,
                .filePath = filePath,
                .size     = size,
        };
        if (auto stored = m_history->append(entry, History::policyFromSettings(*m_settings), QDateTime::currentDateTime()); !stored)
        {
            // 清单写不进去不该让「保存成功」变成假话：成品已经在盘上，只是下次打开找不到它
            Core::Logger::warning(std::format("成品没进历史清单：{}", stored.error().message));
            notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("成品已保存，但没进历史清单：{}"), stored.error().message).c_str()),
                       Ui::ToastKind::Danger);
            return;
        }
        m_mainWindow->refreshHistory();
    }

    Core::Expected<QImage> AppController::loadImageFile(const std::filesystem::path &filePath) const
    {
        QImageReader reader{Core::Paths::toDisplayText(filePath)};
        reader.setAutoTransform(true);
        const auto image = reader.read();
        if (image.isNull())
        {
            return Core::Expected<QImage>{std::unexpected{Core::Error{
                    .code    = Core::ErrorCode::DependencyMissing,
                    .message = GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("读不出这张图：{}。文件可能已被移动或删掉，或这个构建里没有对应的图片解码插件"),
                                                            Core::Paths::toDisplayText(filePath).toStdString()),
            }}};
        }
        return Core::Expected<QImage>{image};
    }

    void AppController::handleHistoryPin(const std::filesystem::path &filePath)
    {
        auto image = loadImageFile(filePath);
        if (!image)
        {
            notifyUser(QString::fromUtf8(image.error().message.c_str()), Ui::ToastKind::Danger);
            return;
        }
        // 盘上的原文件一并交过去：动图（GIF/APNG）贴上去要接着播，而不是停在首帧
        m_pins->addPin(*image, QCursor::pos(), Core::Paths::toDisplayText(filePath));
    }

    void AppController::pickImageFileAndRecognize()
    {
        // 没有窗口可当父对象（托盘动作发生时主窗口多半是藏起来的），对话框按应用级模态开
        const auto picked = QFileDialog::getOpenFileName(nullptr, GyanisScribe::Core::uiText("识别图片文件"), QDir::homePath(),
                                                         GyanisScribe::Core::uiText("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp *.gif)"));
        if (picked.isEmpty())
        {
            // 取消不是失败：一句都不多说，也不落日志
            return;
        }
        recognizeImageFile(Core::Paths::fromDisplayText(picked));
    }

    void AppController::recognizeImageFile(const std::filesystem::path &filePath)
    {
        auto image = loadImageFile(filePath);
        if (!image)
        {
            notifyUser(QString::fromUtf8(image.error().message.c_str()), Ui::ToastKind::Danger);
            return;
        }
        handleOcrRequested(*image);
    }

    void AppController::handleHistoryRemove(const std::filesystem::path &filePath)
    {
        if (auto removed = m_history->remove(filePath); !removed)
        {
            notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("这条历史没删掉：{}"), removed.error().message).c_str()),
                       Ui::ToastKind::Danger);
            return;
        }
        // 删记录不删文件：这句要让用户看见，否则他会以为图也被清掉了
        notifyUser(GyanisScribe::Core::uiText("已从历史里去掉，成品文件仍在 ") + Core::Paths::toDisplayText(filePath), Ui::ToastKind::Success);
        m_mainWindow->refreshHistory();
    }

    void AppController::handleHistoryOpenFolder(const std::filesystem::path &filePath)
    {
        std::error_code ignored;
        const auto      directory = filePath.parent_path();
        if (directory.empty() || !std::filesystem::exists(directory, ignored))
        {
            notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("打不开所在目录：{} 已经不在了。成品可能已被移动或删除"),
                                                                      Core::Paths::toDisplayText(directory).toStdString())
                                                 .c_str()),
                       Ui::ToastKind::Danger);
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(directory.wstring()))))
        {
            notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("打不开目录 {}。请自己到设置里那个截图目录去找"),
                                                                      Core::Paths::toDisplayText(directory).toStdString())
                                                 .c_str()),
                       Ui::ToastKind::Danger);
            return;
        }
        Core::Logger::info(std::format("已打开历史成品所在目录：{}", Core::Paths::toDisplayText(directory).toStdString()));
    }

    void AppController::handleHistoryClear()
    {
        if (auto cleared = m_history->clear(); !cleared)
        {
            notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("历史清单没清空：{}"), cleared.error().message).c_str()),
                       Ui::ToastKind::Danger);
            return;
        }
        // 与单条删除同一条口径：动的是清单，不是用户磁盘上的成品
        notifyUser(GyanisScribe::Core::uiText("历史列表已清空，成品文件仍在各自目录里"), Ui::ToastKind::Success);
        m_mainWindow->refreshHistory();
    }

    void AppController::handleRecordingFailed(const QString &reason)
    {
        hideRecordingHud();
        Core::Logger::error(std::format("录屏中止：{}", reason.toStdString()));
        notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("录屏中止：{}。请重试一次"), reason.toStdString()).c_str()),
                   Ui::ToastKind::Danger);
    }

    void AppController::cancelRecording()
    {
        const auto cancelled = m_recording->cancel();
        // 成功那一路由服务自己的 aborted 信号回到 handleRecordingAborted：收尾只有一处，两个出口不各写一份
        if (!cancelled)
        {
            Core::Logger::warning(std::format("录屏取消没成：{}", cancelled.error().message));
            notifyUser(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("取消录屏没成：{}"), cancelled.error().message).c_str()),
                       Ui::ToastKind::Danger);
        }
    }

    void AppController::handleRecordingAborted()
    {
        hideRecordingHud();
        Core::Logger::info("录屏已取消：这一段没有成品");
        notifyUser(GyanisScribe::Core::uiText("已取消录屏：这一段没有保存，临时帧也删了"), Ui::ToastKind::Information);
    }

} // namespace GyanisScribe::App
