#include "App/RewindCoordinator.h"

#include "App/CaptureCoordinator.h"
#include "Base/Logger.h"
#include "Base/Text.h"
#include "Core/Capture/IScreenCaptureSource.h"
#include "Core/Rewind/FrameChangeDetector.h"
#include "Core/Rewind/RewindFrameCodec.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Gui/Rewind/RewindPickerWindow.h"
#include "Platform/IPlatformIntegration.h"

#include <QCursor>
#include <QDateTime>
#include <QGuiApplication>
#include <QScreen>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <format>
#include <utility>

namespace
{
    /// 单次抓帧超过多少毫秒算慢：超过就跳过这一帧，连续三次暂停
    constexpr qint64 kSlowGrabThresholdMs = 50;

    /// 连续失败几次后暂停：一两次抖动不该打扰用户
    constexpr int kConsecutiveFailureLimit = 3;

    /// 设置键的分组前缀：这一组改动才需要重载回溯配置
    constexpr const char *kRewindSettingsPrefix = "Rewind/";
} // namespace

namespace GyanisScribe::App
{
    RewindCoordinator::RewindCoordinator(Dependencies dependencies, QObject *parent) :
        QObject(parent), m_dependencies(dependencies), m_buffer(Core::kDefaultRewindBufferSeconds, Core::kDefaultRewindFrameRate, Core::kRewindMemoryBudgetMb)
    {
        QObject::connect(&m_timer, &QTimer::timeout, this, &RewindCoordinator::onTick);
        QObject::connect(&m_dependencies.integration, &Platform::IPlatformIntegration::sessionLocked, this, &RewindCoordinator::handleSessionLocked);
        QObject::connect(&m_dependencies.integration, &Platform::IPlatformIntegration::sessionUnlocked, this, &RewindCoordinator::handleSessionUnlocked);
        QObject::connect(&m_dependencies.settings, &Core::ISettingsStore::valueChanged, this,
                         [this](const QString &key)
                         {
                             if (key.startsWith(QLatin1String{kRewindSettingsPrefix}))
                             {
                                 reloadFromSettings();
                             }
                         });
        QObject::connect(&m_encoderWatcher, &QFutureWatcher<Base::Expected<Core::RewindEncodedFrame>>::finished, this, &RewindCoordinator::handleEncoded);
        QObject::connect(&m_decoderWatcher, &QFutureWatcher<Base::Expected<QImage>>::finished, this, &RewindCoordinator::handleDecoded);

        for (auto *screen: QGuiApplication::screens())
        {
            watchScreenGeometry(screen);
        }
        QObject::connect(qGuiApp, &QGuiApplication::screenAdded, this,
                         [this](QScreen *screen)
                         {
                             watchScreenGeometry(screen);
                             handleScreenTopologyChanged();
                         });
        QObject::connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *) { handleScreenTopologyChanged(); });
    }

    RewindCoordinator::~RewindCoordinator()
    {
        m_timer.stop();
        m_buffer.clear();
        if (m_picker != nullptr)
        {
            m_picker->hide();
        }
        // 先断线再等：等待期间结果落地的话，finished 会打在正在析构的对象上
        QObject::disconnect(&m_encoderWatcher, nullptr, this, nullptr);
        QObject::disconnect(&m_decoderWatcher, nullptr, this, nullptr);
        m_encoderWatcher.waitForFinished();
        m_decoderWatcher.waitForFinished();
    }

    void RewindCoordinator::start()
    {
        const auto config = Core::readRewindConfig(m_dependencies.settings);
        if (!config)
        {
            Base::Logger::error(std::format("回溯配置读不出：{}", config.error().message));
            emit messageForUser(QString::fromStdString(config.error().message), Gui::ToastKind::Danger);
            setState(State::Disabled);
            return;
        }
        if (!config->enabled)
        {
            setState(State::Disabled);
            return;
        }
        if (!m_dependencies.integration.capabilities().sessionNotifications)
        {
            Base::Logger::warning("平台给不出锁屏通知，回溯缓冲不启用");
            emit messageForUser(Base::uiText("本平台给不出锁屏通知，出于隐私不开启回溯缓冲"), Gui::ToastKind::Information);
            setState(State::Disabled);
            return;
        }

        applyConfig(*config);
        m_clock.start();
        setState(State::Buffering);
    }

    void RewindCoordinator::stop()
    {
        m_timer.stop();
        m_buffer.clear();
        if (m_picker != nullptr)
        {
            m_picker->hide();
        }
        resetFrameState();
        m_screenResolved = false;
        setState(State::Disabled);
    }

    void RewindCoordinator::showPicker()
    {
        if (m_state == State::Disabled)
        {
            emit messageForUser(Base::uiText("回溯截图未开启。到设置 → 截图 → 回溯截图里开启（只存内存，不上传）。"), Gui::ToastKind::Information);
            return;
        }
        if (m_state == State::PickerOpen)
        {
            if (m_decoderWatcher.isRunning())
            {
                // 解码期间选择器是收起的：抬窗只会对着一个看不见的窗口使劲，改为说明当前进度
                emit messageForUser(Base::uiText("正在解码选中的帧，稍候。"), Gui::ToastKind::Information);
                return;
            }
            if (m_picker != nullptr && m_picker->isVisible())
            {
                m_picker->raise();
                m_picker->activateWindow();
                return;
            }
            // 选择器被收起过（例如没有可选帧）：按打开流程重摆，空缓冲会在里面被挡下
            openPicker();
            return;
        }
        if (m_state == State::CaptureFlow)
        {
            emit messageForUser(rewindBusyMessage(), Gui::ToastKind::Information);
            return;
        }
        if (m_buffer.stats().sampleCount == 0)
        {
            emit messageForUser(Base::uiText("回溯缓冲里还没有画面：刚开启时等一秒再按；若提示已暂停，先看暂停原因。"), Gui::ToastKind::Information);
            return;
        }
        if (m_encoderWatcher.isRunning() || m_pendingFrame.has_value())
        {
            // 编码马上结束：让它把最新一帧追加进来再开，选择器里就能看到刚发生的事；
            // 最多等一小会儿，编码太慢就先拿现有帧打开
            m_openPickerWhenEncoded = true;
            QTimer::singleShot(m_pickerWaitMs, this,
                               [this]
                               {
                                   if (!m_openPickerWhenEncoded)
                                   {
                                       return;
                                   }
                                   m_openPickerWhenEncoded = false;
                                   if (m_state == State::Disabled || m_state == State::PickerOpen || m_state == State::CaptureFlow)
                                   {
                                       return;
                                   }
                                   if (m_buffer.stats().sampleCount == 0)
                                   {
                                       return;
                                   }
                                   openPicker();
                               });
            return;
        }
        openPicker();
    }

    void RewindCoordinator::handleCaptureFlowFinished()
    {
        if (m_state == State::CaptureFlow)
        {
            setState(State::Buffering);
        }
    }

    void RewindCoordinator::clearBuffer()
    {
        if (m_state == State::Disabled)
        {
            return;
        }

        m_buffer.clear();
        resetFrameState();
        if (m_state == State::PickerOpen)
        {
            // 空胶片条没有可选项：收起选择器并回到抓帧，而不是留一个空窗
            if (m_picker != nullptr)
            {
                m_picker->hide();
            }
            setState(State::Buffering);
            return;
        }
        emit bufferStatusChanged(true, m_state != State::Buffering, m_stateReason);
    }

    void RewindCoordinator::reloadFromSettings()
    {
        const bool wasDisabled = m_state == State::Disabled;
        const auto config      = Core::readRewindConfig(m_dependencies.settings);
        if (!config)
        {
            // 名单非法时不能悄悄沿用旧配置重开：先让用户改对，再启动
            Base::Logger::error(std::format("回溯配置重读失败：{}", config.error().message));
            emit messageForUser(QString::fromStdString(config.error().message), Gui::ToastKind::Danger);
            return;
        }
        if (!config->enabled)
        {
            stop();
            return;
        }

        applyConfig(*config);
        if (wasDisabled)
        {
            start();
            return;
        }
        emit bufferStatusChanged(true, m_state != State::Buffering, m_stateReason);
    }

    void RewindCoordinator::captureTickForTesting()
    {
        onTick();
    }

    Core::RewindBufferStats RewindCoordinator::bufferStatsForTesting() const
    {
        return m_buffer.stats();
    }

    std::optional<quint64> RewindCoordinator::firstFrameIdForTesting() const
    {
        const auto views = m_buffer.views();
        if (views.empty())
        {
            return std::nullopt;
        }
        return views.front().id;
    }

    QDateTime RewindCoordinator::sampleCapturedAtForTesting(const quint64 id) const
    {
        const auto sample = m_buffer.sampleById(id);
        return sample ? sample->capturedAt : QDateTime{};
    }

    Core::RewindStatus RewindCoordinator::statusForTesting() const noexcept
    {
        // 选择器与框选是界面流程自己的暂停，对外状态枚举没有这两档；
        // 托盘看到的暂停由 bufferStatusChanged 单独表达，这里只回答抓帧能力
        switch (m_state)
        {
            case State::Disabled:
                return Core::RewindStatus::Disabled;
            case State::PausedByExclusion:
                return Core::RewindStatus::PausedByExclusion;
            case State::PausedByLock:
                return Core::RewindStatus::PausedByLock;
            case State::PausedByError:
                return Core::RewindStatus::PausedByError;
            case State::Buffering:
            case State::PickerOpen:
            case State::CaptureFlow:
                return Core::RewindStatus::Buffering;
        }
        return Core::RewindStatus::Disabled;
    }

    void RewindCoordinator::chooseFrameForTesting(const quint64 id)
    {
        chooseFrame(id);
    }

    void RewindCoordinator::setPickerWaitForTesting(const int milliseconds)
    {
        m_pickerWaitMs = std::max(0, milliseconds);
    }

    Core::RewindConfig RewindCoordinator::configForTesting() const
    {
        return m_config;
    }

    int RewindCoordinator::consecutiveEncodeFailuresForTesting() const noexcept
    {
        return m_consecutiveEncodeFailures;
    }

    bool RewindCoordinator::encodePendingForTesting() const noexcept
    {
        return m_pendingFrame.has_value();
    }

    quint64 RewindCoordinator::injectCorruptFrameForTesting()
    {
        Core::RewindFrameSample sample;
        sample.id          = m_nextSampleId;
        sample.monotonicMs = m_clock.elapsed();
        sample.capturedAt  = QDateTime::currentDateTime();
        // 不是任何图片格式的字节：解码器会直接报错，缓冲本身只检查字节非空
        sample.encodedPixels     = QByteArrayLiteral("truncated-by-test");
        sample.logicalSize       = m_screenRect.size();
        sample.devicePixelRatio  = 1.0;
        sample.screenName        = m_screenName;
        sample.screenLogicalRect = m_screenRect;
        if (auto appended = m_buffer.append(sample); !appended)
        {
            Base::Logger::warning(std::format("用例注入的损坏样本没进缓冲：{}", appended.error().message));
            return 0;
        }
        ++m_nextSampleId;
        return sample.id;
    }

    void RewindCoordinator::applyConfig(const Core::RewindConfig &config)
    {
        m_config = config;
        m_buffer.reconfigure(config.bufferSeconds, config.frameRate, config.memoryBudgetMb);
        m_timer.setInterval(std::max(1, 1000 / std::max(1, config.frameRate)));
    }

    void RewindCoordinator::setState(const State state, const QString &reason)
    {
        m_state = state;
        switch (state)
        {
            case State::Buffering:
                m_consecutiveFailures       = 0;
                m_consecutiveSlowGrabs      = 0;
                m_consecutiveEncodeFailures = 0;
                startCaptureTimer();
                break;
            case State::PausedByExclusion:
                // 定时器留着跑：下一次 tick 用它发现排除程序已经离开
                if (!m_timer.isActive())
                {
                    startCaptureTimer();
                }
                break;
            case State::Disabled:
                m_timer.stop();
                m_buffer.clear();
                if (m_picker != nullptr)
                {
                    m_picker->hide();
                }
                resetFrameState();
                m_screenResolved      = false;
                m_screenAvailableRect = QRect{};
                break;
            case State::PickerOpen:
            case State::CaptureFlow:
            case State::PausedByLock:
            case State::PausedByError:
                m_timer.stop();
                break;
        }

        if (state == State::Disabled || state == State::PausedByLock || state == State::PausedByError)
        {
            m_openPickerWhenEncoded = false;
        }

        const bool enabled = state != State::Disabled;
        const bool paused  = enabled && state != State::Buffering;
        m_stateReason      = paused ? reason : QString{};
        emit bufferStatusChanged(enabled, paused, m_stateReason);
        if (state == State::PausedByError)
        {
            // 同一条原因只弹一次：从选择器取消回到错误暂停只是刷新托盘读数，
            // 用户不需要为同一个错误看第二遍同样的话
            if (reason != m_announcedErrorReason)
            {
                m_announcedErrorReason = reason;
                emit messageForUser(Base::uiText("回溯缓冲已暂停：") + reason + Base::uiText(" 调整后重新开启回溯缓冲即可恢复。"), Gui::ToastKind::Danger);
            }
        } else if (state == State::Buffering || state == State::Disabled)
        {
            // 回到正常或关闭之后，同样的错误再出现就是新一次故障，值得再说一次
            m_announcedErrorReason.clear();
        }
    }

    void RewindCoordinator::startCaptureTimer()
    {
        m_timer.start(std::max(1, 1000 / std::max(1, m_config.frameRate)));
    }

    void RewindCoordinator::onTick()
    {
        if (m_state == State::PausedByExclusion)
        {
            if (exclusionVisible(m_screenRect))
            {
                return;
            }
            setState(State::Buffering);
        }
        if (m_state != State::Buffering)
        {
            return;
        }

        auto *screen = QGuiApplication::screenAt(QCursor::pos());
        if (screen == nullptr)
        {
            setState(State::PausedByError, Base::uiText("找不到光标所在的屏幕"));
            return;
        }
        // 离屏平台的屏名可能为空，所以首次解析不能只靠名字比较
        if (!m_screenResolved || screen->name() != m_screenName)
        {
            const bool switched   = m_screenResolved;
            m_screenName          = screen->name();
            m_screenRect          = screen->geometry();
            m_screenAvailableRect = screen->availableGeometry();
            m_screenResolved      = true;
            m_buffer.clear();
            resetFrameState();
            Base::Logger::info(std::format("回溯改为抓屏 {}（{}×{}）", m_screenName.toStdString(), m_screenRect.width(), m_screenRect.height()));
            if (switched)
            {
                // 换屏清空了旧缓冲，托盘那行读数要跟着重发一次；屏名不在托盘读数的三个参数里，
                // 只能说给用户听，否则「换到哪块屏」这条信息只剩日志里能看到
                emit bufferStatusChanged(true, false, QString{});
                emit messageForUser(Base::uiText("回溯改为抓屏幕「") + m_screenName + Base::uiText("」，旧缓冲已清空。"), Gui::ToastKind::Information);
            }
        }
        if (exclusionVisible(m_screenRect))
        {
            setState(State::PausedByExclusion, Base::uiText("排除的程序在前台"));
            return;
        }
        // 在途帧的结果还没回接时也不能开新编码：换掉 future 会让那一份结果落到新帧头上
        if (m_encoderWatcher.isRunning() || m_pendingFrame.has_value() || m_decoderWatcher.isRunning())
        {
            return;
        }

        QElapsedTimer grabTimer;
        grabTimer.start();
        const auto frame = m_dependencies.source.tryGrabScreen(m_screenName);
        ++m_capturedFrames;
        if (!frame)
        {
            if (++m_consecutiveFailures >= kConsecutiveFailureLimit)
            {
                setState(State::PausedByError, QString::fromStdString(frame.error().message));
            }
            return;
        }
        m_consecutiveFailures = 0;
        if (grabTimer.elapsed() > kSlowGrabThresholdMs)
        {
            if (++m_consecutiveSlowGrabs >= kConsecutiveFailureLimit)
            {
                setState(State::PausedByError, Base::uiText("抓帧太慢，回溯缓冲已暂停"));
            }
            return;
        }
        m_consecutiveSlowGrabs = 0;

        QString title;
        if (const auto foreground = m_dependencies.integration.foregroundWindow(); foreground)
        {
            title = foreground->title;
        }
        const auto fingerprint = Core::makeFrameFingerprint(frame->image, title);

        if (m_lastEncoded && m_lastFingerprint && Core::isSameFrame(fingerprint, *m_lastFingerprint))
        {
            Core::RewindFrameSample heartbeat;
            heartbeat.id                = m_nextSampleId++;
            heartbeat.monotonicMs       = m_clock.elapsed();
            heartbeat.capturedAt        = QDateTime::currentDateTime();
            heartbeat.encodedPixels     = m_lastEncoded->encodedPixels;
            heartbeat.thumbnail         = m_lastEncoded->thumbnail;
            heartbeat.logicalSize       = frame->logicalSize;
            heartbeat.devicePixelRatio  = frame->devicePixelRatio;
            heartbeat.screenName        = frame->screenName;
            heartbeat.screenLogicalRect = frame->logicalRegion.isEmpty() ? m_screenRect : frame->logicalRegion;
            heartbeat.foregroundTitle   = title;
            // 画面没变也可能换了前台窗口；这类样本仍标候选，但不重新编码
            heartbeat.isCandidate = Core::isCandidateChange(fingerprint, *m_lastFingerprint);
            if (auto appended = m_buffer.append(std::move(heartbeat)); !appended)
            {
                setState(State::PausedByError, QString::fromStdString(appended.error().message));
                return;
            }
            m_lastFingerprint = fingerprint;
            // 心跳说明状态机还在正常出样本：两次失败之间隔着一段静止画面就不该累计到暂停
            m_consecutiveEncodeFailures = 0;
            return;
        }

        startEncode(*frame, fingerprint);
    }

    void RewindCoordinator::startEncode(const Base::CapturedImage &frame, const Core::FrameFingerprint &fingerprint)
    {
        if (m_pendingFrame.has_value() || m_encoderWatcher.isRunning())
        {
            // 上一帧还在等结果：再提交一次会把它覆盖掉，那一份结果回接时就没有对应的帧可拼
            return;
        }
        m_pendingFrame       = frame;
        m_pendingFingerprint = fingerprint;
        m_pendingMonotonicMs = m_clock.elapsed();
        const auto image     = frame.image;
        m_encoderWatcher.setFuture(QtConcurrent::run([image] { return Core::encodeRewindFrame(image); }));
    }

    void RewindCoordinator::appendEncoded(const Core::RewindEncodedFrame &encoded)
    {
        if (!m_pendingFrame || !m_pendingFingerprint)
        {
            return;
        }

        Core::RewindFrameSample sample;
        sample.id                = m_nextSampleId++;
        sample.monotonicMs       = m_pendingMonotonicMs;
        sample.capturedAt        = QDateTime::currentDateTime();
        sample.encodedPixels     = encoded.encodedPixels;
        sample.thumbnail         = encoded.thumbnail;
        sample.logicalSize       = m_pendingFrame->logicalSize;
        sample.devicePixelRatio  = m_pendingFrame->devicePixelRatio;
        sample.screenName        = m_pendingFrame->screenName;
        sample.screenLogicalRect = m_pendingFrame->logicalRegion.isEmpty() ? m_screenRect : m_pendingFrame->logicalRegion;
        sample.isCandidate       = true;
        sample.foregroundTitle   = m_pendingFingerprint->foregroundTitle;

        m_lastEncoded     = encoded;
        m_lastFingerprint = m_pendingFingerprint;
        m_pendingFrame.reset();
        m_pendingFingerprint.reset();

        if (auto appended = m_buffer.append(std::move(sample)); !appended)
        {
            setState(State::PausedByError, QString::fromStdString(appended.error().message));
            return;
        }
        if (m_openPickerWhenEncoded)
        {
            m_openPickerWhenEncoded = false;
            openPicker();
        }
    }

    void RewindCoordinator::openPicker()
    {
        const auto views = m_buffer.views();
        if (views.empty())
        {
            // 一条可展示的样本都没有：收起选择器并回到打开前的状态，
            // 不能把窗口藏起来却仍停在 PickerOpen，热键会以为选择器还开着
            if (m_picker != nullptr)
            {
                m_picker->hide();
            }
            setState(stateAfterPickerClosed(), m_reasonBeforePicker);
            emit messageForUser(Base::uiText("回溯缓冲里已经没有画面，选择器收起了。"), Gui::ToastKind::Information);
            return;
        }

        if (m_state != State::PickerOpen)
        {
            m_stateBeforePicker  = m_state;
            m_reasonBeforePicker = m_stateReason;
        }
        setState(State::PickerOpen, Base::uiText("回溯选择器已打开"));
        if (m_picker == nullptr)
        {
            m_picker = std::make_unique<Gui::RewindPickerWindow>();
            QObject::connect(m_picker.get(), &Gui::RewindPickerWindow::frameChosen, this, &RewindCoordinator::chooseFrame);
            QObject::connect(m_picker.get(), &Gui::RewindPickerWindow::dismissed, this, &RewindCoordinator::handlePickerDismissed);
        }
        // 摆放按可用区（避开任务栏），抓帧与样本上的屏矩形仍是整屏
        m_picker->showFrames(views, m_screenAvailableRect.isEmpty() ? m_screenRect : m_screenAvailableRect, m_screenName, false, {});
    }

    void RewindCoordinator::chooseFrame(const quint64 id)
    {
        if (m_state == State::Disabled || m_decoderWatcher.isRunning())
        {
            return;
        }
        const auto sample = m_buffer.sampleById(id);
        if (!sample)
        {
            emit messageForUser(Base::uiText("这一帧已经从回溯缓冲里淘汰了，请在胶片条里另选一帧。"), Gui::ToastKind::Information);
            return;
        }

        m_pendingChosenId = id;
        if (m_state == State::PickerOpen && m_picker != nullptr)
        {
            // 解码期间先收起胶片条：成功就直接进框选，失败再摆回来
            m_picker->hide();
        }
        const auto encodedPixels = sample->encodedPixels;
        m_decoderWatcher.setFuture(QtConcurrent::run([encodedPixels] { return Core::decodeRewindFrame(encodedPixels); }));
    }

    void RewindCoordinator::handlePickerDismissed()
    {
        m_pendingChosenId.reset();
        if (m_state != State::PickerOpen)
        {
            return;
        }
        // 取消只是收起选择器：排除程序还在前台、或错误还没排除时，暂停本来就该继续，
        // 回到暂停态而不是借取消把暂停一并解除
        setState(stateAfterPickerClosed(), m_reasonBeforePicker);
    }

    RewindCoordinator::State RewindCoordinator::stateAfterPickerClosed() const noexcept
    {
        State resume = m_state == State::PickerOpen ? m_stateBeforePicker : m_state;
        if (resume == State::Disabled || resume == State::PickerOpen || resume == State::CaptureFlow)
        {
            resume = State::Buffering;
        }
        return resume;
    }

    void RewindCoordinator::handleEncoded()
    {
        const auto encoded = m_encoderWatcher.result();
        if (m_state == State::Disabled)
        {
            m_pendingFrame.reset();
            m_pendingFingerprint.reset();
            return;
        }
        if (!m_pendingFrame || !m_pendingFingerprint)
        {
            // 关闭、锁屏或清空之后才回来的结果：画面已经不该再进缓冲
            return;
        }
        if (!encoded)
        {
            Base::Logger::warning(std::format("回溯帧编码失败：{}", encoded.error().message));
            m_pendingFrame.reset();
            m_pendingFingerprint.reset();
            if (++m_consecutiveEncodeFailures >= kConsecutiveFailureLimit)
            {
                setState(State::PausedByError, QString::fromStdString(encoded.error().message));
            } else if (m_openPickerWhenEncoded && m_buffer.stats().sampleCount > 0)
            {
                // 热键在等编码，但这一帧没成：用已有帧开选择器，不让热键看起来没反应
                m_openPickerWhenEncoded = false;
                openPicker();
            }
            return;
        }
        m_consecutiveEncodeFailures = 0;
        appendEncoded(*encoded);
    }

    void RewindCoordinator::handleDecoded()
    {
        const auto decoded = m_decoderWatcher.result();
        const auto id      = std::exchange(m_pendingChosenId, std::nullopt);
        if (!id.has_value() || m_state == State::Disabled)
        {
            return;
        }

        if (!decoded)
        {
            Base::Logger::warning(std::format("回溯帧解码失败：{}", decoded.error().message));
            emit messageForUser(Base::uiText("这一帧解码失败，请在胶片条里重选一帧：") + QString::fromStdString(decoded.error().message), Gui::ToastKind::Danger);
            if (m_state == State::PickerOpen)
            {
                openPicker();
            }
            return;
        }

        const auto sample = m_buffer.sampleById(*id);
        if (!sample)
        {
            emit messageForUser(Base::uiText("这一帧已经从回溯缓冲里淘汰了，请在胶片条里另选一帧。"), Gui::ToastKind::Information);
            if (m_state == State::PickerOpen)
            {
                openPicker();
            }
            return;
        }

        Base::CapturedImage frame;
        frame.image            = *decoded;
        frame.logicalSize      = sample->logicalSize;
        frame.devicePixelRatio = sample->devicePixelRatio;
        frame.screenName       = sample->screenName;
        frame.logicalRegion    = sample->screenLogicalRect;
        frame.capturedAt       = sample->capturedAt;

        if (m_picker != nullptr)
        {
            m_picker->hide();
        }
        setState(State::CaptureFlow, Base::uiText("回溯框选进行中"));
        emit frameChosen(frame);
    }

    void RewindCoordinator::handleSessionLocked()
    {
        if (m_state == State::Disabled)
        {
            m_buffer.clear();
            resetFrameState();
            return;
        }
        m_buffer.clear();
        resetFrameState();
        if (m_picker != nullptr)
        {
            m_picker->hide();
        }
        m_screenResolved = false;
        setState(State::PausedByLock, Base::uiText("已锁屏，回溯缓冲已清空"));
    }

    void RewindCoordinator::handleSessionUnlocked()
    {
        if (m_state != State::PausedByLock)
        {
            return;
        }
        if (!m_config.enabled)
        {
            setState(State::Disabled);
            return;
        }
        m_screenResolved = false;
        m_clock.start();
        setState(State::Buffering);
    }

    void RewindCoordinator::handleScreenTopologyChanged()
    {
        if (m_state == State::Disabled)
        {
            return;
        }
        Base::Logger::info(std::format("显示器布局变化：回溯缓冲清空并重新开始（已抓 {} 帧）", m_capturedFrames));
        m_buffer.clear();
        resetFrameState();
        m_screenResolved      = false;
        m_screenRect          = QRect{};
        m_screenAvailableRect = QRect{};
        m_screenName.clear();
        if (m_state == State::PickerOpen)
        {
            if (m_picker != nullptr)
            {
                m_picker->hide();
            }
            setState(State::Buffering);
            return;
        }
        emit bufferStatusChanged(true, m_state != State::Buffering, m_stateReason);
    }

    void RewindCoordinator::watchScreenGeometry(QScreen *screen)
    {
        if (screen == nullptr)
        {
            return;
        }
        QObject::connect(screen, &QScreen::geometryChanged, this, [this](const QRect &) { handleScreenTopologyChanged(); });
    }

    void RewindCoordinator::resetFrameState()
    {
        m_lastEncoded.reset();
        m_lastFingerprint.reset();
        m_pendingFrame.reset();
        m_pendingFingerprint.reset();
        m_pendingChosenId.reset();
        m_openPickerWhenEncoded          = false;
        m_consecutiveFailures            = 0;
        m_consecutiveSlowGrabs           = 0;
        m_consecutiveEncodeFailures      = 0;
        m_windowEnumerationFailureLogged = false;
    }

    bool RewindCoordinator::exclusionVisible(const QRect &screenRect) const
    {
        if (m_config.excludedProcesses.isEmpty() || screenRect.isEmpty())
        {
            return false;
        }
        const auto windows = m_dependencies.integration.screenWindows();
        if (!windows)
        {
            if (!m_windowEnumerationFailureLogged)
            {
                Base::Logger::warning(std::format("回溯排除名单这次没生效：枚举窗口失败（{}）", windows.error().message));
                m_windowEnumerationFailureLogged = true;
            }
            return false;
        }
        m_windowEnumerationFailureLogged = false;
        for (const auto &window: *windows)
        {
            if (window.logicalGlobal.intersects(screenRect) && Core::isExcludedProcessName(window.processName, m_config.excludedProcesses))
            {
                return true;
            }
        }
        return false;
    }
} // namespace GyanisScribe::App
