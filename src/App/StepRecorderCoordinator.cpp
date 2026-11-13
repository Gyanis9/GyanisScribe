#include "App/StepRecorderCoordinator.h"

#include "App/StepsAutoRedactor.h"
#include "Base/Logger.h"
#include "Base/Text.h"
#include "Core/Capture/IScreenCaptureSource.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Steps/StepTypes.h"
#include "Gui/Steps/StepConsentDialog.h"
#include "Gui/Steps/StepRecorderHud.h"
#include "Gui/Steps/StepReviewWindow.h"
#include "Gui/Theme/ThemeManager.h"
#include "Platform/IStepEventSource.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QEventLoop>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>

#include <algorithm>
#include <format>
#include <utility>

namespace
{
    /**
     * @brief 本次会话的临时目录名
     * @param now 开始录制的时刻
     * @return 形如 20261111-153012-345 的名字；同一秒内重开也不会撞上上一轮的目录
     */
    QString sessionDirectoryName(const QDateTime &now)
    {
        return now.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    }

    /// 错误对象里的中文原因
    QString errorText(const GyanisScribe::Base::Error &error)
    {
        return QString::fromStdString(error.message);
    }

    /**
     * @brief 首次说明窗
     * @param parent 父窗口，可空
     * @return 用户选了「开始记录」时为 true
     */
    bool askConsent(QWidget *parent)
    {
        GyanisScribe::Gui::StepConsentDialog dialog{parent};
        return dialog.exec() == QDialog::Accepted;
    }
} // namespace

namespace GyanisScribe::App
{
    StepRecorderCoordinator::StepRecorderCoordinator(StepRecorderDependencies dependencies, QObject *parent) : QObject(parent), m_dependencies(std::move(dependencies))
    {
        m_pendingTimer.setSingleShot(true);
        QObject::connect(&m_pendingTimer, &QTimer::timeout, this, &StepRecorderCoordinator::commitPending);
        // 停止后的审核从这里接手：信号既是给外部的状态通知，也是进入审核的唯一入口
        QObject::connect(this, &StepRecorderCoordinator::stopped, this, &StepRecorderCoordinator::handleStopped);

        // 上一次没审完或崩溃留下的会话先回收，别让临时目录一直涨
        Core::StepSession::cleanupOldSessions(m_dependencies.tempRoot, QDateTime::currentDateTime());
    }

    StepRecorderCoordinator::~StepRecorderCoordinator()
    {
        m_pendingTimer.stop();
        m_pending.reset();
        if (m_source)
        {
            m_source->stop();
            QObject::disconnect(m_source.get(), nullptr, this, nullptr);
            m_source.reset();
        }
        if (m_hud)
        {
            m_hud->hide();
        }
        if (m_review)
        {
            // 直接删而不是 close：析构路径不该再弹放弃确认；临时目录随后由会话收掉
            delete m_review.data();
        }
        m_session.cleanup();
    }

    void StepRecorderCoordinator::toggleRecording()
    {
        if (m_recording)
        {
            stopRecording();
            return;
        }
        startRecording();
    }

    void StepRecorderCoordinator::togglePauseForTesting()
    {
        togglePause();
    }

    bool StepRecorderCoordinator::isRecording() const noexcept
    {
        return m_recording;
    }

    bool StepRecorderCoordinator::isPaused() const noexcept
    {
        return m_paused;
    }

    int StepRecorderCoordinator::stepCount() const noexcept
    {
        return static_cast<int>(m_session.count());
    }

    void StepRecorderCoordinator::flushPendingForTesting()
    {
        m_pendingTimer.stop();
        commitPending();
    }

    Core::StepSession *StepRecorderCoordinator::sessionForTesting() noexcept
    {
        return &m_session;
    }

    Gui::StepRecorderHud *StepRecorderCoordinator::hudForTesting() noexcept
    {
        return m_hud.get();
    }

    bool StepRecorderCoordinator::isReviewOpen() const noexcept
    {
        return !m_review.isNull();
    }

    Gui::StepReviewWindow *StepRecorderCoordinator::reviewWindowForTesting() const noexcept
    {
        return m_review.data();
    }

