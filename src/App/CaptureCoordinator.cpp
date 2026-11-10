#include "App/CaptureCoordinator.h"

#include "App/DeliveryCoordinator.h"
#include "App/HistoryCoordinator.h"
#include "App/OcrCoordinator.h"
#include "App/PinCoordinator.h"
#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/Annotation/AnnotationEngine.h"
#include "Core/Annotation/BlankCanvas.h"
#include "Core/Capture/IScreenCaptureSource.h"
#include "Core/Capture/RegionHistory.h"
#include "Core/Capture/ScreenUtils.h"
#include "Core/History/HistoryTypes.h"
#include "Core/History/UnsavedArchive.h"
#include "Core/Ocr/OcrTypes.h"
#include "Core/Privacy/PiiDetector.h"
#include "Core/Privacy/RedactionGeometry.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Image/ImageFit.h"
#include "Core/Settings/NamingRules.h"
#include "Core/Settings/SettingsKeys.h"
#include "Gui/Annotation/AnnotationToolbar.h"
#include "Gui/Annotation/AnnotationWindow.h"
#include "Gui/Capture/CaptureService.h"
#include "Gui/Privacy/PrivacyReviewDialog.h"
#include "Gui/Resources/IconProvider.h"
#include "Platform/IPlatformIntegration.h"
#include "Gui/Theme/ThemeManager.h"
#include "Plugins/Barcode/BarcodeDecoder.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPageLayout>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QScreen>
#include <QTimer>

#include <format>
#include <vector>

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

    /**
     * @brief 迟到的隐私结果回来时这一轮标注已经收场
     * @return 中文提示
     * @details 入口与审核对话框合上之后两处守卫共用同一句话：引擎与画布在关窗后还留着上一轮，
     *          迟到的结果必须被挡在这句话上，而不是落到看不见的画布上。
     */
    QString noActiveAnnotationMessage()
    {
        return GyanisScribe::Base::uiText("当前没有正在编辑的标注，请重新截图后再试");
    }
} // namespace

namespace GyanisScribe::App
{
    QString rewindBusyMessage()
    {
        return GyanisScribe::Base::uiText("先完成当前截图或标注，再打开回溯。");
    }

    CaptureCoordinator::CaptureCoordinator(Dependencies dependencies, QObject *parent) : QObject(parent), m_dependencies(dependencies)
    {
        m_annotation = std::make_unique<GyanisScribe::Core::AnnotationEngine>();
        m_capture    = std::make_unique<GyanisScribe::Gui::CaptureService>(m_dependencies.captureSource, m_dependencies.settings, m_dependencies.integration);
        // 标注一套：引擎跨轮次复用（载入新帧自己会清空），窗口与工具栏也只建一次
        m_annotationWindow = std::make_unique<GyanisScribe::Gui::AnnotationWindow>(
                *m_annotation, m_dependencies.settings, std::make_unique<GyanisScribe::Gui::AnnotationToolbar>(m_dependencies.settings, m_dependencies.icons));

        QObject::connect(m_capture.get(), &GyanisScribe::Core::ICaptureService::captured, this, &CaptureCoordinator::handleCaptured);
        QObject::connect(m_capture.get(), &GyanisScribe::Core::ICaptureService::captureCancelled, this,
                         [this]
                         {
                             GyanisScribe::Base::Logger::info("截图已取消");
                             finishCaptureFlow();
                         });
        QObject::connect(m_capture.get(), &GyanisScribe::Core::ICaptureService::captureFailed, this,
                         [this](const GyanisScribe::Base::Error &error)
                         {
                             GyanisScribe::Base::Logger::error(std::format("截图未完成：{}", error.message));
                             emit messageForUser(GyanisScribe::Base::uiText("截图未完成：%1。请重试一次").arg(QString::fromStdString(error.message)),
                                                 GyanisScribe::Gui::ToastKind::Danger);
                             // 命令行那次没有界面可看提示：失败原因另外从 commandLineActionFailed 交回一份
                             if (m_commandLineActive)
                             {
                                 emit commandLineActionFailed(QString::fromStdString(error.message));
                             }
                             finishCaptureFlow();
                         });

        // 标注窗的六个出口：动作只有一个入口，界面不需要知道用户是点了按钮还是按了键盘
        QObject::connect(m_annotationWindow.get(), &GyanisScribe::Gui::AnnotationWindow::copyRequested, this, &CaptureCoordinator::copyToClipboard);
        QObject::connect(m_annotationWindow.get(), &GyanisScribe::Gui::AnnotationWindow::saveRequested, &m_dependencies.delivery,
                         &GyanisScribe::App::DeliveryCoordinator::saveAndReport);
        QObject::connect(m_annotationWindow.get(), &GyanisScribe::Gui::AnnotationWindow::pinRequested, this,
                         [this](const QImage &image) { m_dependencies.pins.pinAt(image, m_lastSelection.topLeft()); });
        QObject::connect(m_annotationWindow.get(), &GyanisScribe::Gui::AnnotationWindow::ocrRequested, &m_dependencies.ocr, &GyanisScribe::App::OcrCoordinator::requestRecognition);
        QObject::connect(m_annotationWindow.get(), &GyanisScribe::Gui::AnnotationWindow::scanRequested, this, &CaptureCoordinator::scanImage);
        QObject::connect(m_annotationWindow.get(), &GyanisScribe::Gui::AnnotationWindow::printRequested, this, &CaptureCoordinator::printImage);
        QObject::connect(m_annotationWindow.get(), &GyanisScribe::Gui::AnnotationWindow::refreshRequested, this, &CaptureCoordinator::refreshLastCapture);
        QObject::connect(m_annotationWindow.get(), &GyanisScribe::Gui::AnnotationWindow::cropRequested, this, &CaptureCoordinator::cropLastCapture);
        // 标注窗一关就是这一轮流程的终点：回溯协调器要在这里恢复抓帧
        QObject::connect(m_annotationWindow.get(), &GyanisScribe::Gui::AnnotationWindow::closed, this, &CaptureCoordinator::finishCaptureFlow);
    }

