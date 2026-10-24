#include "Main/AppController.h"

#include "Core/Annotation/AnnotationEngine.h"
#include "Gui/Annotation/AnnotationToolbar.h"
#include "Gui/Annotation/AnnotationWindow.h"
#include "Core/Annotation/BlankCanvas.h"
#include "App/ClipboardPin.h"
#include "App/CustomHotkeys.h"
#include "App/HistoryCoordinator.h"
#include "App/DeliveryCoordinator.h"
#include "App/CaptureCoordinator.h"
#include "App/PinCoordinator.h"
#include "App/PickCoordinator.h"
#include "App/OcrCoordinator.h"
#include "App/RecordingCoordinator.h"
#include "Gui/Capture/CaptureService.h"
#include "Core/Capture/IScreenCaptureSource.h"
#include "Plugins/CaptureSources/ScreenCaptureSourceFactory.h"
#include "Core/Capture/ScreenUtils.h"
#include "Base/ExternalCommand.h"
#include "Core/Image/ImageFit.h"
#include "Core/Image/ImageLoading.h"
#include "Core/Image/ImageFormat.h"
#include "Base/Logger.h"
#include "Core/Image/OutputDecoration.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/History/HistoryStore.h"
#include "Core/History/HistoryTypes.h"
#include "Core/History/UnsavedArchive.h"
#include "Core/Hotkey/HotkeyManager.h"
#include "Core/Hotkey/IHotkeyManager.h"
#include "Core/Pick/ColorFormat.h"
#include "Core/Pick/RecentColors.h"
#include "Gui/Pick/ColorPickerOverlay.h"
#include "Plugins/Barcode/BarcodeDecoder.h"
#include "Gui/Pin/PinGroupManagerWindow.h"
#include "Gui/Pin/PinManager.h"
#include "App/TrayController.h"
#include "Platform/IPlatformIntegration.h"
#include "Core/Settings/NamingRules.h"
#include "Core/Settings/OutputStyleRules.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/Language.h"
#include "Gui/Theme/ThemeManager.h"
#include "Gui/Controls/FocusRing.h"
#include "Gui/Controls/Toast.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFileInfo>
#include <QCursor>
#include <QGuiApplication>
#include <QMimeData>
#include <QPageLayout>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QScreen>
#include <QCoreApplication>
#include <QDateTime>
#include <QUrl>
#include <QImage>
#include <QStringList>

#include <filesystem>
#include <format>

namespace
{
} // namespace

namespace GyanisScribe::Main
{

    /**
     * @brief 按有没有便携标记决定配置落在哪儿
     * @return 便携模式下是程序目录旁那份 INI，否则是系统默认位置（Windows 是注册表）
     * @details 只有配置跟着程序走：日志、历史、缓存与成品目录仍在各自的位置，README 里写清了边界。
     */
    std::unique_ptr<GyanisScribe::Core::SettingsStore> createSettingsStore()
    {
        const auto portableFile = GyanisScribe::Base::Paths::portableConfigFile();
        if (portableFile.isEmpty())
        {
            return std::make_unique<GyanisScribe::Core::SettingsStore>();
        }

        GyanisScribe::Base::Logger::info(std::format("便携模式：配置读写 {}", portableFile.toStdString()));
        return std::make_unique<GyanisScribe::Core::SettingsStore>(GyanisScribe::Base::Paths::fromDisplayText(portableFile));
    }


    AppController::AppController(QObject *parent) : AppController(AppControllerParts{}, parent)
    {
    }

    AppController::AppController(AppControllerParts parts, QObject *parent) :
        AppController(parts.settings ? std::move(parts.settings) : createSettingsStore(),
                      parts.integration ? std::move(parts.integration) : GyanisScribe::Platform::createPlatformIntegration(),
                      parts.captureSource ? std::move(parts.captureSource) : GyanisScribe::Plugins::createScreenCaptureSource(),
                      parts.historyStoragePath.empty() ? GyanisScribe::Core::HistoryStore::defaultStoragePath() : std::move(parts.historyStoragePath), parent)
    {
    }

