// RecordingService 的窄直测：只问「这一份配置能不能开始录」，不验帧序列与成品（那两条要靠真桌面会话，
// 类别的文档里写明容器只能验到编译）。这一条盯的是取声这一环会不会替一种带不了声音的容器白起进程。

#include "Capture/IScreenCaptureSource.h"
#include "Core/Paths.h"
#include "Core/Types.h"
#include "Record/FfmpegRecordingEncoder.h"
#include "Record/RecorderConfig.h"
#include "Record/RecordingService.h"

#include <QImage>
#include <QSize>
#include <QString>
#include <QStandardPaths>

#include <gtest/gtest.h>

#include <filesystem>

using GyanisScribe::Capture::IScreenCaptureSource;
using GyanisScribe::Core::CapturedImage;
using GyanisScribe::Core::Expected;
using GyanisScribe::Record::AudioCaptureOutcome;
using GyanisScribe::Record::IRecordingEncoder;
using GyanisScribe::Record::OutputFormat;
using GyanisScribe::Record::RecorderConfig;
using GyanisScribe::Record::RecordingService;

namespace
{
    /// 不真抓屏的来源：这一条用例里一次都不会被叫到（没有事件循环，节拍定时器不会跳）
    class StubCaptureSource final : public IScreenCaptureSource
    {
    public:
        [[nodiscard]] Expected<CapturedImage> grabScreen(const QString &screenName) const override
        {
            return CapturedImage{QImage{QSize{8, 8}, QImage::Format_ARGB32_Premultiplied}, QSize{8, 8}, 1.0, screenName};
        }

        [[nodiscard]] Expected<CapturedImage> grabWindow(const quintptr) const override
        {
            return CapturedImage{QImage{QSize{8, 8}, QImage::Format_ARGB32_Premultiplied}, QSize{8, 8}, 1.0, QStringLiteral("screen0")};
        }
    };

    /// 不真编码的编码器：这一条用例只走到「能不能开始」为止
    class StubRecordingEncoder final : public IRecordingEncoder
    {
    public:
        [[nodiscard]] Expected<std::filesystem::path> encode(const RecorderConfig &, const std::filesystem::path &frameDirectory, int, const std::filesystem::path &) override
        {
            return frameDirectory;
        }
    };

    /**
     * @brief 一份能通过校验的配置，取声那一格给上值
     * @param format 输出格式
     * @return 交给 start() 的参数
     */
    RecorderConfig audioRequestedConfig(const OutputFormat format)
    {
        auto config                   = RecorderConfig{};
        config.format                 = format;
        config.screenName             = QStringLiteral("screen0");
        config.frameRate              = 15;
        config.maximumDurationSeconds = 60;
        config.outputDirectory        = std::filesystem::temp_directory_path();
        config.microphoneDevice       = QStringLiteral("@device_pnp_\\\\?\\mic#1");
        return config;
    }

    /**
     * @brief 钉住 GIF 那一档不为一声音起进程
     * @details 去掉服务里的格式判断，这条在两种现场都会红：这台机器没有 ffmpeg 时，取声那一步会把整轮录屏拦住；
     *          装了 ffmpeg 的机器上，它会真起一个进程去取一份没人读的声音。
     */
    TEST(RecordingServiceStart, GifWithAudioRequestedStartsWithoutTakingSound)
    {
        StubCaptureSource    source;
        StubRecordingEncoder encoder;
        RecordingService     service{source, encoder};

        const auto started = service.start(audioRequestedConfig(OutputFormat::Gif), 0);
        ASSERT_TRUE(static_cast<bool>(started)) << started.error().message;
        EXPECT_EQ(service.audioOutcome(), AudioCaptureOutcome::NotRequested) << "GIF 带不了声音，这一轮不该把取声算成在跑";

        // 开始一轮就在缓存里建了一个带时间戳的帧目录，用例不给它收尾就会一层层攒下来
        std::error_code ignored;
        std::filesystem::remove_all(GyanisScribe::Core::Paths::cacheDirectory() / "Recording", ignored);
    }

    /**
     * @brief 配对的那一条：MP4 选了取声仍然真的要去起 ffmpeg
     * @details 上一那条容易被写成「什么格式都跳过取声」——那种做法会把 MP4 的音轨一起关掉。
     *          这里在没有 ffmpeg 的现场断句「起不动就拒绝开始」，跳过用在了对的档位上才算得住。
     */
    TEST(RecordingServiceStart, Mp4WithAudioRequestedStillWantsFfmpeg)
    {
        if (!QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
        {
            GTEST_SKIP() << "这台机器 PATH 上有 ffmpeg，这一条要的是「没有它」的那一种现场";
        }

        StubCaptureSource    source;
        StubRecordingEncoder encoder;
        RecordingService     service{source, encoder};

        const auto started = service.start(audioRequestedConfig(OutputFormat::Mp4), 0);
        ASSERT_FALSE(static_cast<bool>(started)) << "MP4 也要跳过取声了：音轨这一档整个没了";
        EXPECT_NE(started.error().message.find("ffmpeg"), std::string::npos) << started.error().message;

        std::error_code ignored;
        std::filesystem::remove_all(GyanisScribe::Core::Paths::cacheDirectory() / "Recording", ignored);
    }
} // namespace
