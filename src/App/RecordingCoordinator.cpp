#include "App/RecordingCoordinator.h"

#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/Capture/RegionHistory.h"
#include "Core/Record/RecorderConfig.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/NamingRules.h"
#include "Core/Settings/SettingsKeys.h"
#include "Gui/Record/RecordingHud.h"
#include "Gui/Resources/IconProvider.h"
#include "Platform/IPlatformIntegration.h"
#include "Plugins/Recording/FfmpegRecordingEncoder.h"
#include "Plugins/Recording/RecordingService.h"

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

#include <format>

namespace GyanisScribe::App
{
    RecordingCoordinator::RecordingCoordinator(GyanisScribe::Core::IScreenCaptureSource &captureSource, GyanisScribe::Platform::IPlatformIntegration &integration,
                                               GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Gui::IconProvider &icons, QObject *parent) :
        QObject(parent), m_captureSource(captureSource), m_integration(integration), m_settings(settings), m_icons(icons)
    {
        m_encoder = std::make_unique<GyanisScribe::Plugins::FfmpegRecordingEncoder>();
        m_service = std::make_unique<GyanisScribe::Plugins::RecordingService>(m_captureSource, *m_encoder, m_integration);

        QObject::connect(m_service.get(), &GyanisScribe::Plugins::RecordingService::finished, this, &RecordingCoordinator::handleFinished);
        QObject::connect(m_service.get(), &GyanisScribe::Plugins::RecordingService::failed, this, &RecordingCoordinator::handleFailed);
        QObject::connect(m_service.get(), &GyanisScribe::Plugins::RecordingService::aborted, this, &RecordingCoordinator::handleAborted);
        QObject::connect(m_service.get(), &GyanisScribe::Plugins::RecordingService::countdownChanged, this,
                         [this](const int remainingSeconds)
                         {
                             m_countdownRemaining = remainingSeconds;
                             updateHud();
                         });
        QObject::connect(m_service.get(), &GyanisScribe::Plugins::RecordingService::frameStored, this, [this] { updateHud(); });
    }