    CaptureCoordinator::~CaptureCoordinator() = default;

    GyanisScribe::Gui::AnnotationWindow &CaptureCoordinator::annotationWindow() const noexcept
    {
        return *m_annotationWindow;
    }

    bool CaptureCoordinator::isCaptureFlowActive() const noexcept
    {
        return m_captureFlowActive;
    }

    bool CaptureCoordinator::isDelayPending() const noexcept
    {
        // 到点回调先把指针清掉再开始抓帧：那一刻之后由流程标志或标注窗接手挡人
        return m_delayTimer != nullptr;
    }

    void CaptureCoordinator::startInteractive(const std::optional<GyanisScribe::App::CaptureIntent> intent)
    {
        m_intent = intent.value_or(settingsCaptureIntent());
        // 流程标志跟着到点那一步立：schedule 被上一次延时挡回时，这一轮根本没开始，
        // 先把标志立起来会让回溯热键一直挡人
        schedule(
                [this]
                {
                    m_captureFlowActive = true;
                    m_capture->startInteractive();
                });
    }

    void CaptureCoordinator::startInteractiveWithFrame(const GyanisScribe::Base::CapturedImage &frame)
    {
        // 去向照设置现读：回溯只是换了一张底图，之后的标注/复制/保存仍是用户点的那一档
        m_intent            = settingsCaptureIntent();
        m_captureFlowActive = true;
        // 回溯帧不是当前前台窗口：不额外取进程，本轮来源留空，免得把当下正在前台的应用记到旧帧上
        m_sourceInfo = {};
        m_capture->startInteractiveWithFrame(frame);
    }

    void CaptureCoordinator::startCommandLine(const GyanisScribe::App::CommandLineRequest &request)
    {
        // 命令行那条路没有界面可等，也不走延时那道闸——延时是给手动截菜单留的，脚本要的是按下即截。
        // 去处由 --to 点名（没点名时解析器已填上默认的「文件 + 剪贴板」），意图仍定在「快速保存」那一档：
        // 上面那串「截图完成」的记账（历史留一份）读的是它
        m_commandLineActive    = true;
        m_intent               = GyanisScribe::App::CaptureIntent::QuickSave;
        m_commandLineOutputs   = request.outputs.empty() ? GyanisScribe::App::defaultCaptureOutputs() : request.outputs;
        m_commandLineExec      = request.execCommand;
        m_commandLineExecGiven = !request.execCommand.trimmed().isEmpty();
        // 这条路绕过 schedule：来源要在抓帧前就地取一次
        noteForegroundSource();
        switch (request.action)
        {
            case GyanisScribe::App::CommandLineAction::CaptureFullScreen:
                m_capture->captureFullScreen();
                return;
            case GyanisScribe::App::CommandLineAction::CaptureLastRegion:
                m_capture->captureLastRegion();
                return;
            case GyanisScribe::App::CommandLineAction::CaptureWindow:
            {
                const auto window = m_dependencies.integration.foregroundWindow();
                if (!window)
                {
                    GyanisScribe::Base::Logger::error(std::format("命令行截窗口没成：{}", window.error().message));
                    emit commandLineActionFailed(QString::fromStdString(window.error().message));
                    return;
                }
                m_capture->captureWindow(window->handle);
                return;
            }
            default:
                emit commandLineActionFailed(GyanisScribe::Base::uiText("这个命令行动作不走截图这条路"));
                return;
        }
    }

    void CaptureCoordinator::startFullScreen(const std::optional<GyanisScribe::App::CaptureIntent> intent)
    {
        m_intent = intent.value_or(settingsCaptureIntent());
        schedule([this] { m_capture->captureFullScreen(); });
    }

    void CaptureCoordinator::startLastRegion(const std::optional<GyanisScribe::App::CaptureIntent> intent)
    {
        m_intent = intent.value_or(settingsCaptureIntent());
        schedule([this] { m_capture->captureLastRegion(); });
    }

