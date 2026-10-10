#include "Record/RecordingService.h"

#include "Capture/ScreenUtils.h"
#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Text.h"

#include <QDateTime>

#include <chrono>
#include <format>

namespace
{
    /// 定时器比目标帧率多要一点余量：正好按 1000/帧率 排会让偶发的一次迟到的抓帧直接跳过一整格
    constexpr int kTimerSlackMs = 4;
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
        m_sink           = std::make_unique<PngSequenceSink>(directory);
        // 节拍从这一刻起算：会话拿到的所有时间都是同一个单调时钟的读数
        m_clock.start();
        m_session = std::make_unique<RecordingSession>(config, *m_sink, m_clock.elapsed(), countdownSeconds);

        // 间隔取 1000/帧率 再加几毫秒余量：抓屏本身就有抖动，排太紧只会让帧率抖动被放大成丢帧
        const auto interval = static_cast<int>(1000 / std::max(1, config.frameRate)) + kTimerSlackMs;
        m_timer.start(interval);
        Core::Logger::info(std::format("录屏开始：帧率 {}，上限 {} 秒，临时帧目录 {}", config.frameRate, config.maximumDurationSeconds, directory.string()));
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
            Core::Logger::warning(std::format("录屏编码失败，临时帧保留在 {} 以便重编码", m_frameDirectory.string()));
            emit failed(QString::fromUtf8(encoded.error().message.c_str()));
            return std::unexpected(encoded.error());
        }

        std::error_code cleanupError;
        std::filesystem::remove_all(m_frameDirectory, cleanupError);
        if (cleanupError)
        {
            // 成品已经有了，这里只报一条：清不掉是磁盘占用问题，不是录屏失败
            Core::Logger::warning(std::format("临时帧目录没清干净：{}（{}）", m_frameDirectory.string(), cleanupError.message()));
        }
        emit finished(QString::fromLocal8Bit(encoded->string().c_str()));
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
            return m_source.grabWindow(m_config.windowHandle);
        }

        const auto screen = m_source.grabScreen(m_config.screenName);
        if (!screen)
        {
            return std::unexpected(screen.error());
        }
        if (m_config.mode == RecordMode::FullScreen)
        {
            return screen;
        }
        // 选区模式在整屏帧上裁：抓屏接口只给整屏与窗口两种，逐区抓屏要等平台上的 WGC 接入才有
        return Capture::cropToLogicalRect(*screen, m_config.region);
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