    void StepRecorderCoordinator::startRecording()
    {
        // 一次只能有一个步骤会话：审核窗还开着时再开始，把那一扇抬回来让用户先了结
        if (m_review)
        {
            m_review->show();
            m_review->raise();
            m_review->activateWindow();
            emit messageForUser(Base::uiText("先导出或放弃这次记录"), Gui::ToastKind::Information);
            return;
        }

        // 先看平台给不给：给不出时连首次说明都不该弹，更不该把钩子失败当成用户的错
        if (!m_dependencies.integration.capabilities().stepRecorder)
        {
            emit messageForUser(Base::uiText("当前平台不支持记录操作步骤，无法开始录制"), Gui::ToastKind::Information);
            return;
        }
        if (!ensureConsent())
        {
            return;
        }

        // 上一轮停止后会话要留给审核；重新开录时旧目录不能再占着临时空间
        if (!m_session.tempDirectory().empty())
        {
            m_session.cleanup();
        }
        // 不重用 begin 失败过的会话：它可能还带着上一轮的步骤
        m_session = Core::StepSession{};

        const auto directory = m_dependencies.tempRoot / std::filesystem::path{sessionDirectoryName(QDateTime::currentDateTime()).toStdWString()};
        if (const auto begun = m_session.begin(directory); !begun)
        {
            m_session.cleanup();
            emit messageForUser(errorText(begun.error()), Gui::ToastKind::Danger);
            return;
        }

        m_source = m_dependencies.eventSourceFactory ? m_dependencies.eventSourceFactory() : Platform::createStepEventSource();
        if (!m_source)
        {
            m_session.cleanup();
            emit messageForUser(Base::uiText("无法创建步骤事件源，请重启程序后重试"), Gui::ToastKind::Danger);
            return;
        }

        QObject::connect(m_source.get(), &Platform::IStepEventSource::stepEvent, this, &StepRecorderCoordinator::handleEvent);
        QObject::connect(m_source.get(), &Platform::IStepEventSource::failed, this, &StepRecorderCoordinator::handleSourceFailed);
        if (const auto started = m_source->start(); !started)
        {
            QObject::disconnect(m_source.get(), nullptr, this, nullptr);
            m_source.reset();
            m_session.cleanup();
            emit messageForUser(errorText(started.error()), Gui::ToastKind::Danger);
            return;
        }

        ensureHud();
        m_hud->setStepCount(0);
        m_hud->setPaused(false);
        showHud();

        m_recording = true;
        m_paused    = false;
        emit stateChanged(true, false, 0);
    }

    void StepRecorderCoordinator::stopRecording()
    {
        if (!m_recording)
        {
            return;
        }

        // 先丢待处理事件再卸钩子：按停止的那一下点击不能多记一步
        clearPending();
        if (m_source)
        {
            m_source->stop();
        }
        m_recording = false;
        m_paused    = false;
        if (m_hud)
        {
            m_hud->hide();
            m_hud->setPaused(false);
        }

        const int steps = stepCount();
        emit      stateChanged(false, false, steps);
        emit      stopped(steps);
    }

    void StepRecorderCoordinator::handleStopped(const int steps)
    {
        if (steps <= 0)
        {
            // 没记到步骤就没有可审核的内容：空目录当场收掉，也不开一扇没有东西的窗
            m_session.cleanup();
            emit messageForUser(Base::uiText("这次没有记录到步骤"), Gui::ToastKind::Information);
            return;
        }

        // 开关关着就与没有这一档时完全一致：一步都不扫，直接开审核窗
        if (!m_dependencies.settings.boolValue(Core::kStepsAutoRedact, false))
        {
            showReview();
            return;
        }

        // 只扫真的抓到图的那几步：抓屏失败的步骤本来就没有文件，也无从打码
        std::vector<std::filesystem::path> images;
        for (const auto &step: m_session.steps())
        {
            if (!step.imagePath.isEmpty())
            {
                images.emplace_back(step.imagePath.toStdWString());
            }
        }
        if (images.empty())
        {
            showReview();
            return;
        }

        if (!m_autoRedactor)
        {
            m_autoRedactor = std::make_unique<StepsAutoRedactor>(m_dependencies.settings, m_dependencies.integration);
        }
        // 扫完才开窗：审核窗里的缩略图与整图都是在打码之后重读的，用户看到的就是打过码的那一份
        emit messageForUser(Base::uiText("正在检查步骤截图里的敏感信息…"), Gui::ToastKind::Information);
        m_autoRedactor->run(std::move(images),
                            [this](const int, const int redacted, const int failed)
                            {
                                if (failed > 0)
                                {
                                    emit messageForUser(Base::uiText("有 %1 张步骤截图没能检查，请在审核窗里自行处理").arg(failed), Gui::ToastKind::Danger);
                                } else if (redacted > 0)
                                {
                                    emit messageForUser(Base::uiText("已对 %1 张步骤截图自动打码").arg(redacted), Gui::ToastKind::Success);
                                }
                                showReview();
                            });
    }