    void CaptureCoordinator::startLong()
    {
        m_intent = settingsCaptureIntent();
        // 长截图也从框选起头：流程标志要跟着一起立，否则用户框完区域进入滚动采集时，
        // 回溯热键会以为没有流程在跑，再叠一层遮罩。与框选同一口径，标志放到到点那一步再立，
        // 被延时回绝的那一次不能把它留在 true
        schedule(
                [this]
                {
                    m_captureFlowActive = true;
                    m_capture->startLongCapture();
                });
    }

    void CaptureCoordinator::startBlankCanvas()
    {
        auto *screen = QGuiApplication::screenAt(QCursor::pos());
        if (screen == nullptr)
        {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen == nullptr)
        {
            // 一块屏都拿不到就不摆窗：标注窗没有可依附的几何，硬摆会落在一块不存在的屏幕上
            emit messageForUser(GyanisScribe::Base::uiText("空白画布没开起来：Qt 没有报出任何可用的屏幕。请在图形会话里重试"), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }

        // 底是白的还是透明的由设置里那一格定：有人要拿画布当垫在别的图上面的一层，白底会把下面那层整个盖掉
        const auto transparentCanvas = m_dependencies.settings.boolValue(GyanisScribe::Core::kAnnotationBlankCanvasTransparent, false);
        const auto canvas            = GyanisScribe::Core::makeBlankCanvas(GyanisScribe::Core::kBlankCanvasSize, screen->devicePixelRatio(), screen->name(), transparentCanvas);
        const auto available         = screen->availableGeometry();
        // 空白画布不是从屏上抓来的图：这一轮没有来源，交付那一族也要换到这一轮，
        // 否则保存白板会把上一轮截图的前台应用与标题记进历史
        m_sourceInfo = {};
        m_dependencies.delivery.beginRound();
        // 不碰抓帧那条链：没有帧、不记「上次选区」、也不触发音效/自动保存/历史——那一串都挂在「抓到了屏上的一块」上
        m_annotationWindow->startAnnotation(canvas, available, GyanisScribe::Core::blankCanvasRegion(available, canvas.logicalSize));
        GyanisScribe::Base::Logger::info(std::format("空白画布已开：逻辑 {}×{}，物理 {}×{}，屏 {}，底{}", canvas.logicalSize.width(), canvas.logicalSize.height(),
                                                     canvas.image.width(), canvas.image.height(), canvas.screenName.toStdString(), transparentCanvas ? "透明" : "白"));
    }

    void CaptureCoordinator::captureForegroundWindow()
    {
        // 走与其他截图入口同一条延时通道：到点才取前台窗口，才截得到「等的那几秒里摆出来的」那一扇
        // 去向也与其他入口同解：从「截完之后」那一格现读，不沿用上一次截图留下的那个值
        m_intent = settingsCaptureIntent();
        schedule(
                [this]
                {
                    const auto window = m_dependencies.integration.foregroundWindow();
                    if (!window)
                    {
                        // 菜单按能力位过滤过这一项，走到这里说明能力位与实际结果不一致（或以后新增了别的入口）：
                        // 原因照样发给用户，不静默什么都不做
                        emit messageForUser(QString::fromStdString(window.error().message), GyanisScribe::Gui::ToastKind::Danger);
                        return;
                    }

                    m_capture->captureWindow(window->handle);
                });
    }

    void CaptureCoordinator::captureScreen(const QString &screenName)
    {
        // 与整屏截图同一条路（含延时那一档）：两处各写一份流程，延时与成品去向那几档迟早只改一处
        m_intent = settingsCaptureIntent();
        schedule([this, screenName] { m_capture->captureFullScreen(screenName); });
    }

    void CaptureCoordinator::schedule(std::function<void()> begin)
    {
        const auto stored = m_dependencies.settings.intValue(GyanisScribe::Core::kCaptureDelaySeconds, 0);
        const auto delay  = GyanisScribe::App::parseCaptureDelay(stored);
        if (!delay)
        {
            // 读不出就按不延时继续：拦下这一次按键比让用户对着一个莫名其妙的等待更好解释
            GyanisScribe::Base::Logger::warning(std::format("延时截图的秒数读不出：{}，按不延时继续", delay.error().message));
        }
        const auto seconds = delay ? *delay : 0;
        if (seconds <= 0)
        {
            // 来源在 begin() 之前现取：抓帧那一刻前台是谁，就记谁
            noteForegroundSource();
            begin();
            return;
        }

        if (m_delayTimer != nullptr)
        {
            emit messageForUser(GyanisScribe::Base::uiText("上一次延时截图还在等，等它弹出来之后再按"), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        m_delayTimer = new QTimer{this};
        m_delayTimer->setSingleShot(true);
        QObject::connect(m_delayTimer, &QTimer::timeout, this,
                         [this]
                         {
                             m_delayTimer->deleteLater();
                             m_delayTimer = nullptr;
                             // 延时这几秒里用户会把目标摆到前台：来源必须到点现取，而不是按下热键时那份
                             noteForegroundSource();
                             m_pendingCapture();
                         });
        // 成员里存的是这一轮要做的动作：定时器响时用户可能已经切走了窗口，走的是同一条开始截图的路
        m_pendingCapture = std::move(begin);
        m_delayTimer->start(seconds * 1000);
        emit messageForUser(
                QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("{} 秒后开始截图：把要截的菜单或悬停状态摆好，这一会儿别动"), seconds).c_str()),
                GyanisScribe::Gui::ToastKind::Information);
    }

    void CaptureCoordinator::noteForegroundSource()
    {
        // 每一轮先清空：上一轮的来源不能跟到这一轮
        m_sourceInfo      = {};
        const auto window = m_dependencies.integration.foregroundWindow();
        if (!window)
        {
            // 来源只是元数据：锁屏或焦点在桌面上时会取不到，不弹提示、不挡这次截图
            return;
        }
        m_sourceInfo.processName = window->processName;
        m_sourceInfo.windowTitle = window->title;
    }

    void CaptureCoordinator::handleCaptured(const GyanisScribe::Base::CapturedImage &image)
    {
        // 屏幕与时刻来自抓帧本身：先补齐再开交付那一轮，自动保存与历史记账才拿得到完整来源
        m_sourceInfo.screenName = image.screenName;
        m_sourceInfo.capturedAt = image.capturedAt.isValid() ? image.capturedAt : QDateTime::currentDateTime();
        m_dependencies.delivery.beginRound(m_sourceInfo);
        m_lastCapture = image;

        // 音效与自动保存接在「截图完成」这一刻：不管之后去哪一步，先响一声、先在盘上留一份。
        // 自动保存走「存成文件」同一条落盘路（装饰与历史记账都在里面），只是不弹成功提示
        if (m_dependencies.settings.boolValue(GyanisScribe::Core::kCaptureSound, false))
        {
            QApplication::beep();
        }
        if (m_dependencies.settings.boolValue(GyanisScribe::Core::kCaptureAutoSave, false) && m_intent != GyanisScribe::App::CaptureIntent::SaveToFile &&
            m_intent != GyanisScribe::App::CaptureIntent::QuickSave)
        {
            if (const auto saved = m_dependencies.delivery.saveToDisk(image.image); saved)
            {
                GyanisScribe::Base::Logger::info(std::format("已自动保存：{}", saved->toStdString()));
            } else
            {
                // 自动保存失败要说出来：用户开着这一档就是指望盘上那份在，静默会把「没有备份」瞒到最后
                emit messageForUser(QString::fromStdString(saved.error().message), GyanisScribe::Gui::ToastKind::Danger);
            }
        }

        // 「没存文件的截图也进历史」接在截图完成这一刻：误触 Esc 关掉标注窗之后，这一张还在历史里。
        // 已经在这一轮落盘的那几路（存文件、快速保存、自动保存）不重复进——两条指同一张图的历史记录是噪声。
        // 命令行那一次看的是点名的去处里有没有 file：只点 clipboard 时这一轮同样没落盘，那一步该走
        const auto alreadyOnDisk = m_commandLineActive ? GyanisScribe::App::captureOutputsContain(m_commandLineOutputs, GyanisScribe::App::CaptureOutput::File)
                                                       : (m_intent == GyanisScribe::App::CaptureIntent::SaveToFile || m_intent == GyanisScribe::App::CaptureIntent::QuickSave ||
                                                          m_dependencies.settings.boolValue(GyanisScribe::Core::kCaptureAutoSave, false));
        if (m_dependencies.settings.boolValue(GyanisScribe::Core::kHistoryKeepUnsaved, false) && !alreadyOnDisk)
        {
            if (const auto archived = GyanisScribe::Core::archiveUnsavedCapture(image.image, QDateTime::currentDateTime(), GyanisScribe::Base::Paths::unsavedCaptureDirectory(),
                                                                                GyanisScribe::Core::kUnsavedArchiveCapacity);
                archived)
            {
                // 记的是像素尺寸而不是逻辑尺寸：历史缩略图与「重新贴图」都按这份文件读回来
                m_dependencies.history.record(*archived, GyanisScribe::Core::HistoryKind::Screenshot, image.image.size(), m_sourceInfo);
            } else
            {
                // 开着这一档就是指望这一张还在，静默失败等于骗人
                emit messageForUser(QString::fromStdString(archived.error().message), GyanisScribe::Gui::ToastKind::Danger);
            }
        }

        auto *screen = screenFor(image.screenName);
        if (screen == nullptr)
        {
            // 截图那块屏幕已经不在了，标注窗摆不出可用位置；退回「直接交剪贴板」并说清原因，
            // 而不是把画面摆到一块不存在的屏幕上
            QGuiApplication::clipboard()->setImage(image.image);
            GyanisScribe::Base::Logger::warning(std::format("截图所在屏幕 {} 已不可用，标注没开启，画面已直接交剪贴板", image.screenName.toStdString()));
            // 命令行那一次也要收个口：脚本的事件循环等的是交付信号，不发出去它就停在「截完了但没人说结束」
            if (m_commandLineActive)
            {
                emit commandLineCaptureDelivered(m_dependencies.delivery.writtenFilePath());
            }
            // 屏不可用这一路没有标注窗可等：成品已经直接进了剪贴板，这一轮就此收场
            finishCaptureFlow();
            return;
        }

        // 选区存的是「屏幕内」的逻辑坐标，回读后要加屏幕原点换成全局坐标；
        // 没有可用记录时按整块可用区处理，工具栏照摆不误
        auto selection = screen->availableGeometry();
        if (const auto parsed = GyanisScribe::Core::regionFromText(m_dependencies.settings.stringValue(GyanisScribe::Core::kCaptureLastRegion, QString{}).toStdString()); parsed)
        {
            selection = QRect{parsed->topLeft() + screen->geometry().topLeft(), parsed->size()};
        }
        m_lastSelection = selection;

        // 命令行那一次按点名的去处逐处交付，不开标注窗：脚本要的是截完就走，多一层窗就是多一次要人点的取消
        if (m_commandLineActive)
        {
            deliverCommandLineOutputs();
            return;
        }

        deliverFrame(image.image, *screen);
        // 外部命令接在「这一轮交付完」之后：命令里多半要拿盘上那份，交付没完就跑等于喂给它一个还不存在的文件
        const auto ran = m_dependencies.delivery.runAfterCaptureCommand(m_commandLineExecGiven ? m_commandLineExec : QString{});
        if (!ran)
        {
            emit messageForUser(QString::fromStdString(ran.error().message), GyanisScribe::Gui::ToastKind::Danger);
        }
        // 标注那一档还要等窗关：其余去向当场交完，这一轮流程到这里就结束了
        if (m_intent != GyanisScribe::App::CaptureIntent::Annotate)
        {
            finishCaptureFlow();
        }
    }

    void CaptureCoordinator::finishCaptureFlow()
    {
        if (!m_captureFlowActive)
        {
            return;
        }
        m_captureFlowActive = false;
        emit captureFlowFinished();
    }

    void CaptureCoordinator::deliverFrame(const QImage &image, QScreen &screen)
    {
        // 交完帧之后做什么由那一档决定；开关覆盖到每一个枚举值，新加一档忘了接会在这里就编不过
        switch (m_intent)
        {
            case GyanisScribe::App::CaptureIntent::Recognize:
                // 识别这条不弹标注窗：用户点的是「识别文字」，先弹一个要再点一次的界面就是多一跳
                m_dependencies.ocr.requestRecognition(image);
                return;
            case GyanisScribe::App::CaptureIntent::CopyToClipboard:
                copyToClipboard(image);
                return;
            case GyanisScribe::App::CaptureIntent::SaveToFile:
                m_dependencies.delivery.saveAndReport(image);
                return;
            case GyanisScribe::App::CaptureIntent::QuickSave:
            {
                // 快速保存：落盘 + 交剪贴板，一句提示收尾。失败时不做剪贴板那一半——
                // 「存下了也复制了」不能说半句真话，用户会以为盘上有一份
                const auto saved = m_dependencies.delivery.saveToDisk(image);
                if (!saved)
                {
                    emit messageForUser(QString::fromStdString(saved.error().message), GyanisScribe::Gui::ToastKind::Danger);
                    return;
                }
                QGuiApplication::clipboard()->setImage(m_dependencies.delivery.decorate(image, false));
                emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("已保存并复制：{}"), saved->toStdString()).c_str()),
                                    GyanisScribe::Gui::ToastKind::Success);
                return;
            }
            case GyanisScribe::App::CaptureIntent::PinToDesk:
                m_dependencies.pins.pinAt(image, m_lastSelection.topLeft());
                return;
            case GyanisScribe::App::CaptureIntent::Annotate:
                break;
        }
        m_annotationWindow->startAnnotation(m_lastCapture, screen.availableGeometry(), m_lastSelection, m_sourceInfo);
    }

    void CaptureCoordinator::deliverCommandLineOutputs()
    {
        // 交付顺序只由词表定：先盘上那份（外部命令的 %file% 要用它），再剪贴板，最后贴到屏幕上
        for (const auto output: m_commandLineOutputs)
        {
            switch (output)
            {
                case GyanisScribe::App::CaptureOutput::File:
                {
                    const auto saved = m_dependencies.delivery.saveToDisk(m_lastCapture.image);
                    if (!saved)
                    {
                        // 落盘失败就整次报错、退出码 1：把「成了两路、少了一路」说成成功，脚本会拿着假信号往下走
                        emit commandLineActionFailed(QString::fromStdString(saved.error().message));
                        return;
                    }
                    break;
                }
                case GyanisScribe::App::CaptureOutput::Clipboard:
                    // 与「复制到剪贴板」那一档同一套装饰口径：剪贴板带得了透明通道，不垫白
                    QGuiApplication::clipboard()->setImage(m_dependencies.delivery.decorate(m_lastCapture.image, false));
                    break;
                case GyanisScribe::App::CaptureOutput::Pin:
                    m_dependencies.pins.pinAt(m_lastCapture.image, m_lastSelection.topLeft());
                    break;
            }
        }
        const auto ran = m_dependencies.delivery.runAfterCaptureCommand(m_commandLineExecGiven ? m_commandLineExec : QString{});
        if (!ran)
        {
            // 脚本的事件循环等的是一个结果：命令没跑起来就说失败，别让退出码停在 0 上
            emit commandLineActionFailed(QString::fromStdString(ran.error().message));
            return;
        }
        emit commandLineCaptureDelivered(m_dependencies.delivery.writtenFilePath());
    }

    GyanisScribe::Base::Expected<GyanisScribe::Base::CapturedImage> CaptureCoordinator::grabWithAnnotationHidden(const QString &screenName, const QRect &region,
                                                                                                                 const bool restoreVisibility)
    {
        // 窗与工具栏都盖在要抓的那一块上：记下可见态与几何，抓完照原样摆回
        const bool  windowWasVisible  = m_annotationWindow->isVisible();
        const bool  toolbarWasVisible = m_annotationWindow->toolbar().isVisible();
        const QRect windowGeometry    = m_annotationWindow->geometry();
        const QRect toolbarGeometry   = m_annotationWindow->toolbar().geometry();

        m_annotationWindow->hide();
        m_annotationWindow->toolbar().hide();
        // 隐藏要等合成器交出那一帧：不等的话这次抓到的仍是窗还在的那一版
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        const auto frame = m_capture->grabRegionNow(region, screenName);

        // 成败都摆回几何：抓失败时画布与标注原样留着，但窗口不许因为这一眼抓屏留在屏外
        m_annotationWindow->setGeometry(windowGeometry);
        m_annotationWindow->toolbar().setGeometry(toolbarGeometry);
        if (!restoreVisibility)
        {
            // 重裁路径随后先换画布再显窗；这里显出来会先闪一帧旧底图
            return frame;
        }
        if (windowWasVisible)
        {
            m_annotationWindow->show();
            m_annotationWindow->raise();
            // 连按 F5 时这一颗键要回到这扇窗：不激活的话焦点留在别处
            m_annotationWindow->activateWindow();
        }
        if (toolbarWasVisible)
        {
            m_annotationWindow->toolbar().show();
            m_annotationWindow->toolbar().raise();
        }
        return frame;
    }

    void CaptureCoordinator::refreshLastCapture(const QString &screenName, const QRect &region)
    {
        // 区域未知（整屏抓来的那几路）就没法对着重抓：说一声，而不是抓一块错的盖上去
        if (region.isEmpty())
        {
            emit messageForUser(GyanisScribe::Base::uiText("这一轮没记录选区，刷新截图用不了"), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        const auto frame = grabWithAnnotationHidden(screenName, region, true);
        if (!frame)
        {
            emit messageForUser(QString::fromStdString(frame.error().message), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        // 这一轮抓到的就是最新那一帧：下一回拖边框重裁必须从它起算。只换画布不换记录的话，
        // 冻帧还停在刷新之前，重裁会裁出 F5 之前那一张
        m_lastCapture = *frame;
        m_annotationWindow->refreshCapture(*frame);
    }

    void CaptureCoordinator::cropLastCapture(const QString &screenName, const QRect &globalSelection)
    {
        auto *screen = screenFor(screenName);
        if (screen == nullptr || globalSelection.isEmpty())
        {
            emit messageForUser(GyanisScribe::Base::uiText("这一轮没记录屏幕或选区，改不了截图范围"), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        const QRect local{globalSelection.topLeft() - screen->geometry().topLeft(), globalSelection.size()};

        // 交互截图带回了整屏冻帧：新范围直接从同一帧裁，画面与用户框选时一致，也不必藏窗再抓
        if (!m_lastCapture.frozenImage.isNull() && !m_lastCapture.frozenLogicalSize.isEmpty())
        {
            Base::CapturedImage source;
            source.image            = m_lastCapture.frozenImage;
            source.logicalSize      = m_lastCapture.frozenLogicalSize;
            source.devicePixelRatio = m_lastCapture.devicePixelRatio;
            source.screenName       = m_lastCapture.screenName;
            source.logicalRegion    = m_lastCapture.frozenLogicalRegion;
            auto cropped            = Core::cropToLogicalRect(source, local);
            if (cropped)
            {
                cropped->frozenImage         = source.image;
                cropped->frozenLogicalSize   = source.logicalSize;
                cropped->frozenLogicalRegion = source.logicalRegion;
                m_lastCapture                = *cropped;
                m_lastSelection              = globalSelection;
                Core::rememberRegion(m_dependencies.settings, Core::RegionHistoryEntry{local, screenName});
                m_annotationWindow->recropAnnotation(m_lastCapture, screen->availableGeometry(), m_lastSelection);
                return;
            }
        }

        // 来源没带冻帧（整屏抓的那几路）：退回重新抓屏；先藏窗，DXGI 抓到的是整块桌面
        // 重露交给下面的 recropAnnotation 与失败时的 restoreAfterCrop：它们先换画布再显窗
        const auto frame = grabWithAnnotationHidden(screenName, local, false);
        if (!frame)
        {
            // 抓新范围失败就退回原来那一轮：画布与标注都没动，只把窗口摆回旧几何
            m_annotationWindow->restoreAfterCrop(m_lastSelection, screen->availableGeometry());
            emit messageForUser(QString::fromStdString(frame.error().message), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }

        m_lastCapture   = *frame;
        m_lastSelection = globalSelection;
        Core::rememberRegion(m_dependencies.settings, Core::RegionHistoryEntry{local, screenName});
        m_annotationWindow->recropAnnotation(m_lastCapture, screen->availableGeometry(), m_lastSelection);
    }

    void CaptureCoordinator::copyToClipboard(const QImage &image)
    {
        // 剪贴板带得了透明通道：装饰按「不垫白」来（垫了白，贴到深色底上的投影会围一圈白框）
        const auto decorated = m_dependencies.delivery.decorate(image, false);
        QGuiApplication::clipboard()->setImage(decorated);
        GyanisScribe::Base::Logger::info(std::format("标注结果已交剪贴板：{}×{} 像素", decorated.width(), decorated.height()));
        // 说一句：交剪贴板这一步本来没有任何界面变化，「按了没反应」与「已经进去了」只有这一处能分
        emit messageForUser(GyanisScribe::Base::uiText("已复制到剪贴板"), GyanisScribe::Gui::ToastKind::Success);
    }

    void CaptureCoordinator::printImage(const QImage &image)
    {
        if (image.isNull())
        {
            emit messageForUser(GyanisScribe::Base::uiText("还没有可打印的画面"), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        QPrinter     printer{QPrinter::HighResolution};
        QPrintDialog dialog{&printer, m_annotationWindow.get()};
        dialog.setWindowTitle(GyanisScribe::Base::uiText("打印标注结果"));
        if (dialog.exec() != QDialog::Accepted)
        {
            // 取消是一种正常收场：什么都没打印，也什么都不用说
            return;
        }

        QPainter painter;
        if (!painter.begin(&printer))
        {
            GyanisScribe::Base::Logger::error("打印没能开始：打印设备没有准备好");
            emit messageForUser(GyanisScribe::Base::uiText("打印没能开始：系统打印设备没有准备好。可以先存成文件再从别处打"), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        // 纸面按没有透明通道处理（投影底下垫白）；等比居中用的就是「图片摆进控件」那条既有换算，
        // 打印的摆放不需要第二套适配算法
        const auto decorated = m_dependencies.delivery.decorate(image, true);
        const auto page      = printer.pageLayout().paintRectPixels(printer.resolution());
        const auto target    = GyanisScribe::Core::displayRectFor(page.size(), decorated.size()).translated(page.topLeft());
        painter.drawImage(target, decorated);
        painter.end();
        GyanisScribe::Base::Logger::info(std::format("已送往打印机：{}×{} 像素", decorated.width(), decorated.height()));
        emit messageForUser(GyanisScribe::Base::uiText("已送往打印机"), GyanisScribe::Gui::ToastKind::Success);
    }

    void CaptureCoordinator::scanImage(const QImage &image)
    {
        const auto readings = GyanisScribe::Plugins::readBarcodes(image);
        if (!readings)
        {
            // 后端没编进来这类原因原样播出去：文案里已经写明出路
            emit messageForUser(QString::fromStdString(readings.error().message), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        if (readings->empty())
        {
            emit messageForUser(GyanisScribe::Base::uiText("这一张里没认出二维码或条形码"), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        // 认出多个就报第一个二维的（一维在二维码花纹里偶发假阳性，见 preferredReading）；
        // 提示里说明数量
        const auto preferred = GyanisScribe::Plugins::preferredReading(*readings);
        if (!preferred)
        {
            return; // 空列表在上面已经报过了，这里只是不变量兜底
        }
        QGuiApplication::clipboard()->setText(preferred->text);
        if (readings->size() == 1)
        {
            emit messageForUser(GyanisScribe::Base::uiText("已复制识别结果（%1 个字符）").arg(preferred->text.size()), GyanisScribe::Gui::ToastKind::Success);
        } else
        {
            emit messageForUser(GyanisScribe::Base::uiText("认出 %1 个码，已复制最上面那条").arg(static_cast<int>(readings->size())), GyanisScribe::Gui::ToastKind::Success);
        }
    }

    void CaptureCoordinator::handlePrivacyScan(const QImage &image, const GyanisScribe::Core::OcrResult &result)
    {
        // 识别是异步的：回来时这一轮标注可能已经收起（Esc、X 或交付出口），引擎与画布却还留着上一轮。
        // 先认轮次再认底图：没有正在编辑的标注时既不该弹审核，也不该往看不见的画布上落笔
        if (!m_annotationWindow->isRoundActive())
        {
            emit messageForUser(noActiveAnnotationMessage(), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        // 同一条轮次里画布也可能已经刷新或重裁（F5、拖边框），旧框的坐标属于另一张图。
        // 同一份图像素的 cacheKey 不变，换了底图就变——比尺寸更严，同尺寸重裁也能挡住
        const auto &canvas = m_annotation->canvas();
        if (image.isNull() || canvas.image.isNull() || image.cacheKey() != canvas.image.cacheKey())
        {
            emit messageForUser(GyanisScribe::Base::uiText("画布已经变了，请重新点隐私打码"), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        std::vector<QRect> barcodeBoxes;
        if (const auto readings = GyanisScribe::Plugins::readBarcodes(image); readings)
        {
            // 一维码的定位框是一条近零高度的扫描线：撑到至少 32 逻辑像素高的算法在 Core 里单测，
            // 这里只管按它把物理框并进清单；二维码的定位框本来就框住整个码，原样并入
            barcodeBoxes.reserve(readings->size());
            for (const auto &reading: *readings)
            {
                const QRect box = reading.matrix ? reading.box.normalized().intersected(image.rect())
                                                 : GyanisScribe::Core::Privacy::expandBarcodeScanline(reading.box, image.size(), canvas.logicalSize);
                if (!box.isEmpty())
                {
                    barcodeBoxes.push_back(box);
                }
            }
        } else
        {
            // 码识别后端不在场不该拖住文本检测：记下原因，清单里不出现二维码项
            GyanisScribe::Base::Logger::warning(std::format("隐私扫描里的二维码识别已跳过：{}", readings.error().message));
        }

        const auto items = GyanisScribe::Core::Privacy::detectPii(result, barcodeBoxes, image.size());
        if (items.empty())
        {
            emit messageForUser(GyanisScribe::Base::uiText("没有发现可打码的敏感信息。请仍自行检查后再分享。"), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        // 审核是用户确认这一步：默认全选也要按下「应用打码」才落笔，取消或一项不选都什么都不改
        GyanisScribe::Gui::PrivacyReviewDialog dialog{items, m_annotationWindow.get()};
        if (dialog.exec() != QDialog::Accepted)
        {
            return;
        }
        const auto selected = dialog.selectedItems();
        if (selected.empty())
        {
            return;
        }

        // 模态期间这一轮可能已经收场，底图也可能被换掉：落笔前用同一对判据复核，不再信 exec 之前那份现场
        if (!m_annotationWindow->isRoundActive())
        {
            emit messageForUser(noActiveAnnotationMessage(), GyanisScribe::Gui::ToastKind::Information);
            return;
        }
        const auto &currentCanvas = m_annotation->canvas();
        if (currentCanvas.image.isNull() || image.cacheKey() != currentCanvas.image.cacheKey())
        {
            emit messageForUser(GyanisScribe::Base::uiText("画布已经变了，请重新点隐私打码"), GyanisScribe::Gui::ToastKind::Information);
            return;
        }

        // 物理框换到画布逻辑坐标：倍比、留边与夹取都在 Core 的纯函数里，单测按同一份算式钉住
        std::vector<GyanisScribe::Core::RedactionRequest> requests;
        requests.reserve(selected.size());
        for (const auto &item: selected)
        {
            if (const auto rect = GyanisScribe::Core::Privacy::logicalRedactionRect(item.physicalBox, currentCanvas.image.size(), currentCanvas.logicalSize); rect)
            {
                requests.push_back(GyanisScribe::Core::RedactionRequest{.logicalRect = *rect});
            }
        }
        // 全部框都被换算剔掉时不自己再写一句：引擎对空清单给的中文原因就是这里该说的那一句
        if (auto added = m_annotation->addRedactions(requests, dialog.redactionTool()); !added)
        {
            emit messageForUser(QString::fromStdString(added.error().message), GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        // 数的是实际交给引擎的框：换算后落在画布之外的那些已被剔除，不再算进成功提示
        emit messageForUser(GyanisScribe::Base::uiText("已打码 %1 处；请自行检查后再分享。").arg(static_cast<int>(requests.size())), GyanisScribe::Gui::ToastKind::Success);
    }

    GyanisScribe::App::CaptureIntent CaptureCoordinator::settingsCaptureIntent() const
    {
        const auto stored = m_dependencies.settings.stringValue(GyanisScribe::Core::kCaptureAfterAction, QString{});
        if (stored.isEmpty())
        {
            return GyanisScribe::App::defaultCaptureIntent();
        }
        const auto parsed = GyanisScribe::App::parseCaptureIntent(stored);
        if (!parsed)
        {
            // 读不出就按默认档走，但原因要落进日志：只把界面悄悄拨回「进标注窗」，
            // 用户会以为自己那一档没保存成功，而实际是配置里那一格的值写歪了
            GyanisScribe::Base::Logger::warning(std::format("设置里的「截完之后」读不出：{}", parsed.error().message));
            return GyanisScribe::App::defaultCaptureIntent();
        }
        return *parsed;
    }

    GyanisScribe::Base::Expected<GyanisScribe::Base::CapturedImage> CaptureCoordinator::grabRegionNow(const QRect &logicalRegion, const QString &screenName)
    {
        // 贴图那条 F5 从这里取图：抓帧的口子在服务上，枢纽只负责把它露出来
        return m_capture->grabRegionNow(logicalRegion, screenName);
    }

} // namespace GyanisScribe::App
