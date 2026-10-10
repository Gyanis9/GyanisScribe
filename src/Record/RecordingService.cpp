#include "Record/RecordingService.h"

#include "Capture/ScreenUtils.h"
#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Text.h"
#include "Record/CursorHighlight.h"
#include "Record/FfmpegCommand.h"

#include <QCursor>
#include <QDateTime>
#include <QProcess>
#include <QScreen>

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>

namespace
{
    /// 定时器比目标帧率多要一点余量：正好按 1000/帧率 排会让偶发的一次迟到的抓帧直接跳过一整格
    constexpr int kTimerSlackMs = 4;

    /// 等 ffmpeg 起来的最长时间：设备被占用时它几乎立刻就退，等满只为防机器卡住
    constexpr int kAudioStartTimeoutMs = 3000;

    /// 发了收尾的 q 之后最多再等多久：ffmpeg 关掉容器是毫秒级的事
    constexpr int kAudioStopTimeoutMs = 5000;

    /// wav 的头是 44 字节，只有头的文件等于没录到任何采样
    constexpr auto kWavHeaderBytes = static_cast<std::uintmax_t>(44);

    /**
     * @brief 这一块屏左上角在全局逻辑坐标里的位置
     * @param screenName 帧的来源屏幕名
     * @return 全局原点；屏找不到时返回空
     * @details 光标读数是整个虚拟桌面的全局坐标，而一帧只覆盖一块屏（或屏上一块选区），
     *          减掉这一块的原点才知道它落在帧里的第几格。找不到屏就不画：
     *          把圈画到别的位置比不画更误导人。
     */
    std::optional<QPoint> screenGlobalOrigin(const QString &screenName)
    {
        const auto *screen = GyanisScribe::Capture::screenByName(screenName);
        if (screen == nullptr)
        {
            return {};
        }
        return screen->geometry().topLeft();
    }
} // namespace

namespace GyanisScribe::Record
{
    std::optional<QPoint> windowFrameOrigin(const std::vector<Platform::ScreenWindow> &windows, const quintptr handle)
    {
        if (handle == 0)
        {
            return std::nullopt;
        }

        // 只认客户区那一份：抓下来的那一帧就是客户区的像素，整窗读数带着标题栏，用它当帧原点会把圈画歪
        for (const auto &window: windows)
        {
            if (window.handle == handle)
            {
                if (window.clientLogicalGlobal.isNull())
                {
                    return std::nullopt;
                }
                return window.clientLogicalGlobal.topLeft();
            }
        }
        return std::nullopt;
    }

