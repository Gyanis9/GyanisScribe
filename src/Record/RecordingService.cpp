#include "Record/RecordingService.h"

#include "Capture/ScreenUtils.h"
#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Text.h"
#include "Record/CursorHighlight.h"

#include <QCursor>
#include <QDateTime>
#include <QScreen>

#include <chrono>
#include <format>
#include <optional>

namespace
{
    /// 定时器比目标帧率多要一点余量：正好按 1000/帧率 排会让偶发的一次迟到的抓帧直接跳过一整格
    constexpr int kTimerSlackMs = 4;

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
    RecordingService::RecordingService(Capture::IScreenCaptureSource &source, IRecordingEncoder &encoder, QObject *parent) : QObject(parent), m_source(source), m_encoder(encoder)
    {
        m_timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_timer, &QTimer::timeout, this, &RecordingService::onTick);
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

    Core::Expected<std::filesystem::path> RecordingService::stop()
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

        const auto encoded = m_encoder.encode(m_config, m_frameDirectory, frames, {});
        if (!encoded)
        {
            // 保留临时帧：这已经是录完的内容，删掉就等于让用户白录一次
            Core::Logger::warning(std::format("录屏编码失败，临时帧保留在 {} 以便重编码", Core::Paths::toDisplayText(m_frameDirectory).toStdString()));
            emit failed(QString::fromUtf8(encoded.error().message.c_str()));
            return std::unexpected(encoded.error());
        }

        std::error_code cleanupError;
        std::filesystem::remove_all(m_frameDirectory, cleanupError);
        if (cleanupError)
        {
            // 成品已经有了，这里只报一条：清不掉是磁盘占用问题，不是录屏失败
            Core::Logger::warning(std::format("临时帧目录没清干净：{}（{}）", Core::Paths::toDisplayText(m_frameDirectory).toStdString(), cleanupError.message()));
        }
        emit finished(Core::Paths::toDisplayText(*encoded));
        return encoded;
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
            // 窗口帧的左上角要等平台侧的窗口几何接口才能拿到全局读数，这里算不出光标落在第几格：
            // 与其画一个错位的圈冒充「已高亮」，不如第一帧就把这一档在窗口模式下没做说出来
            if (m_config.cursorHighlight && !m_cursorHighlightWarned)
            {
                m_cursorHighlightWarned = true;
                Core::Logger::warning("窗口录屏圈不出光标：还没法把窗口左上角换成全局坐标。请改用整屏或选区录屏，光标高亮只在它们上面生效");
            }
            return m_source.grabWindow(m_config.windowHandle);
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

        const auto painted = paintCursorHighlight(frame.image, QCursor::pos(), frameGlobalOriginLogical, frame.devicePixelRatio);
        if (!painted && !m_cursorHighlightWarned)
        {
            m_cursorHighlightWarned = true;
            Core::Logger::warning(std::format("光标高亮没画出来：{}", painted.error().message));
        }
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

} // namespace GyanisScribe::Record