    GyanisScribe::Base::Expected<GyanisScribe::Core::RecorderConfig> RecordingCoordinator::makeConfig() const
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
            return std::unexpected(GyanisScribe::Base::Error{GyanisScribe::Base::ErrorCode::ScreenUnavailable,
                                                             GyanisScribe::Base::uiTextUtf8("当前没有可用屏幕，录屏没法开始。请接回显示器后重试")});
        }

        GyanisScribe::Core::RecorderConfig config;
        config.screenName      = screen->name();
        const auto configured  = m_settings.stringValue(GyanisScribe::Core::kPathRecording, QString());
        config.outputDirectory = configured.isEmpty() ? GyanisScribe::Base::Paths::defaultRecordingDirectory() : GyanisScribe::Base::Paths::fromDisplayText(configured);

        // 帧率取用户记下的那一档；读不出时记一条并按 RecorderConfig 的初值走，
        // 不拿一个"看起来能用"的值替用户决定（validateRecorderConfig 会在真正开始前再判一次范围）
        if (const auto parsed = GyanisScribe::Core::parseFrameRateSetting(m_settings.intValue(GyanisScribe::Core::kRecordDefaultFrameRate, GyanisScribe::Core::kDefaultFrameRate));
            parsed)
        {
            config.frameRate = *parsed;
        } else
        {
            GyanisScribe::Base::Logger::warning(std::format("录屏默认帧率读不出：{}，本次按 {} 帧开始", parsed.error().message, config.frameRate));
        }
        // 最长时长同一条形状：读不出就记一条并按初值走，validateRecorderConfig 开始前还会再判一次范围
        if (const auto parsed = GyanisScribe::Core::parseMaximumDurationSetting(
                    m_settings.intValue(GyanisScribe::Core::kRecordMaximumDuration, GyanisScribe::Core::kDefaultMaximumDurationSeconds));
            parsed)
        {
            config.maximumDurationSeconds = *parsed;
        } else
        {
            GyanisScribe::Base::Logger::warning(std::format("最长录制时长读不出：{}，本次按 {} 秒开始", parsed.error().message, config.maximumDurationSeconds));
        }

        // 画面码率与帧率同一条形状：读不出就记一条并按初值（按质量优先）走，不替用户挑一档
        if (const auto parsed =
                    GyanisScribe::Core::parseVideoBitrateSetting(m_settings.intValue(GyanisScribe::Core::kRecordVideoBitrate, GyanisScribe::Core::kDefaultVideoBitrateKbps));
            parsed)
        {
            config.videoBitrateKbps = *parsed;
        } else
        {
            GyanisScribe::Base::Logger::warning(std::format("录屏画面码率读不出：{}，本次按质量优先开始", parsed.error().message));
        }
        // 光标高亮是个开关，没有「读不出」这一档：配置里没有就按关；到了画不出位置的那一种模式，RecordingService 会自己报一句
        config.cursorHighlight = m_settings.boolValue(GyanisScribe::Core::kRecordCursorHighlight, false);
        // 编码器钉子：选的是「自动」或没填就是空串（全自动逐探）；钉的是具体名字就原样交给编码器那一层去探
        const auto pinnedEncoder =
                m_settings.stringValue(GyanisScribe::Core::kRecordVideoEncoder, QString::fromUtf8(std::string{GyanisScribe::Core::kRecordVideoEncoderAutoToken}));
        if (pinnedEncoder != QString::fromUtf8(std::string{GyanisScribe::Core::kRecordVideoEncoderAutoToken}) && !pinnedEncoder.isEmpty())
        {
            config.videoEncoder = pinnedEncoder.toStdString();
        }
        // 成品文件名模板：与截图、贴图保存、识别导出同一格，几个写盘口各取一次、各按同一规则回落
        config.nameTemplate = GyanisScribe::Core::nameTemplateOrDefault(m_settings);

        // 取声的两路：留空就是不取那一路。存的是 dshow 的稳定标识，换系统显示语言也不漂
        config.microphoneDevice  = m_settings.stringValue(GyanisScribe::Core::kRecordAudioMicrophone, QString());
        config.systemSoundDevice = m_settings.stringValue(GyanisScribe::Core::kRecordAudioSystemSound, QString());

        // 输出容器按用户选的那一档来：GIF 走两遍调色板，帧率上限比 MP4 低，
        // 那一档的天花板由 validateRecorderConfig 在真正开录之前判
        const auto storedFormat = m_settings.stringValue(GyanisScribe::Core::kRecordFormat, QString::fromUtf8(std::string{GyanisScribe::Core::recordFormatText(config.format)}));
        const auto parsedFormat = GyanisScribe::Core::parseRecordFormat(storedFormat);
        if (!parsedFormat)
        {
            return std::unexpected(parsedFormat.error());
        }
        config.format = *parsedFormat;

        // 录制范围按用户选的那一档来；读不出或做不出来时把原因交回去，不悄悄换成整屏——
        // 那会让用户以为录的是他选的那片，实际录下整个桌面
        const auto storedScope = m_settings.stringValue(GyanisScribe::Core::kRecordScope, QString::fromUtf8(std::string{GyanisScribe::Core::recordScopeText(config.mode)}));
        const auto parsedScope = GyanisScribe::Core::parseRecordScope(storedScope);
        if (!parsedScope)
        {
            return std::unexpected(parsedScope.error());
        }
        config.mode = *parsedScope;

        if (config.mode == GyanisScribe::Core::RecordMode::Region)
        {
            // 「上次选区」取历史首条：与截图那边「重复上次选区」同一条（选区历史见 Core/Capture/RegionHistory.h）
            const auto history = GyanisScribe::Core::loadRegionHistory(m_settings);
            if (!history)
            {
                return std::unexpected(history.error());
            }
            if (history->empty())
            {
                return std::unexpected(GyanisScribe::Base::Error{GyanisScribe::Base::ErrorCode::InvalidArgument,
                                                                 GyanisScribe::Base::uiTextUtf8("还没有可录的选区：先按截图热键框一次选区，再开始录屏；也可以把录制范围改回整屏")});
            }
            config.region = history->front().region;
        } else if (config.mode == GyanisScribe::Core::RecordMode::Window)
        {
            const auto window = m_integration.foregroundWindow();
            if (!window)
            {
                return std::unexpected(window.error());
            }
            config.windowHandle = window->handle;
        }
        return config;
    }

    void RecordingCoordinator::toggle()
    {
        const auto state = m_service->state();
        if (state == GyanisScribe::Core::RecordingState::Idle || state == GyanisScribe::Core::RecordingState::Done || state == GyanisScribe::Core::RecordingState::Failed)
        {
            const auto config = makeConfig();
            if (!config)
            {
                GyanisScribe::Base::Logger::error(std::format("录屏没能开始：{}", config.error().message));
                return;
            }
            showHud();
            // 倒计时取用户记下的那一档；读不出时记一条并回到默认 3 秒（validate 那道不在这里，值本身是 0..10 的整数）
            auto countdown = GyanisScribe::Core::kDefaultCountdownSeconds;
            if (const auto parsed =
                        GyanisScribe::Core::parseCountdownSetting(m_settings.intValue(GyanisScribe::Core::kRecordCountdownSeconds, GyanisScribe::Core::kDefaultCountdownSeconds));
                parsed)
            {
                countdown = *parsed;
            } else
            {
                GyanisScribe::Base::Logger::warning(std::format("录屏倒计时读不出：{}，本次按 {} 秒开始", parsed.error().message, countdown));
            }
            if (auto started = m_service->start(*config, countdown); !started)
            {
                hideHud();
                GyanisScribe::Base::Logger::error(std::format("录屏没能开始：{}", started.error().message));
                emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("录屏没能开始：{}"), started.error().message).c_str()),
                                    GyanisScribe::Gui::ToastKind::Danger);
                return;
            }
            updateHud();
            return;
        }

        // 还在录或还在倒计时/暂停：这一按是「停止并保存」。编码已经搬到工作线程，界面不再停在控制条上——
        // 录像是真的停了，控制条与托盘那行读数再留着就是句假话，所以先收掉，再告诉用户成品还在编
        GyanisScribe::Base::Logger::info("录屏停止中，正在编码成品");
        if (auto stopped = m_service->stop(); !stopped)
        {
            hideHud();
            GyanisScribe::Base::Logger::error(std::format("录屏收尾失败：{}", stopped.error().message));
            emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("录屏收尾失败：{}"), stopped.error().message).c_str()),
                                GyanisScribe::Gui::ToastKind::Danger);
            return;
        }
        hideHud();
        emit messageForUser(GyanisScribe::Base::uiText("正在编成品，好了会说一声"), GyanisScribe::Gui::ToastKind::Information);
    }

    void RecordingCoordinator::cancel()
    {
        const auto cancelled = m_service->cancel();
        // 成功那一路由服务自己的 aborted 信号回到 handleAborted：收尾只有一处，两个出口不各写一份
        if (!cancelled)
        {
            GyanisScribe::Base::Logger::warning(std::format("录屏取消没成：{}", cancelled.error().message));
            emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("取消录屏没成：{}"), cancelled.error().message).c_str()),
                                GyanisScribe::Gui::ToastKind::Danger);
        }
    }

    void RecordingCoordinator::showHud()
    {
        auto *screen = QGuiApplication::primaryScreen();
        if (screen == nullptr)
        {
            return;
        }

        m_hud = new GyanisScribe::Gui::RecordingHud(m_icons, screen->availableGeometry());
        QObject::connect(m_hud, &GyanisScribe::Gui::RecordingHud::pauseRequested, this,
                         [this]
                         {
                             m_service->pause();
                             updateHud();
                         });
        QObject::connect(m_hud, &GyanisScribe::Gui::RecordingHud::resumeRequested, this,
                         [this]
                         {
                             m_service->resume();
                             updateHud();
                         });
        QObject::connect(m_hud, &GyanisScribe::Gui::RecordingHud::stopRequested, this, &RecordingCoordinator::toggle);
        QObject::connect(m_hud, &GyanisScribe::Gui::RecordingHud::cancelRequested, this, &RecordingCoordinator::cancel);
        QObject::connect(m_hud, &GyanisScribe::Gui::RecordingHud::minimized, this,
                         [this]
                         {
                             // 最小化只是收起这一层：托盘菜单的录屏项还在，用户照样能停
                             emit messageForUser(GyanisScribe::Base::uiText("控制条已收起；按 Ctrl+Alt+R 或托盘里的录屏项可停止并保存"), GyanisScribe::Gui::ToastKind::Information);
                         });

        // 时长与状态每秒推一次：服务只在帧落地时发信号，而控制条要的是「现在过了多久」
        m_ticker = new QTimer(this);
        m_ticker->setInterval(1000);
        QObject::connect(m_ticker, &QTimer::timeout, this, &RecordingCoordinator::updateHud);
        m_ticker->start();
        m_hud->show();
        // 一开始就把托盘那一行接管过来：第一秒的 ticker 还没跑，别让用户看到「贴图 N 张」以为没在录
        emit recordingStatusChanged(0, false);
    }

    void RecordingCoordinator::updateHud()
    {
        if (m_hud == nullptr)
        {
            return;
        }
        m_hud->applyState(m_service->state(), m_countdownRemaining, m_service->recordedSeconds());
        // 控制条收进托盘后，这一行读数就是用户唯一的出口：每秒推一次，暂停与在录分开显示
        emit recordingStatusChanged(m_service->recordedSeconds(), m_service->state() == GyanisScribe::Core::RecordingState::Paused);
    }

    void RecordingCoordinator::hideHud()
    {
        if (m_ticker != nullptr)
        {
            m_ticker->stop();
            m_ticker->deleteLater();
            m_ticker = nullptr;
        }
        if (m_hud != nullptr)
        {
            m_hud->close();
        }
        m_countdownRemaining = 0;
        // 交还给贴图张数那一档：录制结束后托盘还写着「正在录制」就是句假话
        emit recordingStatusChanged(-1, false);
    }

    void RecordingCoordinator::handleFinished(const QString &outputPath, const QSize &frameSize)
    {
        hideHud();
        GyanisScribe::Base::Logger::info(std::format("录屏成品：{}", outputPath.toStdString()));
        // 中途丢了声音要当面说一句：只写日志等于让用户拿到一个安静的成品，再自己发现没录上
        if (m_service->audioOutcome() == GyanisScribe::Plugins::AudioCaptureOutcome::Failed)
        {
            emit messageForUser(GyanisScribe::Base::uiText("成品已出，但没带上声音：取声那一步中途失败了，原因写在日志里"), GyanisScribe::Gui::ToastKind::Information);
        }
        // 尺寸取信号里这一轮自己那份：编码在后台跑的时候用户可以再开一轮，回读成员拿到的会是后一轮的
        emit finished(outputPath, frameSize);
    }

    void RecordingCoordinator::handleFailed(const QString &reason)
    {
        hideHud();
        GyanisScribe::Base::Logger::error(std::format("录屏中止：{}", reason.toStdString()));
        emit messageForUser(QString::fromUtf8(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("录屏中止：{}。请重试一次"), reason.toStdString()).c_str()),
                            GyanisScribe::Gui::ToastKind::Danger);
    }

    void RecordingCoordinator::handleAborted()
    {
        hideHud();
        GyanisScribe::Base::Logger::info("录屏已取消：这一段没有成品");
        emit messageForUser(GyanisScribe::Base::uiText("已取消录屏：这一段没有保存，临时帧也删了"), GyanisScribe::Gui::ToastKind::Information);
    }

} // namespace GyanisScribe::App