    AppController::AppController(std::unique_ptr<GyanisScribe::Core::SettingsStore> settings, std::unique_ptr<GyanisScribe::Platform::IPlatformIntegration> integration,
                                 std::unique_ptr<GyanisScribe::Core::IScreenCaptureSource> captureSource, std::filesystem::path historyStoragePath, QObject *parent) :
        QObject(parent), m_settings(std::move(settings)), m_integration(std::move(integration)), m_captureSource(std::move(captureSource))
    {
        // 语言要排在所有界面对象之前：translator 装晚了，已经建好的控件仍然拿源码文案
        const auto defaultLanguageText = QString::fromUtf8(std::string{GyanisScribe::Gui::appLanguageText(GyanisScribe::Gui::AppLanguage::Chinese)});
        const auto storedLanguage      = m_settings->stringValue(GyanisScribe::Core::kUiLanguage, defaultLanguageText);
        if (const auto parsed = GyanisScribe::Gui::parseAppLanguage(storedLanguage.toStdString()); parsed)
        {
            if (auto applied = GyanisScribe::Gui::applyLanguage(*parsed); !applied)
            {
                // 语言包缺席只降级不阻断：按中文继续跑，原因里已经给出该把它放哪儿
                GyanisScribe::Base::Logger::warning(std::format("界面语言没能切换：{}", applied.error().message));
            }
        } else
        {
            // 配置里的值读不出时不猜：按中文继续，把原话报进日志
            GyanisScribe::Base::Logger::warning(std::format("界面语言的配置值读不出：{}，按中文继续", parsed.error().message));
        }

        m_hotkeys = std::make_unique<GyanisScribe::Core::HotkeyManager>(*m_settings, *m_integration);

        // 主题与图标：样式表落下去、图标描边色跟着走。这里挂一次连接，之后所有取图标的地方都不用再过问主题
        m_theme = std::make_unique<GyanisScribe::Gui::ThemeManager>(*m_settings);
        m_icons = std::make_unique<GyanisScribe::Gui::IconProvider>();
        QObject::connect(m_theme.get(), &GyanisScribe::Gui::ThemeManager::themeChanged, this, [this] { m_icons->setTintColor(m_theme->iconTint()); });
        // 第一次生效必然带一次通知（构造时解算位是「还没落过」的哨兵），描边色就在通知里落定
        m_theme->applyFromSettings();

        // 焦点环覆盖层排在主题之后：析构顺序反过来，它先于主题消失，绘制时读到的色板始终有效
        m_focusRing = std::make_unique<GyanisScribe::Gui::FocusRingController>([this] { return m_theme->palette(); });
        m_focusRing->setThemeKind(m_theme->resolvedKind());
        QObject::connect(m_theme.get(), &GyanisScribe::Gui::ThemeManager::themeChanged, m_focusRing.get(),
                         [this] { m_focusRing->setThemeKind(m_theme->resolvedKind()); });
        qApp->installEventFilter(m_focusRing.get());

        // 标注一套：引擎跨轮次复用（载入新帧自己会清空历史），窗口与工具栏也只建一次
        m_pins = std::make_unique<GyanisScribe::Gui::PinManager>(*m_settings, *m_integration);
        // 贴图这一族的贴出动作也搬进了协调器：管理器本身仍在这里（别的族也在用），
        // 取网页图那条链（ImageFetcher、在飞那张的落点）也归它——取图的结果总是「贴到屏幕上」
        m_pinCoordinator = std::make_unique<GyanisScribe::App::PinCoordinator>(*m_pins);
        QObject::connect(m_pinCoordinator.get(), &GyanisScribe::App::PinCoordinator::messageForUser, this, &AppController::notifyUser);
        // 取色这一族（遮罩、写法、最近取色）也搬进了协调器：控制器只留提示
        m_picker = std::make_unique<GyanisScribe::App::PickCoordinator>(*m_settings, *m_theme, *m_captureSource);
        QObject::connect(m_picker.get(), &GyanisScribe::App::PickCoordinator::messageForUser, this, &AppController::notifyUser);
        // 识别这一族整族搬进了 OcrCoordinator：控制器只留「谁要认什么」的接线，
        // 它要说的那句提示（后端没编进来、没有可认的图、认失败了）走 messageForUser 回到这一层出气泡
        m_ocr = std::make_unique<GyanisScribe::App::OcrCoordinator>(*m_settings, *m_integration, *m_icons, *m_theme);
        QObject::connect(m_ocr.get(), &GyanisScribe::App::OcrCoordinator::messageForUser, this, &AppController::notifyUser);
        m_tray         = std::make_unique<GyanisScribe::App::TrayController>(*m_icons, *m_theme, *m_settings, *m_integration);
        m_historyStore = std::make_unique<GyanisScribe::Core::HistoryStore>(std::move(historyStoragePath));
        m_mainWindow   = std::make_unique<MainWindow>(*m_settings, *m_theme, *m_icons, *m_historyStore, m_integration->capabilities(), *m_integration);
        // 历史这一族整族搬进了 HistoryCoordinator：清单本体仍由这里持有并与主窗口共享，
        // 协调器只拿引用；「清单变了要刷新窗口」由 changed() 回到这一层接——它不能自己去叫 Main，
        // 那会让 App 层反向依赖 Main，而 Main 本来就依赖 App
        m_history = std::make_unique<GyanisScribe::App::HistoryCoordinator>(*m_historyStore, *m_settings);
        QObject::connect(m_history.get(), &GyanisScribe::App::HistoryCoordinator::messageForUser, this, &AppController::notifyUser);
        QObject::connect(m_history.get(), &GyanisScribe::App::HistoryCoordinator::changed, this, [this] { m_mainWindow->refreshHistory(); });
        // 交付这一族也搬进了协调器：控制器只留提示与「命令行那条会话状态」的接线
        m_delivery = std::make_unique<GyanisScribe::App::DeliveryCoordinator>(*m_settings, *m_history);
        QObject::connect(m_delivery.get(), &GyanisScribe::App::DeliveryCoordinator::messageForUser, this, &AppController::notifyUser);
        // 贴图那一族的地盘：贴到哪儿（光标）由这一层决定，协调器只交图与来源
        QObject::connect(m_history.get(), &GyanisScribe::App::HistoryCoordinator::pinRequested, this,
                         [this](const QImage &image, const QString &sourceFile) { m_pins->addPin(image, QCursor::pos(), sourceFile); });
        m_toast = std::make_unique<GyanisScribe::Gui::Toast>(*m_icons, *m_theme);
        QObject::connect(m_tray.get(), &GyanisScribe::App::TrayController::actionRequested, this, &AppController::handleTrayAction);
        // 装没装上要留到接线全部完成之后再处置：现身的这一刻起用户就能点，每个入口都得已经接好
        const bool trayInstalled = m_tray->install();
        // 托盘在场时顶部动作条就是第二份入口：整条收起，逐屏截图改走托盘子菜单
        m_mainWindow->setTrayMenuAvailable(trayInstalled);
        QObject::connect(m_tray.get(), &GyanisScribe::App::TrayController::screenCaptureRequested, m_capture.get(), &GyanisScribe::App::CaptureCoordinator::captureScreen);

        // 热键 → 动作：唯一的接线处，界面加菜单项时也是发同一个信号而不是再走一条路径
        QObject::connect(m_hotkeys.get(), &GyanisScribe::Core::IHotkeyManager::actionTriggered, this,
                         [this](GyanisScribe::Core::HotkeyAction action)
                         {
                             switch (action)
                             {
                                 case GyanisScribe::Core::HotkeyAction::Capture:
                                     m_capture->startInteractive();
                                     return;
                                 case GyanisScribe::Core::HotkeyAction::Record:
                                     m_recording->toggle();
                                     return;
                                 case GyanisScribe::Core::HotkeyAction::CaptureLong:
                                     // 与托盘那一行同一条路：热键不该有一条自己的截图逻辑
                                     m_capture->startLong();
                                     return;
                                 case GyanisScribe::Core::HotkeyAction::Pick:
                                     m_picker->start();
                                     return;
                                 case GyanisScribe::Core::HotkeyAction::Ocr:
                                     // 识别热键：截完直接进结果窗口，中间不弹标注窗
                                     m_capture->startInteractive(GyanisScribe::App::CaptureIntent::Recognize);
                                     return;
                                 case GyanisScribe::Core::HotkeyAction::TogglePins:
                                     // 走托盘那一条：开关的方向与提示文案都只有一份
                                     handleTrayAction(GyanisScribe::App::TrayAction::TogglePinsVisible);
                                     return;
                                 case GyanisScribe::Core::HotkeyAction::NextPinGroup:
                                     // 同上：切组只有一条实现，热键与托盘那一行不发散
                                     handleTrayAction(GyanisScribe::App::TrayAction::NextPinGroup);
                                     return;
                                 case GyanisScribe::Core::HotkeyAction::QuickSave:
                                     // 快速保存走的就是「截完之后」那一档的实体：热键只是替用户把这一档定死，
                                     // 不另写一条截图→存盘的捷径，两条各写一遍就会两边行为漂移
                                     m_capture->startInteractive(GyanisScribe::App::CaptureIntent::QuickSave);
                                     return;
                             }
                         });

        // 自定义绑定的那一路：热键层只报回一个标识，功能名到动作的映射住在这一层（词表在 App/CustomHotkeys.h）
        QObject::connect(m_hotkeys.get(), &GyanisScribe::Core::IHotkeyManager::customActionTriggered, this,
                         [this](const QString &token)
                         {
                             const auto action = GyanisScribe::App::trayActionForToken(token);
                             if (!action)
                             {
                                 // 配置里留着一个不再可绑的标识：说清是没认出来，而不是按了没反应
                                 GyanisScribe::Base::Logger::warning(std::format("自定义热键报回没认出的功能标识「{}」，已忽略", token.toStdString()));
                                 return;
                             }
                             handleTrayAction(*action);
                         });
        // 自定义键不在热键层那八个的订阅里：改完这一族的任何一格，由这里重登一次，不必重启
        QObject::connect(m_settings.get(), &GyanisScribe::Core::ISettingsStore::valueChanged, this,
                         [this](const QString &key)
                         {
                             const auto prefix =
                                     QString::fromUtf8(GyanisScribe::Core::kHotkeyCustomPrefix.data(), static_cast<qsizetype>(GyanisScribe::Core::kHotkeyCustomPrefix.size()));
                             if (!key.startsWith(prefix))
                             {
                                 return;
                             }
                             if (const auto report = registerHotkeys(); !report)
                             {
                                 GyanisScribe::Base::Logger::warning(std::format("自定义热键重登没走通：{}", report.error().message));
                             }
                         });

        // 截图这一族（枢纽）整族搬进了 CaptureCoordinator：入口、延时、抓帧、
        // 按意图交付与标注窗都在它身上；控制器只留两根线——要说给用户的话、命令行那次失败的另交一份
        m_capture = std::make_unique<GyanisScribe::App::CaptureCoordinator>(GyanisScribe::App::CaptureCoordinator::Dependencies{
                *m_settings, *m_integration, *m_theme, *m_icons, *m_captureSource, *m_ocr, *m_pinCoordinator, *m_delivery, *m_history});
        QObject::connect(m_capture.get(), &GyanisScribe::App::CaptureCoordinator::messageForUser, this, &AppController::notifyUser);
        QObject::connect(m_capture.get(), &GyanisScribe::App::CaptureCoordinator::commandLineActionFailed, this, &AppController::commandLineActionFailed);
        QObject::connect(m_capture.get(), &GyanisScribe::App::CaptureCoordinator::commandLineCaptureDelivered, this, &AppController::commandLineCaptureDelivered);

        // 录屏这一族整族搬进了 RecordingCoordinator：控制器只留三根线——
        // 要说给用户的话、托盘那一行读数、成品归档进历史
        m_recording = std::make_unique<GyanisScribe::App::RecordingCoordinator>(*m_captureSource, *m_integration, *m_settings, *m_icons);
        QObject::connect(m_recording.get(), &GyanisScribe::App::RecordingCoordinator::messageForUser, this, &AppController::notifyUser);
        QObject::connect(m_recording.get(), &GyanisScribe::App::RecordingCoordinator::recordingStatusChanged, this,
                         [this](const int seconds, const bool paused) { m_tray->setRecordingStatus(seconds, paused); });
        QObject::connect(m_recording.get(), &GyanisScribe::App::RecordingCoordinator::finished, this, [this](const QString &outputPath, const QSize &frameSize)
                         { m_history->record(GyanisScribe::Base::Paths::fromDisplayText(outputPath), GyanisScribe::Core::HistoryKind::Recording, frameSize); });

        // 历史页的三个出口：贴图与识别都要先把成品读回内存，删除只动清单不动文件
        QObject::connect(m_mainWindow.get(), &MainWindow::historyPinRequested, m_history.get(), &GyanisScribe::App::HistoryCoordinator::requestPin);
        QObject::connect(m_mainWindow.get(), &MainWindow::historyOcrRequested, m_ocr.get(), &GyanisScribe::App::OcrCoordinator::recognizeFile);
        QObject::connect(m_mainWindow.get(), &MainWindow::historyRemoveRequested, m_history.get(), &GyanisScribe::App::HistoryCoordinator::remove);
        QObject::connect(m_mainWindow.get(), &MainWindow::historyOpenFolderRequested, m_history.get(), &GyanisScribe::App::HistoryCoordinator::openContainingFolder);
        QObject::connect(m_mainWindow.get(), &MainWindow::historyClearRequested, m_history.get(), &GyanisScribe::App::HistoryCoordinator::clear);
        QObject::connect(m_mainWindow.get(), &MainWindow::messageForUser, this, [this](const QString &message, GyanisScribe::Gui::ToastKind kind) { notifyUser(message, kind); });
        // 关设置窗就是退出程序：托盘常驻指的是「窗口没关时」还活着，不是关掉之后还得再去托盘点一次退出
        QObject::connect(m_mainWindow.get(), &MainWindow::quitRequested, qApp, &QCoreApplication::quit);
        // 主窗口动作条与托盘菜单发的是同一个枚举，所以出口也只有这一个 handleTrayAction
        QObject::connect(m_mainWindow.get(), &MainWindow::actionRequested, this, &AppController::handleTrayAction);
        // 「整屏截图：<屏名>」那一条：与整屏截图同一个出口，只是把屏名点了出来
        QObject::connect(m_mainWindow.get(), &MainWindow::captureScreenRequested, m_capture.get(), &GyanisScribe::App::CaptureCoordinator::captureScreen);
        // 贴图张数走托盘悬停提示：贴图常被压在别的窗口后面，这是唯一还能看出「挂着几张」的地方
        QObject::connect(m_pins.get(), &GyanisScribe::Gui::PinManager::pinCountChanged, m_tray.get(), &GyanisScribe::App::TrayController::setPinCount);
        // 贴图菜单里的「识别文字」与热键、标注窗走的是同一个出口：一处接线管所有张（含启动恢复的那些）
        QObject::connect(m_pins.get(), &GyanisScribe::Gui::PinManager::pinOcrRequested, m_ocr.get(), &GyanisScribe::App::OcrCoordinator::requestRecognition);
        // 贴图上取的色与屏幕取色共用写法和复制那两句：两处各写一份，复制到的文本格式就会分家
        QObject::connect(m_pins.get(), &GyanisScribe::Gui::PinManager::pinColorPicked, m_picker.get(), &GyanisScribe::App::PickCoordinator::copyPickedColor);
        // 贴图标注时的「另贴一张」：与回车复制出去的结果一样走 addPin，一处入口管所有新贴图
        QObject::connect(m_pins.get(), &GyanisScribe::Gui::PinManager::pinAnotherRequested, m_pinCoordinator.get(), &GyanisScribe::App::PinCoordinator::pinAnother);
        // 拖到贴图上的文件/链接/文本：与「把剪贴板里的内容贴到屏幕上」共用同一套内容渲染
        QObject::connect(m_pins.get(), &GyanisScribe::Gui::PinManager::pinDropRequested, m_pinCoordinator.get(), &GyanisScribe::App::PinCoordinator::handleDrop);
        // 贴图窗上的 `Ctrl+V`：只有序号能认人——剪贴板在这一层，像素在哪一张上也只有管理器知道
        QObject::connect(m_pins.get(), &GyanisScribe::Gui::PinManager::pinClipboardReplaceRequested, m_pinCoordinator.get(),
                         &GyanisScribe::App::PinCoordinator::replaceWithClipboard);
        // `F5` 与右键「刷新这一张」：抓屏的口子在这里，管理器只管谁在册与哪几档不许刷
        QObject::connect(m_pins.get(), &GyanisScribe::Gui::PinManager::pinRefreshRequested, this, &AppController::refreshPinWithScreenUnder);
        // 整组「批量保存」落完账：一次写走 N 份文件，不说清去了哪儿就等于没存
        QObject::connect(m_pins.get(), &GyanisScribe::Gui::PinManager::pinBatchSaveReported, m_delivery.get(), &GyanisScribe::App::DeliveryCoordinator::reportBatchSave);
        // 移到别的组那一下它就从桌上消失了：说清它去了哪、怎么切过去，免得像「贴图自己没了」
        QObject::connect(m_pins.get(), &GyanisScribe::Gui::PinManager::pinMovedToGroup, this,
                         [this](int group)
                         {
                             if (m_pins->activeGroup() == group)
                             {
                                 notifyUser(GyanisScribe::Base::uiText("这张贴图已移到当前分组，就摆在原处"), GyanisScribe::Gui::ToastKind::Information);
                                 return;
                             }
                             // 热键文本从设置现读：用户改过组合键之后，提示里那个键还得是按得动的那个
                             notifyUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("贴图已移到{}，先收了起来；按 {} 或从托盘切到那一组就能看见"),
                                                                                       m_pinCoordinator->groupLabel(group).toStdString(),
                                                                                       m_hotkeys->bindingText(GyanisScribe::Core::HotkeyAction::NextPinGroup).toStdString())
                                                                  .c_str()),
                                        GyanisScribe::Gui::ToastKind::Information);
                         });
        // 改组名与删组都在管理器落账，这两句是它们的回声：菜单早就收起来了，不出声就成了「敲了字什么也没变」
        QObject::connect(m_pins.get(), &GyanisScribe::Gui::PinManager::pinGroupRenamed, m_pinCoordinator.get(), &GyanisScribe::App::PinCoordinator::reportGroupRenamed);
        QObject::connect(m_pins.get(), &GyanisScribe::Gui::PinManager::pinGroupDeleted, m_pinCoordinator.get(), &GyanisScribe::App::PinCoordinator::reportGroupDeleted);

        // 启动把上一次会话留下的那几张摆回来；退出前把在存的这批连像素一起记下
        if (const auto restored = m_pins->restoreLayout(); !restored)
        {
            GyanisScribe::Base::Logger::warning(std::format("上次留下的贴图没摆回来：{}", restored.error().message));
        } else if (*restored > 0)
        {
            GyanisScribe::Base::Logger::info(std::format("启动恢复了 {} 张贴图", *restored));
        }
        // 挂在 aboutToQuit 而不是托盘那一条上：主窗口关闭、命令行退出与崩溃守卫收尾走的是同一个出口
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, this,
                         [this]
                         {
                             if (const auto saved = m_pins->saveLayout(); !saved)
                             {
                                 GyanisScribe::Base::Logger::error(std::format("贴图布局没能存下：{}", saved.error().message));
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

    GyanisScribe::Base::Expected<GyanisScribe::Core::HotkeyReloadReport> AppController::registerHotkeys()
    {
        // 先把手绑的那一批交给热键层再重登：登记要的是「这一批」，中间态不必单独登一次
        m_hotkeys->setCustomBindings(GyanisScribe::App::collectCustomBindings(*m_settings));
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
            notifyUser(GyanisScribe::Base::uiText("这些全局热键没能登记上：") + lines.join(QStringLiteral("；")), GyanisScribe::Gui::ToastKind::Danger);
        }
        if (report && !report->customFailures.empty())
        {
            // 自定义那一批单独报一句：这里要点的是功能名（「整屏截图」），只报组合键没人知道是哪件事没绑上
            QStringList lines;
            for (const auto &failure: report->customFailures)
            {
                const auto bound = GyanisScribe::App::trayActionForToken(failure.token);
                const auto label = bound ? GyanisScribe::App::bindableActionLabel(*bound) : failure.token;
                lines << QStringLiteral("%1（%2）").arg(label + QStringLiteral(" ") + failure.combination, QString::fromStdString(failure.error.message));
            }
            notifyUser(GyanisScribe::Base::uiText("这些自定义热键没能登记上：") + lines.join(QStringLiteral("；")), GyanisScribe::Gui::ToastKind::Danger);
        }
        return report;
    }

    void AppController::openMainWindow(const MainWindowPage page)
    {
        m_mainWindow->open(page);
    }

    void AppController::startColorPick()
    {
        // 取色这一族在 PickCoordinator 里，这里只转发：Application 那条命令行 --pick 入口认的是控制器
        m_picker->start();
    }

    void AppController::startCommandLineCapture(const GyanisScribe::App::CommandLineRequest &request)
    {
        // 截图这一族在 CaptureCoordinator 里，这里只转发：Application 那条命令行入口认的还是控制器
        m_capture->startCommandLine(request);
    }

    GyanisScribe::App::CaptureCoordinator &AppController::capture() const noexcept
    {
        return *m_capture;
    }

    GyanisScribe::Base::Expected<void> AppController::pinImageFile(const QString &filePath)
    {
        // 识图与贴出那一族在 PinCoordinator 里，这里只转发：Application 那条命令行入口认的还是控制器
        return m_pinCoordinator->pinFile(filePath);
    }

    void AppController::handleTrayAction(const GyanisScribe::App::TrayAction action)
    {
        switch (action)
        {
            case GyanisScribe::App::TrayAction::CaptureRegion:
                m_capture->startInteractive();
                return;
            case GyanisScribe::App::TrayAction::CaptureFullScreen:
                m_capture->startFullScreen();
                return;
            case GyanisScribe::App::TrayAction::CaptureLastRegion:
                m_capture->startLastRegion();
                return;
            case GyanisScribe::App::TrayAction::CaptureWindow:
                m_capture->captureForegroundWindow();
                return;
            case GyanisScribe::App::TrayAction::CaptureLongRegion:
                // 长截图的入口只做「去框那一条区域」，接与收口都在截图服务里
                m_capture->startLong();
                return;
            case GyanisScribe::App::TrayAction::BlankCanvas:
                m_capture->startBlankCanvas();
                return;
            case GyanisScribe::App::TrayAction::RepinClosed:
                m_pinCoordinator->repinLastClosed();
                return;
            case GyanisScribe::App::TrayAction::ToggleRecord:
                m_recording->toggle();
                return;
            case GyanisScribe::App::TrayAction::Recognize:
                // 识别入口先框选：没有一张图，后端无事可做
                m_capture->startInteractive(GyanisScribe::App::CaptureIntent::Recognize);
                return;
            case GyanisScribe::App::TrayAction::RecognizeFile:
                // 需求给 OCR 列了三种来路：框选区域、贴图、以及盘上的一张图，这一条走最后那种
                m_ocr->pickImageFileAndRecognize();
                return;
            case GyanisScribe::App::TrayAction::PickColor:
                m_picker->start();
                return;
            case GyanisScribe::App::TrayAction::ClearPinsThrough:
                m_pinCoordinator->restoreMouseThrough();
                return;
            case GyanisScribe::App::TrayAction::TogglePinsVisible:
                m_pinCoordinator->toggleVisibility();
                return;
            case GyanisScribe::App::TrayAction::PasteClipboard:
                m_pinCoordinator->pasteClipboard();
                return;
            case GyanisScribe::App::TrayAction::NextPinGroup:
                m_pinCoordinator->switchToNextGroup();
                return;
            case GyanisScribe::App::TrayAction::CloseAllPins:
                m_pinCoordinator->closeAll();
                return;
            case GyanisScribe::App::TrayAction::ManagePinGroups:
                m_pinCoordinator->openGroupManager();
                return;
            case GyanisScribe::App::TrayAction::OpenSettings:
                openMainWindow(MainWindowPage::General);
                return;
            case GyanisScribe::App::TrayAction::OpenAbout:
                openMainWindow(MainWindowPage::About);
                return;
            case GyanisScribe::App::TrayAction::Quit:
                // 退出交给事件循环：托盘图标随对象析构消失，这里不自己 quit 以免绕开析构里的日志冲刷
                QCoreApplication::exit(0);
                return;
        }
    }

    GyanisScribe::Core::SettingsStore &AppController::settings() const noexcept
    {
        return *m_settings;
    }

    GyanisScribe::Gui::ThemeManager &AppController::theme() const noexcept
    {
        return *m_theme;
    }

    GyanisScribe::Gui::IconProvider &AppController::icons() const noexcept
    {
        return *m_icons;
    }

    void AppController::refreshPinWithScreenUnder(const int index)
    {
        const auto target = m_pins->pinRefreshTarget(index);
        if (!target)
        {
            // 缩到小图与动图那两档按 F5 就该说清为什么不动：不出声就成了「按键坏了」
            notifyUser(QString::fromUtf8(target.error().message.c_str()), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        auto *screen = GyanisScribe::Core::screenAtGlobalPoint(target->center());
        if (screen == nullptr)
        {
            notifyUser(GyanisScribe::Base::uiText("这块屏幕已经不在了，重抓不到画面。把贴图挪回还在的屏幕上再刷新"), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }

        // 全局逻辑坐标换成那一块屏内的坐标：抓帧那一路按「哪块屏 + 屏内区域」取图，与标注窗的 F5 同一个口子
        const QRect   local      = target->translated(-screen->geometry().topLeft());
        const QString screenName = screen->name();
        const auto    frame      = m_pins->grabWithPinHidden(index, [this, local, screenName] { return m_capture->grabRegionNow(local, screenName); });
        if (!frame)
        {
            notifyUser(QString::fromUtf8(frame.error().message.c_str()), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }

        // 换内容仍走那一个出口：动图与不在册那两道拒绝只有一份判据
        auto pixels = frame->image;
        // 抓回来的图按物理像素交回（dpr 被清成 1），而贴图的显示尺寸是「像素 ÷ dpr」折出来的：
        // 不把这块屏的 dpr 带回去，刷新一次就把这张图放大 dpr 倍（1.5 倍屏上肉眼可见地跳大）
        pixels.setDevicePixelRatio(frame->devicePixelRatio > 0.0 ? frame->devicePixelRatio : 1.0);
        const auto replaced = m_pins->replacePinContent(index, pixels);
        if (!replaced)
        {
            notifyUser(QString::fromUtf8(replaced.error().message.c_str()), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        // 成功不弹提示：按住 F5 连着刷时，每一下弹一条会把要看的画面挡掉（标注窗那条 F5 也是这个做法）
        GyanisScribe::Base::Logger::info(std::format("贴图已重抓这块屏幕：{}×{} 像素", frame->image.width(), frame->image.height()));
    }

    void AppController::notifyUser(const QString &message, const GyanisScribe::Gui::ToastKind kind)
    {
        if (m_toast != nullptr)
        {
            m_toast->show(message, kind);
            return;
        }
        // 气泡还没建起来的那一段（构造早期）不该把话吞掉：日志是兜底出口
        GyanisScribe::Base::Logger::info(std::format("提示：{}", message.toStdString()));
    }

} // namespace GyanisScribe::Main