    void StepRecorderCoordinator::showReview()
    {
        if (m_review)
        {
            m_review->show();
            m_review->raise();
            m_review->activateWindow();
            return;
        }

        auto *review = new Gui::StepReviewWindow{m_session, m_dependencies.settings, [this] { return captureForManualStep(); }};
        // 关掉即自毁：协调器只持 QPointer；放弃信号先放掉这份引用，窗口随后自己收场
        review->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(review, &Gui::StepReviewWindow::discardConfirmed, this, [this] { m_review = nullptr; });
        m_review = review;
        review->show();
        review->raise();
        review->activateWindow();
    }

    Base::Expected<QImage> StepRecorderCoordinator::captureForManualStep()
    {
        // 审核窗在调用前后自己隐藏与恢复；这里只补控制条，且要等这一轮绘制，
        // 否则刚隐藏的控制条还会留在截图里
        const bool hudVisible = m_hud && m_hud->isVisible();
        if (hudVisible)
        {
            m_hud->hide();
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }

        const auto captured = m_dependencies.captureSource.grabScreen(QString{});

        if (hudVisible)
        {
            m_hud->show();
        }
        if (!captured)
        {
            return std::unexpected(captured.error());
        }
        return captured->image;
    }

    void StepRecorderCoordinator::togglePause()
    {
        if (!m_recording)
        {
            return;
        }

        if (!m_paused)
        {
            m_paused = true;
            // 暂停期间不该继续攒事件：钩子摘掉前已经排进来的那一下也丢掉
            clearPending();
            if (m_source)
            {
                m_source->stop();
            }
            if (m_hud)
            {
                m_hud->setPaused(true);
            }
            emit stateChanged(true, true, stepCount());
            return;
        }

        if (!m_source)
        {
            stopRecording();
            return;
        }
        if (const auto resumed = m_source->start(); !resumed)
        {
            const QString reason = errorText(resumed.error());
            stopRecording();
            emit messageForUser(reason, Gui::ToastKind::Danger);
            return;
        }

        m_paused = false;
        if (m_hud)
        {
            m_hud->setPaused(false);
        }
        emit stateChanged(true, false, stepCount());
    }

    void StepRecorderCoordinator::handleEvent(const Platform::StepInputEvent &event)
    {
        if (!m_recording || m_paused)
        {
            return;
        }

        const auto ownHandles = ownWindowHandles();
        if (event.windowHandle != 0 && std::find(ownHandles.begin(), ownHandles.end(), event.windowHandle) != ownHandles.end())
        {
            return;
        }

        PendingEvent pending;
        pending.event = event;

        if (event.kind == Platform::StepInputEvent::Kind::MouseClick)
        {
            const auto described = m_dependencies.integration.uiElementAtPoint(event.globalLogicalPoint, ownHandles);
            if (described)
            {
                pending.element = *described;
            } else
            {
                Base::Logger::warning(std::format("步骤记录取不到光标下的界面元素：{}", described.error().message));
            }
        }

        if (pending.element)
        {
            pending.windowTitle = pending.element->windowTitle;
            pending.processName = pending.element->processName;
        }
        if (pending.windowTitle.isEmpty() || pending.processName.isEmpty())
        {
            // 前台窗口只在事件到达时取：延迟到抓屏那一刻，用户可能已经切走了
            if (const auto foreground = m_dependencies.integration.foregroundWindow(); foreground && (event.windowHandle == 0 || foreground->handle == event.windowHandle))
            {
                if (pending.windowTitle.isEmpty())
                {
                    pending.windowTitle = foreground->title;
                }
                if (pending.processName.isEmpty())
                {
                    pending.processName = foreground->processName;
                }
            }
        }

        // 新事件替换旧的并重置计时：最后一下决定这一步
        m_pending = std::move(pending);
        m_pendingTimer.start(kCaptureDelayMs);
    }

    void StepRecorderCoordinator::handleSourceFailed(const Base::Error &error)
    {
        if (!m_recording)
        {
            return;
        }

        Base::Logger::error(std::format("步骤记录事件源故障：{}", error.message));
        clearPending();
        if (m_source)
        {
            m_source->stop();
        }
        m_recording = false;
        m_paused    = false;
        if (m_hud)
        {
            m_hud->hide();
            m_hud->setPaused(false);
        }
        // 监听已经不可信，这次会话的截图也一并清掉；故障不同于用户主动停止，不进入审核
        m_session.cleanup();
        emit stateChanged(false, false, 0);
        emit messageForUser(errorText(error), Gui::ToastKind::Danger);
    }