    RecordingService::RecordingService(Capture::IScreenCaptureSource &source, IRecordingEncoder &encoder, Platform::IPlatformIntegration &integration, QObject *parent) :
        QObject(parent), m_source(source), m_encoder(encoder), m_integration(integration)
    {
        m_timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_timer, &QTimer::timeout, this, &RecordingService::onTick);
    }

    RecordingService::~RecordingService()
    {
        // 对象要走了而 ffmpeg 还在取声：强杀，不给它留一个「销毁了进程还在跑」的尾巴
        if (m_audio != nullptr && m_audio->state() != QProcess::NotRunning)
        {
            m_audio->kill();
            m_audio->waitForFinished(1000);
        }

        // 还在编的那几份要等：那条线程用的是 m_encoder 的引用与带过来的快照，
        // 不等就是让它对着已经析构的服务继续编（回接那一段由 context 自动不再投递）
        for (auto &running: m_runningEncodes)
        {
            running.waitForFinished();
        }
        m_runningEncodes.clear();
    }

    Core::Expected<void> RecordingService::start(const RecorderConfig &config, const int countdownSeconds)
    {
        if (m_session != nullptr && m_session->state() != RecordingState::Done && m_session->state() != RecordingState::Failed)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::Busy, GyanisScribe::Core::uiTextUtf8("上一轮录屏还没收尾。请先停止它，再开始新的一轮")});
        }

        if (auto valid = validateRecorderConfig(config); !valid)
        {
            return valid;
        }

        // 成品目录在这一步就建出来：编码落在几十秒之后，那时才发现写不进去，这一轮录下的帧就白丢了
        if (auto ensured = Core::Paths::ensureDirectory(config.outputDirectory); !ensured)
        {
            return std::unexpected(ensured.error());
        }

        // 临时帧目录带时间戳：两轮录屏不会写进同一个目录，编码器也就不会把上一轮的残帧算进来
        const auto directory =
                Core::Paths::cacheDirectory() / std::format("Recording/{}", QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")).toStdString());
        if (auto ensured = Core::Paths::ensureDirectory(directory); !ensured)
        {
            return std::unexpected(ensured.error());
        }

        m_config         = config;
        m_frameDirectory = directory;
        // 每一轮重新开始报：上一轮报过的原因（比如屏被拔掉）不代表这一轮也成立
        m_cursorHighlightWarned = false;
        m_sink                  = std::make_unique<PngSequenceSink>(directory);

        // 取声起不动就整轮拒绝开始：交出一个安静的成品，比根本不录更容易让人误会
        if (const auto audio = startAudioCapture(); !audio)
        {
            std::error_code cleanup;
            std::filesystem::remove_all(directory, cleanup);
            return std::unexpected(audio.error());
        }
        // 节拍从这一刻起算：会话拿到的所有时间都是同一个单调时钟的读数
        m_clock.start();
        m_session = std::make_unique<RecordingSession>(config, *m_sink, m_clock.elapsed(), countdownSeconds);

        // 间隔取 1000/帧率 再加几毫秒余量：抓屏本身就有抖动，排太紧只会让帧率抖动被放大成丢帧
        const auto interval = static_cast<int>(1000 / std::max(1, config.frameRate)) + kTimerSlackMs;
        m_timer.start(interval);
        Core::Logger::info(
                std::format("录屏开始：帧率 {}，上限 {} 秒，临时帧目录 {}", config.frameRate, config.maximumDurationSeconds, Core::Paths::toDisplayText(directory).toStdString()));
        return {};
    }

    void RecordingService::pause()
    {
        if (m_session != nullptr)
        {
            m_session->pause(m_clock.elapsed());
        }
    }

    void RecordingService::resume()
    {
        if (m_session != nullptr)
        {
            m_session->resume(m_clock.elapsed());
        }
    }

    Core::Expected<void> RecordingService::stop()
    {
        if (m_session == nullptr)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("当前没有正在进行的录屏")});
        }

        m_timer.stop();
        const auto now    = m_clock.elapsed();
        const auto frames = m_session->storedFrames();
        auto       sealed = m_session->finish(now);
        if (!sealed)
        {
            emit failed(QString::fromUtf8(sealed.error().message.c_str()));
            return std::unexpected(sealed.error());
        }

        // 收尾这一串里只有编码要几秒到几十秒，所以只把那一段搬走：帧序列已经写完、取声进程也已经收尸，
        // 剩下的「编成成品 + 处置中间文件」由 deliverEncoding 在界面线程上接着做
        startEncoding(m_config, m_frameDirectory, frames, finishAudioCapture());
        return {};
    }

    Core::Expected<void> RecordingService::cancel()
    {
        const auto state = m_session == nullptr ? RecordingState::Idle : m_session->state();
        // 只对「还在要帧」的那三档动手：stop() 之后的那一轮正在后台读帧目录编成品，
        // 这时候删等于把它的输入抽走，用户拿到的将是「编码失败」而不是「我取消了」
        if (state != RecordingState::Countdown && state != RecordingState::Recording && state != RecordingState::Paused)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument,
                                               state == RecordingState::Idle
                                                       ? GyanisScribe::Core::uiTextUtf8("当前没有正在进行的录屏")
                                                       : GyanisScribe::Core::uiTextUtf8("这一轮已经停止并在编成品：取消只对在录的那几秒有效，不想要成品就等它编完再删")});
        }

        m_timer.stop();
        // 取声的进程照常收尸：留着一个还在写盘的 ffmpeg，删掉的文件会被它重新造出来
        const auto      discardedWav = finishAudioCapture();
        std::error_code ignored;
        if (!discardedWav.empty())
        {
            std::filesystem::remove(discardedWav, ignored);
        }
        std::filesystem::remove_all(m_frameDirectory, ignored);
        m_frameDirectory.clear();
        m_audioWav.clear();
        // 会话先走、去处再走：会话拿着那一份的引用。两者都交回去之后 state() 回到 Idle，
        // 控制条与托盘的读数才不说谎，下一轮也不会被「上一轮还没收尾」拦住
        m_session.reset();
        m_sink.reset();
        Core::Logger::info("录屏已取消：临时帧与中间音频都删掉了，这一轮不出成品");
        emit aborted();
        return {};
    }

    void RecordingService::startEncoding(const RecorderConfig &config, const std::filesystem::path &frameDirectory, const int frames, const std::filesystem::path &audioWav)
    {
        auto &encoder = m_encoder;
        // 参数与目录都按快照带过去：期间用户重新开一轮会覆盖成员那两份，而这一次编码认的是叫它出来的那一轮
        auto future = QtConcurrent::run([&encoder, config, frameDirectory, frames, audioWav] { return encoder.encode(config, frameDirectory, frames, audioWav); });

        // continuation 的 context 是本对象：回接落在界面线程，删目录与发信号都在这条线上做
        future.then(this, [this, frameDirectory, audioWav](const EncodeOutcome &outcome) { deliverEncoding(outcome, frameDirectory, audioWav); });

        m_runningEncodes.push_back(future);
        // 顺手收掉已经跑完的那些：这份列表只为析构时「还有谁在编」服务，不该越攒越长
        std::erase_if(m_runningEncodes, [](const QFuture<EncodeOutcome> &running) { return running.isFinished(); });
    }

    void RecordingService::deliverEncoding(const EncodeOutcome &outcome, const std::filesystem::path &frameDirectory, const std::filesystem::path &audioWav)
    {
        std::erase_if(m_runningEncodes, [](const QFuture<EncodeOutcome> &running) { return running.isFinished(); });

        if (!outcome)
        {
            // 保留临时帧：这已经是录完的内容，删掉就等于让用户白录一次
            Core::Logger::warning(std::format("录屏编码失败，临时帧保留在 {} 以便重编码", Core::Paths::toDisplayText(frameDirectory).toStdString()));
            emit failed(QString::fromUtf8(outcome.error().message.c_str()));
            return;
        }

        // 成品已含音轨，中间 wav 就没用了；帧目录按原规则清，失败时两者都留着好重编码
        if (!audioWav.empty())
        {
            std::error_code wavCleanup;
            std::filesystem::remove(audioWav, wavCleanup);
        }

        std::error_code cleanupError;
        std::filesystem::remove_all(frameDirectory, cleanupError);
        if (cleanupError)
        {
            // 成品已经有了，这里只报一条：清不掉是磁盘占用问题，不是录屏失败
            Core::Logger::warning(std::format("临时帧目录没清干净：{}（{}）", Core::Paths::toDisplayText(frameDirectory).toStdString(), cleanupError.message()));
        }

        emit finished(Core::Paths::toDisplayText(*outcome));
    }

    RecordingState RecordingService::state() const noexcept
    {
        return m_session == nullptr ? RecordingState::Idle : m_session->state();
    }

    int RecordingService::recordedSeconds() const
    {
        if (m_session == nullptr)
        {
            return 0;
        }
        return static_cast<int>(m_session->recordedDurationMs(m_clock.elapsed()) / 1000);
    }

    Core::Expected<Core::CapturedImage> RecordingService::grabFrame()
    {
        if (m_config.mode == RecordMode::Window)
        {
            auto grabbed = m_source.grabWindow(m_config.windowHandle);
            if (!grabbed)
            {
                return std::unexpected(grabbed.error());
            }

            if (m_config.cursorHighlight)
            {
                // 每帧现问一次：窗口会被拖走，缓存上一帧的位置就是把圈画到它已经不在的那个地方
                const auto windows = m_integration.screenWindows();
                const auto origin  = windows ? windowFrameOrigin(*windows, m_config.windowHandle) : std::optional<QPoint>{};
                if (origin)
                {
                    overlayCursorHighlight(*grabbed, *origin);
                } else if (!m_cursorHighlightWarned)
                {
                    m_cursorHighlightWarned = true;
                    Core::Logger::warning("窗口录屏圈不出光标：平台这一帧没给得出那扇窗的客户区位置（窗口没在清单里、或本平台不枚举窗口）。请改用整屏或选区录屏");
                }
            }
            return grabbed;
        }

        auto grabbed = m_source.grabScreen(m_config.screenName);
        if (!grabbed)
        {
            return std::unexpected(grabbed.error());
        }

        if (m_config.mode == RecordMode::FullScreen)
        {
            // 认不出这块屏就跳过画圈：把圈画到别的位置比不画更误导人
            if (const auto origin = screenGlobalOrigin(grabbed->screenName); origin)
            {
                overlayCursorHighlight(*grabbed, *origin);
            }
            return grabbed;
        }

        // 选区模式在整屏帧上裁：抓屏接口只给整屏与窗口两种，逐区抓屏要等平台上的 WGC 接入才有
        auto cropped = Capture::cropToLogicalRect(*grabbed, m_config.region);
        if (cropped)
        {
            // 裁出来的这一帧离全局原点的距离 = 屏的原点 + 选区在屏内的位置，两段都要算进去
            if (const auto origin = screenGlobalOrigin(grabbed->screenName); origin)
            {
                overlayCursorHighlight(*cropped, *origin + m_config.region.topLeft());
            }
        }
        return cropped;
    }

    void RecordingService::overlayCursorHighlight(Core::CapturedImage &frame, const QPoint &frameGlobalOriginLogical)
    {
        if (!m_config.cursorHighlight)
        {
            return;
        }

        const auto painted = paintCursorHighlight(frame.image, cursorGlobalPosition(), frameGlobalOriginLogical, frame.devicePixelRatio);
        if (!painted && !m_cursorHighlightWarned)
        {
            m_cursorHighlightWarned = true;
            Core::Logger::warning(std::format("光标高亮没画出来：{}", painted.error().message));
        }
    }

    QPoint RecordingService::cursorGlobalPosition() const
    {
        return QCursor::pos();
    }

    void RecordingService::onTick()
    {
        if (m_session == nullptr)
        {
            m_timer.stop();
            return;
        }

        const auto now   = m_clock.elapsed();
        const auto frame = grabFrame();
        if (!frame)
        {
            // 抓不到帧通常是屏幕没了：这种情况直接收，别录一份一直空转的成品
            const auto reason = frame.error().message;
            Core::Logger::error(std::format("录屏抓帧失败，这一轮中止：{}", reason));
            m_timer.stop();
            emit failed(QString::fromUtf8(reason.c_str()));
            return;
        }

        const auto tick = m_session->tick(now, frame->image);
        if (tick.countdownRemainingSeconds > 0)
        {
            emit countdownChanged(tick.countdownRemainingSeconds);
        }
        if (tick.frameStored)
        {
            emit frameStored(m_session->storedFrames() - 1);
        }
        if (tick.state == RecordingState::Failed)
        {
            m_timer.stop();
            emit failed(QString::fromUtf8(m_session->failure().message.c_str()));
            return;
        }
        if (tick.durationReached)
        {
            // 触到上限自动收尾：用户可能根本没看着屏幕，继续录只是把磁盘写满
            m_timer.stop();
            const auto stopped = stop();
            if (!stopped)
            {
                emit failed(QString::fromUtf8(stopped.error().message.c_str()));
            }
        }
    }

    Core::Expected<void> RecordingService::startAudioCapture()
    {
        m_audioOutcome = AudioCaptureOutcome::NotRequested;
        m_audioWav.clear();
        if (m_config.microphoneDevice.isEmpty() && m_config.systemSoundDevice.isEmpty())
        {
            return {};
        }

        // 存的是 MP4 才谈得上取声：GIF 容器没有音轨，起了进程只会得到一份没人用的 wav 还占着磁盘。
        // 设置页选 GIF 时已经把两格置灰，这里兜的是「导入的配置里两格有值而格式是 GIF」那一路
        if (!outputFormatCarriesAudio(m_config.format))
        {
            Core::Logger::info("录屏取声跳过：GIF 容器带不了声音。要留声就把输出格式改成 MP4");
            return {};
        }

        const auto program = locateFfmpeg();
        if (!program)
        {
            return std::unexpected(program.error());
        }

        m_audioWav           = m_frameDirectory / "audio.wav";
        const auto arguments = audioCaptureArguments(m_config.microphoneDevice, m_config.systemSoundDevice, m_audioWav);
        if (!arguments)
        {
            return std::unexpected(arguments.error());
        }

        m_audio = std::make_unique<QProcess>();
        m_audio->setProcessChannelMode(QProcess::MergedChannels);
        m_audio->start(*program, *arguments);
        if (!m_audio->waitForStarted(kAudioStartTimeoutMs))
        {
            m_audio.reset();
            return std::unexpected(Core::Error{
                    Core::ErrorCode::DependencyMissing,
                    GyanisScribe::Core::uiTextUtf8("取声的 ffmpeg 起不来。设备可能正被别的程序占用，关掉占用者再录，或在设置里把取声关掉"),
            });
        }
        m_audioOutcome = AudioCaptureOutcome::Captured;
        Core::Logger::info(std::format("录屏开始取声：麦克风 {}，系统声音 {}", m_config.microphoneDevice.isEmpty() ? "未选" : m_config.microphoneDevice.toStdString(),
                                       m_config.systemSoundDevice.isEmpty() ? "未选" : m_config.systemSoundDevice.toStdString()));
        return {};
    }

    std::filesystem::path RecordingService::finishAudioCapture()
    {
        if (m_audio == nullptr)
        {
            return {};
        }

        // 一个 q 走 stdin 就是 ffmpeg 自己的「收尾退出」：它会正常关掉 wav 的长度头，强杀留下的文件读不出时长
        if (m_audio->state() != QProcess::NotRunning)
        {
            m_audio->write("q\\n");
            m_audio->waitForBytesWritten(1000);
            if (!m_audio->waitForFinished(kAudioStopTimeoutMs))
            {
                m_audio->kill();
                m_audio->waitForFinished(1000);
            }
        }

        std::error_code sizeError;
        const auto      bytes = std::filesystem::file_size(m_audioWav, sizeError);
        const auto      took  = !sizeError && bytes > kWavHeaderBytes;
        if (!took)
        {
            Core::Logger::warning(std::format("取声没留下可读的 wav（退出码 {}）：成品按无声交回", m_audio->exitCode()));
            m_audioOutcome = AudioCaptureOutcome::Failed;
            m_audio.reset();
            return {};
        }
        m_audio.reset();
        return m_audioWav;
    }

    AudioCaptureOutcome RecordingService::audioOutcome() const noexcept
    {
        return m_audioOutcome;
    }

} // namespace GyanisScribe::Record
