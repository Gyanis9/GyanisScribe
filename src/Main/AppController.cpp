#include "Main/AppController.h"

#include "Core/Annotation/AnnotationEngine.h"
#include "Gui/Annotation/AnnotationToolbar.h"
#include "Gui/Annotation/AnnotationWindow.h"
#include "Core/Annotation/BlankCanvas.h"
#include "App/ClipboardPin.h"
#include "App/CustomHotkeys.h"
#include "App/BugReportCoordinator.h"
#include "App/HistoryCoordinator.h"
#include "App/MemoryCoordinator.h"
#include "App/DeliveryCoordinator.h"
#include "App/CaptureCoordinator.h"
#include "App/RewindCoordinator.h"
#include "App/SceneCleanupGuard.h"
#include "App/PinCoordinator.h"
#include "App/PickCoordinator.h"
#include "App/OcrCoordinator.h"
#include "App/TiledPrivacyScanner.h"
#include "App/RecordingCoordinator.h"
#include "App/StepRecorderCoordinator.h"
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
#include "Core/Memory/MemoryIndex.h"
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
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QCursor>
#include <QGuiApplication>
#include <QMessageBox>
#include <QMimeData>
#include <QPageLayout>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QScopeGuard>
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
    /// 截图、录屏与回溯共用同一块桌面时挡回后来者的那一句：两个入口各写一遍就会有一处改漏
    QString desktopBusyMessage()
    {
        return GyanisScribe::Base::uiText("先结束当前截图/录屏/回溯");
    }
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


    AppController::AppController(const bool restorePins, QObject *parent) : AppController(AppControllerParts{}, restorePins, parent)
    {
    }

    AppController::AppController(AppControllerParts parts, const bool restorePins, QObject *parent) :
        AppController(parts.settings ? std::move(parts.settings) : createSettingsStore(),
                      parts.integration ? std::move(parts.integration) : GyanisScribe::Platform::createPlatformIntegration(),
                      parts.captureSource ? std::move(parts.captureSource) : GyanisScribe::Plugins::createScreenCaptureSource(),
                      parts.historyStoragePath.empty() ? GyanisScribe::Core::HistoryStore::defaultStoragePath() : std::move(parts.historyStoragePath),
                      parts.memoryStoragePath.empty() ? GyanisScribe::Core::MemoryIndex::defaultStoragePath() : std::move(parts.memoryStoragePath),
                      std::move(parts.stepEventSourceFactory), restorePins, parent)
    {
    }

    AppController::AppController(std::unique_ptr<GyanisScribe::Core::SettingsStore> settings, std::unique_ptr<GyanisScribe::Platform::IPlatformIntegration> integration,
                                 std::unique_ptr<GyanisScribe::Core::IScreenCaptureSource> captureSource, std::filesystem::path historyStoragePath,
                                 std::filesystem::path memoryStoragePath, std::function<std::unique_ptr<GyanisScribe::Platform::IStepEventSource>()> stepEventSourceFactory,
                                 const bool restorePins, QObject *parent) :
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
        // 全应用窗口图标：分享、审核、输入框这些对话框不必各自记得设一次，任务栏与标题栏都有品牌图标
        QApplication::setWindowIcon(m_icons->brandIcon());
        QObject::connect(m_theme.get(), &GyanisScribe::Gui::ThemeManager::themeChanged, this, [this] { m_icons->setTintColor(m_theme->iconTint()); });
        // 第一次生效必然带一次通知（构造时解算位是「还没落过」的哨兵），描边色就在通知里落定
        m_theme->applyFromSettings();

        // 焦点环覆盖层排在主题之后：析构顺序反过来，它先于主题消失，绘制时读到的色板始终有效
        m_focusRing = std::make_unique<GyanisScribe::Gui::FocusRingController>([this] { return m_theme->palette(); });
        m_focusRing->setThemeKind(m_theme->resolvedKind());
        QObject::connect(m_theme.get(), &GyanisScribe::Gui::ThemeManager::themeChanged, m_focusRing.get(), [this] { m_focusRing->setThemeKind(m_theme->resolvedKind()); });
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
        // 隐私扫描这一族只做「一张图的敏感信息检查」：分带扫描器把长图切带逐带认，收尾交回原图与合并结果。
        // 它自己选后端、自己单飞；失败原因由这一层出气泡并留一条日志，结果要落到画布上，
        // 所以接下来那两根线接在截图协调器那边（它才拿得到标注窗与引擎）
        m_tiledPrivacy = std::make_unique<GyanisScribe::App::TiledPrivacyScanner>(*m_settings, *m_integration);
        QObject::connect(
                m_tiledPrivacy.get(), &GyanisScribe::App::TiledPrivacyScanner::scanFailed, this,
                [this](const GyanisScribe::Base::Error &error)
                {
                    GyanisScribe::Base::Logger::error(std::format("隐私扫描失败：{}", error.message));
                    notifyUser(QString::fromUtf8(
                                       GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("敏感信息检查失败：{}。请重试一次，或换一种语言再试"), error.message).c_str()),
                               GyanisScribe::Gui::ToastKind::Danger);
                });
        // 报告这一族的报告窗、自动打码与环境组装都在协调器里；截图协调器负责把这一轮的画布与来源交给它
        m_bugReport    = std::make_unique<GyanisScribe::App::BugReportCoordinator>(*m_settings, *m_integration);
        m_tray         = std::make_unique<GyanisScribe::App::TrayController>(*m_icons, *m_theme, *m_settings, *m_integration);
        m_historyStore = std::make_unique<GyanisScribe::Core::HistoryStore>(std::move(historyStoragePath));
        // 记忆协调器要排在主窗口之前：窗口的「记忆库」页拿索引的引用。这时还不 start()，
        // 等窗口与信号都接好再开库，免得启动期的通知打在还没接线的页上
        m_memory     = std::make_unique<GyanisScribe::App::MemoryCoordinator>(*m_settings, *m_integration, std::move(memoryStoragePath));
        m_mainWindow = std::make_unique<MainWindow>(*m_settings, *m_theme, *m_icons, *m_historyStore, m_memory->index(), m_integration->capabilities(), *m_integration);
        // 历史这一族整族搬进了 HistoryCoordinator：清单本体仍由这里持有并与主窗口共享，
        // 协调器只拿引用；「清单变了要刷新窗口」由 changed() 回到这一层接——它不能自己去叫 Main，
        // 那会让 App 层反向依赖 Main，而 Main 本来就依赖 App
        m_history = std::make_unique<GyanisScribe::App::HistoryCoordinator>(*m_historyStore, *m_settings);
        QObject::connect(m_history.get(), &GyanisScribe::App::HistoryCoordinator::messageForUser, this, &AppController::notifyUser);
        QObject::connect(m_history.get(), &GyanisScribe::App::HistoryCoordinator::changed, this, [this] { m_mainWindow->refreshHistory(); });
        // 历史是记忆库的来路：一条截图进了历史才建索引；删一条或清空按同一条口径删索引行。
        // 只认截图：录屏成品不是「主动截的图」，不进记忆库
        QObject::connect(
                m_history.get(), &GyanisScribe::App::HistoryCoordinator::recorded, this,
                [this](const std::filesystem::path &filePath, const GyanisScribe::Core::HistoryKind kind, const QSize &size, const GyanisScribe::Core::CaptureSourceInfo &source)
                {
                    if (kind != GyanisScribe::Core::HistoryKind::Screenshot)
                    {
                        return;
                    }
                    m_memory->noteScreenshot(filePath, size, source);
                });
        QObject::connect(m_history.get(), &GyanisScribe::App::HistoryCoordinator::removed, m_memory.get(), &GyanisScribe::App::MemoryCoordinator::removePath);
        QObject::connect(m_history.get(), &GyanisScribe::App::HistoryCoordinator::cleared, m_memory.get(), &GyanisScribe::App::MemoryCoordinator::clearCaptureEntries);

        // 记忆库的状态与动作：状态从协调器推到窗口，动作从窗口交回协调器；两边都不认识对方的内部
        QObject::connect(m_memory.get(), &GyanisScribe::App::MemoryCoordinator::messageForUser, this, &AppController::notifyUser);
        QObject::connect(m_memory.get(), &GyanisScribe::App::MemoryCoordinator::changed, m_mainWindow.get(), &MainWindow::refreshMemory);
        // 统计与列表同一次刷新：页脚读数不能比列表晚一拍，否则删完最后一条时阅读区是空的、页脚还写着 1
        QObject::connect(m_memory.get(), &GyanisScribe::App::MemoryCoordinator::statsChanged, m_mainWindow.get(), &MainWindow::refreshMemory);
        QObject::connect(m_memory.get(), &GyanisScribe::App::MemoryCoordinator::databaseError, m_mainWindow.get(), &MainWindow::setMemoryDatabaseError);
        QObject::connect(m_memory.get(), &GyanisScribe::App::MemoryCoordinator::ocrAvailabilityChanged, m_mainWindow.get(), &MainWindow::setMemoryOcrAvailability);
        QObject::connect(m_memory.get(), &GyanisScribe::App::MemoryCoordinator::importProgress, m_mainWindow.get(), &MainWindow::setMemoryImportProgress);
        QObject::connect(m_memory.get(), &GyanisScribe::App::MemoryCoordinator::importFinished, m_mainWindow.get(), &MainWindow::hideMemoryImportProgress);
        QObject::connect(m_memory.get(), &GyanisScribe::App::MemoryCoordinator::consentRequested, this, &AppController::showMemoryConsent);
        // 进到记忆库页那一下先清掉原图已不在的行再重读列表：导航行、托盘与 open() 都经过窗口的那一条信号，
        // 因此不必每个入口各写一遍
        QObject::connect(m_mainWindow.get(), &MainWindow::memoryPageShown, this,
                         [this]
                         {
                             m_memory->pruneMissing();
                             // 一条都没删时 pruneMissing 不发 changed，列表仍按当前索引重读一次
                             m_mainWindow->refreshMemory();
                         });
        QObject::connect(m_mainWindow.get(), &MainWindow::memoryPinRequested, m_history.get(), &GyanisScribe::App::HistoryCoordinator::requestPin);
        QObject::connect(m_mainWindow.get(), &MainWindow::memoryOpenFolderRequested, m_history.get(), &GyanisScribe::App::HistoryCoordinator::openContainingFolder);
        QObject::connect(m_mainWindow.get(), &MainWindow::memoryOpenFileRequested, this,
                         [this](const std::filesystem::path &filePath)
                         {
                             if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(filePath.wstring()))))
                             {
                                 notifyUser(GyanisScribe::Base::uiText("无法打开该文件：文件可能已被移动或删除。请刷新记忆库后重试"), GyanisScribe::Gui::ToastKind::Danger);
                             }
                         });
        QObject::connect(m_mainWindow.get(), &MainWindow::memoryRetryOcrRequested, m_memory.get(), &GyanisScribe::App::MemoryCoordinator::retryOcr);
        // 视图只发主键：按主键找回路径再删，删除口径与历史那一路同为「按路径删行」
        QObject::connect(m_mainWindow.get(), &MainWindow::memoryRemoveRequested, this,
                         [this](const qint64 id)
                         {
                             const auto found = m_memory->index().findById(id);
                             if (!found || !found->has_value())
                             {
                                 return;
                             }
                             m_memory->removePath((**found).filePath);
                         });
        // 文件对话框要人点：选好目录以后的那一段在 importMemoryFolder 里，用例直接驱动那里
        QObject::connect(m_mainWindow.get(), &MainWindow::memoryImportRequested, this,
                         [this]
                         {
                             const auto picked = QFileDialog::getExistingDirectory(m_mainWindow.get(), GyanisScribe::Base::uiText("导入旧截图"), QDir::homePath());
                             if (picked.isEmpty())
                             {
                                 return;
                             }
                             importMemoryFolder(GyanisScribe::Base::Paths::fromDisplayText(picked));
                         });
        // 清空的确认框在记忆视图那一侧已经点过：这里收到的是确认之后的请求
        QObject::connect(m_mainWindow.get(), &MainWindow::memoryClearRequested, m_memory.get(), &GyanisScribe::App::MemoryCoordinator::clearAll);
        QObject::connect(m_mainWindow.get(), &MainWindow::memoryExcludeRequested, this,
                         [this](QStringList processes)
                         {
                             m_memory->setExcludedProcesses(processes);
                             // 协调器保存前会归一（去 .exe、小写、去重），回给视图的是归一后的那一份
                             m_mainWindow->setMemoryExcludedProcesses(m_memory->excludedProcesses());
                         });
        QObject::connect(m_mainWindow.get(), &MainWindow::memoryEnableRequested, this,
                         [this]
                         {
                             m_memory->setEnabled(true);
                             m_mainWindow->setMemoryEnabled(true);
                         });
        QObject::connect(m_mainWindow.get(), &MainWindow::memoryRebuildRequested, m_memory.get(), &GyanisScribe::App::MemoryCoordinator::rebuildIndex);
        QObject::connect(m_mainWindow.get(), &MainWindow::memoryRetryPendingRequested, m_memory.get(), &GyanisScribe::App::MemoryCoordinator::retryPending);
        QObject::connect(m_mainWindow.get(), &MainWindow::memoryImportCancelRequested, m_memory.get(), &GyanisScribe::App::MemoryCoordinator::cancelImport);
        QObject::connect(m_mainWindow.get(), &MainWindow::memoryEnabledToggled, this,
                         [this](const bool enabled)
                         {
                             m_memory->setEnabled(enabled);
                             // 协调器落账后把勾选与横幅摆回同一状态：写失败时界面不能比配置更乐观
                             m_mainWindow->setMemoryEnabled(m_memory->isEnabled());
                         });

        // 启动状态先摆一次：勾选、横幅与排除名单都不从协调器的私有字段里猜
        m_mainWindow->setMemoryEnabled(m_memory->isEnabled());
        m_mainWindow->setMemoryExcludedProcesses(m_memory->excludedProcesses());
        m_mainWindow->setMemoryOcrAvailability(true, QString{});
        // 窗口与信号都接完再开库：start() 里的 pruneMissing、统计与待识别恢复都会发信号
        m_memory->start();
        // 交付这一族也搬进了协调器：控制器只留提示与「命令行那条会话状态」的接线
        m_delivery = std::make_unique<GyanisScribe::App::DeliveryCoordinator>(*m_settings, *m_history);
        QObject::connect(m_delivery.get(), &GyanisScribe::App::DeliveryCoordinator::messageForUser, this, &AppController::notifyUser);
        // 贴图那一族的地盘：贴到哪儿（光标）由这一层决定，协调器只交图与来源
        QObject::connect(m_history.get(), &GyanisScribe::App::HistoryCoordinator::pinRequested, this,
                         [this](const QImage &image, const QString &sourceFile) { m_pins->addPin(image, QCursor::pos(), sourceFile); });
        m_toast = std::make_unique<GyanisScribe::Gui::Toast>(*m_icons, *m_theme);
        QObject::connect(m_tray.get(), &GyanisScribe::App::TrayController::actionRequested, this, &AppController::handleTrayAction);

        // 上次若在清场态里退出，先把桌面还回去再继续启动：此刻托盘与主窗口都还没对外现身。
        // 恢复失败只说一句、不阻断启动，标记留着供下次再试
        if (!m_settings->stringValue(GyanisScribe::Core::kSceneClearMarker, QString{}).isEmpty())
        {
            if (!m_integration->capabilities().sceneCleanup)
            {
                // 标记只可能来自别的机器拷来的配置：这台平台连清场都做不了，恢复无从谈起；
                // 留着标记只会让每次启动都空走一遍，直接清掉
                if (const auto dropped = m_settings->setValue(GyanisScribe::Core::kSceneClearMarker, QVariant{}); !dropped)
                {
                    GyanisScribe::Base::Logger::warning(std::format("本平台不支持清场截图，残留的清场标记也没清掉：{}", dropped.error().message));
                } else
                {
                    GyanisScribe::Base::Logger::info("本平台不支持清场截图，残留的清场标记已清掉");
                }
            } else
            {
                GyanisScribe::App::SceneCleanupGuard guard{*m_integration, *m_settings};
                if (const auto restored = guard.restore(); !restored)
                {
                    notifyUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(
                                                        GyanisScribe::Base::uiTextUtf8("上次退出时桌面处于清场状态，启动时自动恢复失败：{}。"
                                                                                       "请重启程序再试；若桌面图标仍不显示，可在桌面右键菜单的「查看」里重新勾选「显示桌面图标」；"
                                                                                       "若窗口没有回来，按一次 Win+D"),
                                                        restored.error().message)
                                                        .c_str()),
                               GyanisScribe::Gui::ToastKind::Danger);
                } else
                {
                    notifyUser(GyanisScribe::Base::uiText("上次退出时桌面处于清场状态，已自动恢复；若窗口没有回来，按一次 Win+D"), GyanisScribe::Gui::ToastKind::Information);
                }
            }
        }

        // 托盘在这一步装起：菜单动作那条线已经接好，逐屏截图那一条要等截图协调器建出来才接（见下）；
        // 装没装上的读数先决定主窗口动作条收不收，没有托盘时还要让主窗口自己现身（见下）
        const bool trayInstalled = m_tray->install();
        // 托盘在场时顶部动作条就是第二份入口：整条收起，逐屏截图改走托盘子菜单
        m_mainWindow->setTrayMenuAvailable(trayInstalled);

        // 热键 → 动作：唯一的接线处，界面加菜单项时也是发同一个信号而不是再走一条路径
        QObject::connect(m_hotkeys.get(), &GyanisScribe::Core::IHotkeyManager::actionTriggered, this,
                         [this](const GyanisScribe::Core::HotkeyAction action) { handleHotkeyAction(action); });

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
        // 设置页改固定组合时由热键层自己重登，那一路没人接返回值：失败项从这条信号回来。
        // 启动与自定义键变更走 registerHotkeys()，它已经当面提示过，这里不会再弹第二遍
        QObject::connect(m_hotkeys.get(), &GyanisScribe::Core::IHotkeyManager::reloadFailed, this,
                         [this](const GyanisScribe::Core::HotkeyReloadReport &report) { reportHotkeyFailures(report); });
        // 自定义键不在热键层那八个的订阅里：改完这一族的任何一格，由这里重登一次，不必重启
        QObject::connect(m_settings.get(), &GyanisScribe::Core::ISettingsStore::valueChanged, this,
                         [this](const QString &key)
                         {
                             if (m_restoringHotkeySettings)
                             {
                                 // 写回恢复的旧组合也会发自定义键的变更通知：再登一遍会把刚恢复的登记又翻掉
                                 return;
                             }
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
                *m_settings, *m_integration, *m_theme, *m_icons, *m_captureSource, *m_ocr, *m_pinCoordinator, *m_delivery, *m_history, *m_bugReport});
        QObject::connect(m_capture.get(), &GyanisScribe::App::CaptureCoordinator::messageForUser, this, &AppController::notifyUser);
        QObject::connect(m_capture.get(), &GyanisScribe::App::CaptureCoordinator::commandLineActionFailed, this, &AppController::commandLineActionFailed);
        QObject::connect(m_capture.get(), &GyanisScribe::App::CaptureCoordinator::commandLineCaptureDelivered, this, &AppController::commandLineCaptureDelivered);
        // 多屏时托盘子菜单里那几条「整屏截图：<屏名>」不走 handleTrayAction：屏名信号直接交给同一个门禁出口
        QObject::connect(m_tray.get(), &GyanisScribe::App::TrayController::screenCaptureRequested, this, &AppController::captureScreenFromMenu);
        // 隐私打码的两根线：标注窗的入口发起分带扫描，扫描成功交回截图协调器认码、审核、落笔。
        // 标注窗不关，结果回来之前用户还能接着改；过期画布由 handlePrivacyScan 自己挡
        QObject::connect(&m_capture->annotationWindow(), &GyanisScribe::Gui::AnnotationWindow::privacyRequested, m_tiledPrivacy.get(),
                         &GyanisScribe::App::TiledPrivacyScanner::start);
        QObject::connect(m_tiledPrivacy.get(), &GyanisScribe::App::TiledPrivacyScanner::scanSucceeded, m_capture.get(), &GyanisScribe::App::CaptureCoordinator::handlePrivacyScan);
        // 托盘那一族的逐屏截图接在协调器建好之后：连在尚未构造的成员上时 QObject::connect 不报错，
        // 托盘图标与菜单都照常现身，点下去却什么都不会发生

        // 回溯这一族排在截图协调器之后：缓冲与选择器都由它自己管，控制器只接四条线——
        // 选中的帧进同一条截图链路、要说的话回到提示、缓冲状态推给托盘、流程收场恢复抓帧。
        // start() 自己读设置与平台能力位：默认关闭，未开启时停在 Disabled，不抓帧也不占托盘那一段。
        // 编码器传空：录屏协调器排在后面，此时还没有可共享的实例；回溯在首次导出时自建 ffmpeg 编码器
        m_rewind = std::make_unique<GyanisScribe::App::RewindCoordinator>(
                GyanisScribe::App::RewindCoordinator::Dependencies{*m_captureSource, *m_integration, *m_settings, {}});
        QObject::connect(m_rewind.get(), &GyanisScribe::App::RewindCoordinator::frameChosen, m_capture.get(), &GyanisScribe::App::CaptureCoordinator::startInteractiveWithFrame);
        QObject::connect(m_rewind.get(), &GyanisScribe::App::RewindCoordinator::messageForUser, this, &AppController::notifyUser);
        QObject::connect(m_rewind.get(), &GyanisScribe::App::RewindCoordinator::bufferStatusChanged, m_tray.get(), &GyanisScribe::App::TrayController::setRewindStatus);
        QObject::connect(m_capture.get(), &GyanisScribe::App::CaptureCoordinator::captureFlowFinished, m_rewind.get(),
                         &GyanisScribe::App::RewindCoordinator::handleCaptureFlowFinished);
        m_rewind->start();

        // 步骤记录排在回溯之后：主题、设置、平台与抓屏来源都已建好，事件源由注入口决定真假，
        // 首次说明留空让协调器按生产那一套弹窗；会话目录落在未保存归档下的 Steps，启动时回收过期的
        GyanisScribe::App::StepRecorderDependencies stepDependencies{
                *m_settings, *m_integration, *m_captureSource, *m_theme, std::move(stepEventSourceFactory), {}, GyanisScribe::Base::Paths::unsavedCaptureDirectory() / "Steps"};
        m_stepRecorder = std::make_unique<GyanisScribe::App::StepRecorderCoordinator>(std::move(stepDependencies));
        QObject::connect(m_stepRecorder.get(), &GyanisScribe::App::StepRecorderCoordinator::messageForUser, this, &AppController::notifyUser);
        // 状态镜像供互斥门禁读：控制条可以收起，光看窗口可见性判不出「还在录」
        QObject::connect(m_stepRecorder.get(), &GyanisScribe::App::StepRecorderCoordinator::stateChanged, this,
                         [this](const bool recording, const bool, const int) { m_stepRecording = recording; });

        // 录屏这一族整族搬进了 RecordingCoordinator：控制器只留三根线——
        // 要说给用户的话、托盘那一行读数、成品归档进历史
        m_recording = std::make_unique<GyanisScribe::App::RecordingCoordinator>(*m_captureSource, *m_integration, *m_settings, *m_icons);
        QObject::connect(m_recording.get(), &GyanisScribe::App::RecordingCoordinator::messageForUser, this, &AppController::notifyUser);
        QObject::connect(m_recording.get(), &GyanisScribe::App::RecordingCoordinator::recordingStatusChanged, this,
                         [this](const int seconds, const bool paused)
                         {
                             // 控制条可以收进托盘，可见性不是「在录」的真值；负数只在这一轮收场时发
                             m_recordingActive = seconds >= 0;
                             m_tray->setRecordingStatus(seconds, paused);
                         });
        QObject::connect(m_recording.get(), &GyanisScribe::App::RecordingCoordinator::finished, this, [this](const QString &outputPath, const QSize &frameSize)
                         { m_history->record(GyanisScribe::Base::Paths::fromDisplayText(outputPath), GyanisScribe::Core::HistoryKind::Recording, frameSize); });

        // 历史页的三个出口：贴图与识别都要先把成品读回内存，删除只动清单不动文件
        QObject::connect(m_mainWindow.get(), &MainWindow::historyPinRequested, m_history.get(), &GyanisScribe::App::HistoryCoordinator::requestPin);
        QObject::connect(m_mainWindow.get(), &MainWindow::historyOcrRequested, m_ocr.get(), &GyanisScribe::App::OcrCoordinator::recognizeFile);
        QObject::connect(m_mainWindow.get(), &MainWindow::historyRemoveRequested, m_history.get(), &GyanisScribe::App::HistoryCoordinator::remove);
        QObject::connect(m_mainWindow.get(), &MainWindow::historyOpenFolderRequested, m_history.get(), &GyanisScribe::App::HistoryCoordinator::openContainingFolder);
        QObject::connect(m_mainWindow.get(), &MainWindow::historyClearRequested, m_history.get(), &GyanisScribe::App::HistoryCoordinator::clear);
        QObject::connect(m_mainWindow.get(), &MainWindow::messageForUser, this, [this](const QString &message, GyanisScribe::Gui::ToastKind kind) { notifyUser(message, kind); });
        // 主窗口动作条与托盘菜单发的是同一个枚举，所以出口也只有这一个 handleTrayAction
        QObject::connect(m_mainWindow.get(), &MainWindow::actionRequested, this, &AppController::handleTrayAction);
        // 「整屏截图：<屏名>」也走逐屏截图的同一个出口：步骤记录期间与其余截图入口同门禁
        QObject::connect(m_mainWindow.get(), &MainWindow::captureScreenRequested, this, &AppController::captureScreenFromMenu);
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
                                 notifyUser(GyanisScribe::Base::uiText("该贴图已移至当前分组，位置保持不变"), GyanisScribe::Gui::ToastKind::Information);
                                 return;
                             }
                             // 热键文本从设置现读：用户改过组合键之后，提示里那个键还得是按得动的那个
                             notifyUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("贴图已移至{}，已暂时收起；按 {} 或从托盘切换到该分组即可显示"),
                                                                                       m_pinCoordinator->groupLabel(group).toStdString(),
                                                                                       m_hotkeys->bindingText(GyanisScribe::Core::HotkeyAction::NextPinGroup).toStdString())
                                                                  .c_str()),
                                        GyanisScribe::Gui::ToastKind::Information);
                         });
        // 改组名与删组都在管理器落账，这两句是它们的回声：菜单早就收起来了，不出声就成了「敲了字什么也没变」
        QObject::connect(m_pins.get(), &GyanisScribe::Gui::PinManager::pinGroupRenamed, m_pinCoordinator.get(), &GyanisScribe::App::PinCoordinator::reportGroupRenamed);
        QObject::connect(m_pins.get(), &GyanisScribe::Gui::PinManager::pinGroupDeleted, m_pinCoordinator.get(), &GyanisScribe::App::PinCoordinator::reportGroupDeleted);

        // 启动把上一次会话留下的那几张摆回来；退出前把在存的这批连像素一起记下。
        // 一次性命令行的装配不接管桌面上那批贴图：既不摆回来，也不在手上一张都没有时把布局写成空的
        if (restorePins)
        {
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
        }

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

    bool AppController::rejectWhileStepRecorderBusy()
    {
        // 状态镜像与协调器的活状态都判：镜像一般先到，协调器这一条兜住信号没来得及回到槽里的那一下
        if (!m_stepRecording && !m_stepRecorder->isRecording() && !m_stepRecorder->isReviewOpen())
        {
            return false;
        }
        notifyUser(GyanisScribe::Base::uiText("先停止步骤记录或处理完审核窗"), GyanisScribe::Gui::ToastKind::Information);
        return true;
    }

    void AppController::captureScreenFromMenu(const QString &screenName)
    {
        // 屏名这一路绕开了托盘动作的开关，门禁要在这里补上：录制中它同样会铺出标注窗并截进步骤
        if (rejectWhileStepRecorderBusy())
        {
            return;
        }
        m_capture->captureScreen(screenName);
    }

    void AppController::handleHotkeyAction(const GyanisScribe::Core::HotkeyAction action)
    {
        switch (action)
        {
            case GyanisScribe::Core::HotkeyAction::Capture:
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                m_capture->startInteractive();
                return;
            case GyanisScribe::Core::HotkeyAction::Record:
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                m_recording->toggle();
                return;
            case GyanisScribe::Core::HotkeyAction::CaptureLong:
                // 与托盘那一行同一条路：热键不该有一条自己的截图逻辑
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                m_capture->startLong();
                return;
            case GyanisScribe::Core::HotkeyAction::Pick:
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                m_picker->start();
                return;
            case GyanisScribe::Core::HotkeyAction::Ocr:
                // 识别热键：截完直接进结果窗口，中间不弹标注窗
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
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
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                m_capture->startInteractive(GyanisScribe::App::CaptureIntent::QuickSave);
                return;
            case GyanisScribe::Core::HotkeyAction::Rewind:
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                // 回溯选择器会再铺一层遮罩：当前截图或标注还没收场就打开，
                // 遮罩会把用户正在标注的那一窗拍进去，所以先挡住并说清出路。
                // 整屏、上次选区与空白画布这几路不开流程标志，标注窗已经立起来时同样算没收场；
                // 延时截图还在倒计时时流程标志也还没立，但再过几秒遮罩就会铺满屏，同样要挡
                if (m_capture->isCaptureFlowActive() || m_capture->isDelayPending() || m_capture->annotationWindow().isVisible())
                {
                    notifyUser(GyanisScribe::App::rewindBusyMessage(), GyanisScribe::Gui::ToastKind::Information);
                    return;
                }
                m_rewind->showPicker();
                return;
            case GyanisScribe::Core::HotkeyAction::Steps:
                // 与托盘那一行只有一条实现：起停判定、互斥门禁与提示文案都不在这一层抄第二份
                handleTrayAction(GyanisScribe::App::TrayAction::ToggleSteps);
                return;
            case GyanisScribe::Core::HotkeyAction::PickElement:
                // 与托盘那一行只有一条实现：互斥门禁在托盘动作那一段判；能力由托盘菜单按 uiElements 过滤，
                // CaptureService::startElementPick 入口还会再挡一次，热键这一路不必自己判
                handleTrayAction(GyanisScribe::App::TrayAction::PickElement);
                return;
            case GyanisScribe::Core::HotkeyAction::SceneClear:
                // 与托盘那一行只有一条实现：步骤记录门禁与平台能力都只在托盘动作那一段判
                handleTrayAction(GyanisScribe::App::TrayAction::SceneClearCapture);
                return;
        }
    }

    GyanisScribe::Base::Expected<GyanisScribe::Core::HotkeyReloadReport> AppController::registerHotkeys()
    {
        // 先把手绑的那一批交给热键层再重登：登记要的是「这一批」，中间态不必单独登一次
        m_hotkeys->setCustomBindings(GyanisScribe::App::collectCustomBindings(*m_settings));
        auto report = m_hotkeys->reload();
        if (report)
        {
            reportHotkeyFailures(*report);
        }
        return report;
    }

    void AppController::reportHotkeyFailures(const GyanisScribe::Core::HotkeyReloadReport &report)
    {
        // 先把恢复成功的旧组合写回配置：设置页与重启都只认配置，不写回就仍停在失败的新文本上。
        // 写回期间压住自定义键那一路的重登，否则刚恢复的登记会被再翻一遍。
        if (!report.restored.empty())
        {
            m_restoringHotkeySettings = true;
            // 中途抛出也要把标志复位：永久留在 true 会吞掉之后所有自定义键变更的重登
            const auto resetRestoring = qScopeGuard([this] { m_restoringHotkeySettings = false; });
            m_hotkeys->persistRestoredBindings(report);
        }
        if (!report.failures.empty())
        {
            // 登记结果只写进日志的话，用户看到的就只是「按了没反应」：要当场说出来，
            // 而且把每一条组合键与它各自的原因一起说——四条未必是同一个理由。
            QStringList lines;
            for (const auto &failure: report.failures)
            {
                lines << QStringLiteral("%1（%2）").arg(failure.combination, QString::fromStdString(failure.error.message));
            }
            notifyUser(GyanisScribe::Base::uiText("以下全局快捷键注册失败：") + lines.join(QStringLiteral("；")), GyanisScribe::Gui::ToastKind::Danger);
        }
        if (!report.customFailures.empty())
        {
            // 自定义那一批单独报一句：这里要点的是功能名（「整屏截图」），只报组合键没人知道是哪件事没绑上
            QStringList lines;
            for (const auto &failure: report.customFailures)
            {
                const auto bound = GyanisScribe::App::trayActionForToken(failure.token);
                const auto label = bound ? GyanisScribe::App::bindableActionLabel(*bound) : failure.token;
                lines << QStringLiteral("%1（%2）").arg(label + QStringLiteral(" ") + failure.combination, QString::fromStdString(failure.error.message));
            }
            notifyUser(GyanisScribe::Base::uiText("以下自定义快捷键注册失败：") + lines.join(QStringLiteral("；")), GyanisScribe::Gui::ToastKind::Danger);
        }
    }

    void AppController::openMainWindow(const MainWindowPage page)
    {
        // 记忆库页的清理与重读挂在 MainWindow::memoryPageShown 上：导航行直接切页也走同一处
        m_mainWindow->open(page);
    }

    void AppController::startColorPick()
    {
        // 取色这一族在 PickCoordinator 里，这里只转发：Application 那条命令行 --pick 入口认的是控制器
        m_picker->start();
    }

    void AppController::importMemoryFolder(const std::filesystem::path &directory)
    {
        // 导入这一族在 MemoryCoordinator 里，这里只转发：文件对话框选好目录之后走的也是这一条
        m_memory->importFolder(directory);
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
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                m_capture->startInteractive();
                return;
            case GyanisScribe::App::TrayAction::CaptureFullScreen:
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                m_capture->startFullScreen();
                return;
            case GyanisScribe::App::TrayAction::CaptureLastRegion:
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                m_capture->startLastRegion();
                return;
            case GyanisScribe::App::TrayAction::CaptureWindow:
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                m_capture->captureForegroundWindow();
                return;
            case GyanisScribe::App::TrayAction::CaptureLongRegion:
                // 长截图的入口只做「去框那一条区域」，接与收口都在截图服务里
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                m_capture->startLong();
                return;
            case GyanisScribe::App::TrayAction::SceneClearCapture:
                // 清场也是铺同一层框选遮罩：步骤记录在跑时同样要先挡住，免得把遮罩记进步骤
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                // 与元素拾取同一套互斥门禁：清场会改桌面并把遮罩铺满屏，
                // 截图、录屏与回溯任一件没结束都先挡住
                if (m_capture->isCaptureFlowActive() || m_capture->isDelayPending() || m_capture->annotationWindow().isVisible() || m_recordingActive ||
                    m_rewind->isPickerVisible())
                {
                    notifyUser(desktopBusyMessage(), GyanisScribe::Gui::ToastKind::Information);
                    return;
                }
                m_capture->startSceneClearCapture();
                return;
            case GyanisScribe::App::TrayAction::BlankCanvas:
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                m_capture->startBlankCanvas();
                return;
            case GyanisScribe::App::TrayAction::PickElement:
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                // 元素拾取会铺满屏一层遮罩并走截图流程：截图、录屏与回溯任一件没结束都先挡住。
                // 整屏、上次选区与空白画布这几路不开流程标志，标注窗已经立起来时同样算没收场；
                // 延时截图还在倒计时时流程标志也还没立，但再过几秒遮罩就会铺满屏
                if (m_capture->isCaptureFlowActive() || m_capture->isDelayPending() || m_capture->annotationWindow().isVisible() || m_recordingActive ||
                    m_rewind->isPickerVisible())
                {
                    notifyUser(desktopBusyMessage(), GyanisScribe::Gui::ToastKind::Information);
                    return;
                }
                m_capture->startElementPick();
                return;
            case GyanisScribe::App::TrayAction::RepinClosed:
                m_pinCoordinator->repinLastClosed();
                return;
            case GyanisScribe::App::TrayAction::ToggleRecord:
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                m_recording->toggle();
                return;
            case GyanisScribe::App::TrayAction::ToggleSteps:
                // 先在录或暂停：这一按就是停止，停止不该被任何互斥门禁挡住
                if (m_stepRecording || m_stepRecorder->isRecording())
                {
                    m_stepRecorder->toggleRecording();
                    return;
                }
                // 截图、录屏与回溯都在占用桌面：控制条压上去会把它们记进步骤或把遮罩拍进截图
                if (m_capture->isCaptureFlowActive() || m_capture->isDelayPending() || m_capture->annotationWindow().isVisible() || m_recordingActive ||
                    m_rewind->isPickerVisible())
                {
                    notifyUser(desktopBusyMessage(), GyanisScribe::Gui::ToastKind::Information);
                    return;
                }
                m_stepRecorder->toggleRecording();
                return;
            case GyanisScribe::App::TrayAction::Recognize:
                // 识别入口先框选：没有一张图，后端无事可做
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
                m_capture->startInteractive(GyanisScribe::App::CaptureIntent::Recognize);
                return;
            case GyanisScribe::App::TrayAction::RecognizeFile:
                // 需求给 OCR 列了三种来路：框选区域、贴图、以及盘上的一张图，这一条走最后那种
                m_ocr->pickImageFileAndRecognize();
                return;
            case GyanisScribe::App::TrayAction::PickColor:
                if (rejectWhileStepRecorderBusy())
                {
                    return;
                }
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

    GyanisScribe::App::TrayController &AppController::trayControllerForTesting() const noexcept
    {
        return *m_tray;
    }

    GyanisScribe::App::RewindCoordinator &AppController::rewindCoordinatorForTesting() const noexcept
    {
        return *m_rewind;
    }

    GyanisScribe::App::RecordingCoordinator &AppController::recordingCoordinatorForTesting() const noexcept
    {
        return *m_recording;
    }

    GyanisScribe::App::StepRecorderCoordinator &AppController::stepRecorderForTesting() const noexcept
    {
        return *m_stepRecorder;
    }

    GyanisScribe::App::TiledPrivacyScanner &AppController::tiledPrivacyScannerForTesting() const noexcept
    {
        return *m_tiledPrivacy;
    }

    GyanisScribe::App::HistoryCoordinator &AppController::historyCoordinatorForTesting() const noexcept
    {
        return *m_history;
    }

    GyanisScribe::App::MemoryCoordinator &AppController::memoryCoordinatorForTesting() const noexcept
    {
        return *m_memory;
    }

    void AppController::handleHotkeyActionForTesting(const GyanisScribe::Core::HotkeyAction action)
    {
        // 与热键层那条信号走同一段分派：门禁分支只在这一处判，用例不另抄一份
        handleHotkeyAction(action);
    }

    int AppController::pinCountForTesting() const noexcept
    {
        return m_pins->count();
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
            notifyUser(GyanisScribe::Base::uiText("该屏幕已不存在，无法重新截取画面。请将贴图移至仍连接的屏幕后再刷新"), GyanisScribe::Gui::ToastKind::Danger);
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

    void AppController::showMemoryConsent()
    {
        QMessageBox box{m_mainWindow.get()};
        box.setWindowTitle(GyanisScribe::Base::uiText("开启截图记忆？"));
        box.setIcon(QMessageBox::Question);
        // 四条分行的承诺：挤成一段长句时，读的人只会扫到第一行
        QStringList lines;
        lines << GyanisScribe::Base::uiText("仅索引主动截取的图像");
        lines << GyanisScribe::Base::uiText("只在本机");
        lines << GyanisScribe::Base::uiText("不上传、不录屏");
        lines << GyanisScribe::Base::uiText("可随时关闭并清空");
        box.setText(lines.join(QChar{u'\n'}));

        auto *enableButton  = box.addButton(GyanisScribe::Base::uiText("开启记忆"), QMessageBox::AcceptRole);
        auto *declineButton = box.addButton(GyanisScribe::Base::uiText("暂不"), QMessageBox::RejectRole);
        // 默认与 Esc 都落在「暂不」：顺手回车或按 Esc 不该替用户打开一个索引本机屏幕内容的库
        box.setDefaultButton(declineButton);
        box.setEscapeButton(declineButton);
        box.exec();

        const bool enabled = box.clickedButton() == enableButton;
        m_memory->resolveConsent(enabled);
        m_mainWindow->setMemoryEnabled(enabled);
    }

} // namespace GyanisScribe::Main