    void StepRecorderCoordinator::commitPending()
    {
        m_pendingTimer.stop();
        if (!m_pending)
        {
            return;
        }

        PendingEvent pending = std::move(*m_pending);
        m_pending.reset();
        if (!m_recording || m_paused)
        {
            return;
        }

        std::optional<Platform::ScreenWindow> window;
        QScreen                              *screen = resolveScreen(pending, window);

        Core::RecordedStep step;
        step.kind               = pending.event.kind == Platform::StepInputEvent::Kind::MouseClick ? Core::StepKind::Click : Core::StepKind::WindowChange;
        step.at                 = pending.event.at.isValid() ? pending.event.at : QDateTime::currentDateTime();
        step.globalLogicalPoint = pending.event.globalLogicalPoint;
        step.rightButton        = pending.event.rightButton;
        fillWindowIdentity(pending, window, step);

        if (step.kind == Core::StepKind::Click)
        {
            if (pending.element)
            {
                step.elementName = pending.element->name;
                step.controlType = pending.element->controlType;
            }
            step.description = Core::clickStepDescription(step.elementName, step.controlType, step.globalLogicalPoint, step.rightButton);
            if (screen != nullptr)
            {
                step.clickPointInImage     = pending.event.globalLogicalPoint - screen->geometry().topLeft();
                step.imageDevicePixelRatio = screen->devicePixelRatio();
            }
        } else
        {
            step.description = Core::windowChangeStepDescription(step.windowTitle, step.processName);
        }

        // 合并判定在抓屏之前：同一元素上的重复点击不值得再截一张图
        const auto &existing = m_session.steps();
        if (!existing.empty() && m_session.shouldMerge(existing.back(), step))
        {
            return;
        }

        // 点击也按事件点所在屏抓：250 毫秒里光标可能已经挪到别的屏，
        // 抓光标屏会让截图与 clickPointInImage、缩放因子对不上
        const QString screenName = screen != nullptr ? screen->name() : QString{};
        const auto    captured   = captureScreen(screenName);
        // 抓屏要等这一轮绘制：期间事件源可能报错清掉会话，或用户按了暂停；
        // 这一张图与这一步都不该再落进会话
        if (!m_recording || m_paused)
        {
            return;
        }
        if (!captured)
        {
            step.captureError = errorText(captured.error());
        }

        if (const auto appended = m_session.append(step); !appended)
        {
            emit messageForUser(errorText(appended.error()), Gui::ToastKind::Danger);
            stopRecording();
            return;
        }

        const std::size_t index = m_session.count() - 1;
        if (captured)
        {
            if (const auto attached = m_session.attachImage(index, captured->image); !attached)
            {
                // 临时图写不进去就停在这里：再录下去只会攒下一串没有图的步骤；已记的步骤保留
                Base::Logger::warning(std::format("步骤截图写不进临时目录：{}", attached.error().message));
                emit messageForUser(Base::uiText("步骤截图写不进临时目录，已停止本次记录：%1").arg(errorText(attached.error())), Gui::ToastKind::Danger);
                stopRecording();
                return;
            }
        }

        const int count = stepCount();
        if (m_hud)
        {
            m_hud->setStepCount(count);
        }
        emit stateChanged(true, m_paused, count);

        if (count >= Core::kMaximumSteps)
        {
            stopRecording();
            emit messageForUser(Base::uiText("已经记满 %1 步，先导出或放弃这次记录").arg(Core::kMaximumSteps), Gui::ToastKind::Information);
        }
    }

    void StepRecorderCoordinator::clearPending()
    {
        m_pendingTimer.stop();
        m_pending.reset();
    }

    bool StepRecorderCoordinator::ensureConsent()
    {
        if (m_dependencies.settings.boolValue(Core::kStepsConsentAccepted, false))
        {
            return true;
        }

        const auto provider = m_dependencies.consentProvider;
        const bool accepted = provider ? provider(nullptr) : askConsent(nullptr);
        if (!accepted)
        {
            return false;
        }

        if (const auto written = m_dependencies.settings.setValue(Core::kStepsConsentAccepted, true); !written)
        {
            // 写不进去只影响下次是否再问；用户已经同意，不该因此拒绝这一次录制
            Base::Logger::warning(std::format("步骤记录的同意状态写不进配置：{}", written.error().message));
        }
        return true;
    }

    void StepRecorderCoordinator::ensureHud()
    {
        if (m_hud)
        {
            return;
        }

        m_hud = std::make_unique<Gui::StepRecorderHud>(m_dependencies.theme);
        QObject::connect(m_hud.get(), &Gui::StepRecorderHud::pauseToggled, this, &StepRecorderCoordinator::togglePause);
        QObject::connect(m_hud.get(), &Gui::StepRecorderHud::stopRequested, this, &StepRecorderCoordinator::stopRecording);
    }

    void StepRecorderCoordinator::showHud()
    {
        const QScreen *screen    = QGuiApplication::primaryScreen();
        const QRect    available = screen != nullptr ? screen->availableGeometry() : QRect{};
        const QSize    size      = m_hud->preferredSize();
        const QPoint   position  = available.isValid() ? Gui::StepRecorderHud::positionFor(available, size) : QPoint{};
        m_hud->setGeometry(QRect{position, size});
        m_hud->show();
    }

    std::vector<quintptr> StepRecorderCoordinator::ownWindowHandles() const
    {
        std::vector<quintptr> handles;
        if (m_hud)
        {
            // winId 会把窗口落成原生句柄；控制条已经建好，这一步不会额外弹出什么
            if (const quintptr handle = m_hud->winId(); handle != 0)
            {
                handles.push_back(handle);
            }
        }
        if (m_review)
        {
            // 审核窗与录制窗同时开着的情形目前不会出现，但两条会话路径共用这套排除表，缺一条就会漏点
            if (const quintptr handle = m_review->winId(); handle != 0)
            {
                handles.push_back(handle);
            }
        }
        return handles;
    }

    QScreen *StepRecorderCoordinator::resolveScreen(const PendingEvent &pending, std::optional<Platform::ScreenWindow> &window) const
    {
        if (pending.event.kind == Platform::StepInputEvent::Kind::MouseClick)
        {
            if (QScreen *screen = QGuiApplication::screenAt(pending.event.globalLogicalPoint); screen != nullptr)
            {
                return screen;
            }
            return QGuiApplication::primaryScreen();
        }

        if (pending.event.windowHandle != 0)
        {
            if (const auto windows = m_dependencies.integration.screenWindows())
            {
                const auto found = std::find_if(windows->begin(), windows->end(),
                                                [&pending](const Platform::ScreenWindow &candidate) { return candidate.handle == pending.event.windowHandle; });
                if (found != windows->end())
                {
                    window = *found;
                }
            } else
            {
                Base::Logger::warning(std::format("步骤记录枚举窗口失败：{}", windows.error().message));
            }
        }

        if (window && window->logicalGlobal.isValid())
        {
            if (QScreen *screen = QGuiApplication::screenAt(window->logicalGlobal.center()); screen != nullptr)
            {
                return screen;
            }
        }
        return QGuiApplication::primaryScreen();
    }

    void StepRecorderCoordinator::fillWindowIdentity(const PendingEvent &pending, const std::optional<Platform::ScreenWindow> &window, Core::RecordedStep &step) const
    {
        step.windowTitle = pending.windowTitle;
        step.processName = pending.processName;

        if (window)
        {
            if (step.windowTitle.isEmpty())
            {
                step.windowTitle = window->title;
            }
            if (step.processName.isEmpty())
            {
                step.processName = window->processName;
            }
        }
        // 句柄为 0 的点击没有可核对的窗口：提交时再问前台，只会把 250 毫秒里刚切过去的窗口
        // 算到这一步头上；到达时缓存的标题与进程才是当时点到的窗口
        if (pending.event.windowHandle != 0 && (step.windowTitle.isEmpty() || step.processName.isEmpty()))
        {
            if (const auto foreground = m_dependencies.integration.foregroundWindow(); foreground && foreground->handle == pending.event.windowHandle)
            {
                if (step.windowTitle.isEmpty())
                {
                    step.windowTitle = foreground->title;
                }
                if (step.processName.isEmpty())
                {
                    step.processName = foreground->processName;
                }
            }
        }
    }

    Base::Expected<Base::CapturedImage> StepRecorderCoordinator::captureScreen(const QString &screenName)
    {
        const bool hudVisible = m_hud && m_hud->isVisible();
        if (hudVisible)
        {
            m_hud->hide();
        }
        // 隐藏要真的落到屏上：不等这一轮绘制，控制条就会留在截图里
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        const auto captured = m_dependencies.captureSource.grabScreen(screenName);
        // 抓屏期间可能已经因事件源故障收场，这时不能再把控制条摆回来
        if (hudVisible && m_recording)
        {
            m_hud->show();
        }
        return captured;
    }

} // namespace GyanisScribe::App
