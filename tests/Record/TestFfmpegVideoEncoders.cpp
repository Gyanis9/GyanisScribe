// 编码器候选表与选型的用例。判据来自本机实测：那份 ffmpeg 的 -encoders 名单里 h264_qsv、h264_amf 都在，
// 真跑却分别报「MFX 不支持」和「amfrt64.dll 打不开」，而 libx264 干脆没有——所以「在名单里」不能当选型依据，
// 只有真跑三帧到 null 输出的成功才算。

#include "Core/Paths.h"
#include "Core/Types.h"
#include "Record/FfmpegCommand.h"
#include "Record/FfmpegRecordingEncoder.h"
#include "Record/FfmpegVideoEncoders.h"
#include "Record/RecorderConfig.h"
#include "Record/RecordingSinks.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using GyanisScribe::Core::Error;
using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Record::mp4Arguments;
using GyanisScribe::Record::selectVideoEncoder;
using GyanisScribe::Record::videoEncoderProbeArguments;
using GyanisScribe::Record::videoEncoderQualityArguments;

namespace
{
    /// 记下一条条探过的命令，并按「哪个编码器该失败」的表回答成败——选型的所有形状都在这里可判
    class ProbeRecorder
    {
    public:
        explicit ProbeRecorder(std::vector<std::string> failing) : m_failing{std::move(failing)}
        {
        }

        bool operator()(const QStringList &arguments)
        {
            const auto index = arguments.indexOf(QStringLiteral("-c:v"));
            m_probed.push_back(index >= 0 ? arguments.value(index + 1).toStdString() : std::string{});
            return std::find(m_failing.begin(), m_failing.end(), m_probed.back()) == m_failing.end();
        }

        [[nodiscard]] const std::vector<std::string> &probed() const
        {
            return m_probed;
        }

    private:
        std::vector<std::string> m_failing; ///< 这些编码器在这条用例里假装开不起来
        std::vector<std::string> m_probed;  ///< 实际探过的顺序
    };

    /**
     * @brief 把 recorder 按引用交给选型
     * @details 选型收的是 std::function，直接把 recorder 交进去会连同计数一起拷走，
     *          用例读的那份永远是空的。
     */
    auto makeProbe(ProbeRecorder &recorder)
    {
        return [&recorder](const QStringList &arguments) { return recorder(arguments); };
    }

    TEST(VideoEncoderShapes, EachFamilyGetsItsOwnQualityFlags)
    {
        // QStringList 的花括号写法在 EXPECT_EQ 宏里会被逗号拆成三个实参，期望值先写成局部量
        const QStringList x264Quality{QStringLiteral("-preset"), QStringLiteral("veryfast"), QStringLiteral("-crf"), QStringLiteral("23")};
        const QStringList x264Bitrate{QStringLiteral("-preset"), QStringLiteral("veryfast"), QStringLiteral("-b:v"), QStringLiteral("4000k")};
        const QStringList mpeg4Quality{QStringLiteral("-q:v"), QStringLiteral("5")};
        const QStringList bitrateOnly{QStringLiteral("-b:v"), QStringLiteral("2000k")};

        // x264 的说法：默认质量档走 CRF，给了码率才换成码率控
        EXPECT_EQ(videoEncoderQualityArguments("libx264", 0), x264Quality);
        EXPECT_EQ(videoEncoderQualityArguments("libx264", 4000), x264Bitrate);

        // NVENC 不认 -crf，它那一档叫 -cq；档位名也是 p0-p7 而不是 veryfast
        const auto nvenc = videoEncoderQualityArguments("h264_nvenc", 0);
        EXPECT_TRUE(nvenc.contains(QStringLiteral("-cq"))) << nvenc.join(QLatin1Char(' ')).toStdString();
        EXPECT_FALSE(nvenc.contains(QStringLiteral("-crf"))) << "把 x264 的参数抄给 NVENC，整条编码开不起来";

        // mpeg4 既没有 CRF 也没有 -cq，只有 -q:v
        EXPECT_EQ(videoEncoderQualityArguments("mpeg4", 0), mpeg4Quality);

        // 硬件那一档里没把握的参数一律不发明：没给码率时只留公共参数
        EXPECT_TRUE(videoEncoderQualityArguments("h264_qsv", 0).isEmpty());
        EXPECT_EQ(videoEncoderQualityArguments("h264_qsv", 2000), bitrateOnly);
    }

    TEST(VideoEncoderProbe, ProbeCarriesTheRealFrameSizeAndTheNullSink)
    {
        const auto arguments = videoEncoderProbeArguments("h264_nvenc", 0, 1920, 1080, 15);

        EXPECT_TRUE(arguments.contains(QStringLiteral("color=c=black:s=1920x1080:r=15"))) << arguments.join(QLatin1Char(' ')).toStdString();
        EXPECT_EQ(arguments.value(arguments.indexOf(QStringLiteral("-frames:v")) + 1), QStringLiteral("3"));
        EXPECT_TRUE(arguments.contains(QStringLiteral("-pix_fmt"))) << "探的像素格式要与真编码同档，否则探出来的成功不作数";
        EXPECT_NE(arguments.indexOf(QStringLiteral("null")), -1) << "探测不该落盘";
        EXPECT_EQ(arguments.last(), QStringLiteral("-"));
    }

    TEST(VideoEncoderSelection, FirstUsableCandidateWinsInPreferenceOrder)
    {
        ProbeRecorder recorder{std::vector<std::string>{"libx264"}};
        const auto    selected = selectVideoEncoder(makeProbe(recorder), 1920, 1080, 15, 0);

        ASSERT_TRUE(static_cast<bool>(selected)) << selected.error().message;
        EXPECT_EQ(*selected, "h264_nvenc");
        ASSERT_EQ(recorder.probed().size(), 2u) << "第一个开不起来之后该接着问下一档，而不是回头重试";
        EXPECT_EQ(recorder.probed()[0], "libx264") << "软件编码器排最前：没有显卡的机器也要能得到 H.264";
        EXPECT_EQ(recorder.probed()[1], "h264_nvenc");
    }

    TEST(VideoEncoderSelection, StopsProbingOnceOneOpens)
    {
        ProbeRecorder recorder{std::vector<std::string>{"libx264", "h264_nvenc", "h264_qsv"}};
        const auto    selected = selectVideoEncoder(makeProbe(recorder), 1920, 1080, 15, 0);

        ASSERT_TRUE(static_cast<bool>(selected)) << selected.error().message;
        EXPECT_EQ(*selected, "h264_amf");
        EXPECT_EQ(recorder.probed().size(), 4u) << "探到能用的那一档就该停，后面每档都是白等";
    }

    TEST(VideoEncoderSelection, RejectionNamesEveryCandidateItTried)
    {
        // 全部候选都开不起来：报错要点名试过哪几个，否则用户无法判断是 ffmpeg 缺件还是参数写歪
        ProbeRecorder recorder{std::vector<std::string>{"libx264", "h264_nvenc", "h264_qsv", "h264_amf", "mpeg4"}};
        const auto    selected = selectVideoEncoder(makeProbe(recorder), 1920, 1080, 15, 0);

        ASSERT_FALSE(static_cast<bool>(selected));
        EXPECT_EQ(selected.error().code, ErrorCode::DependencyMissing);
        EXPECT_EQ(recorder.probed().size(), GyanisScribe::Record::kPreferredVideoEncoders.size());
        for (const auto candidate: GyanisScribe::Record::kPreferredVideoEncoders)
        {
            EXPECT_NE(selected.error().message.find(std::string{candidate}), std::string::npos) << candidate;
        }
    }

    TEST(VideoEncoderSelection, Mp4ArgumentsUseTheSelectedEncoder)
    {
        GyanisScribe::Record::RecorderConfig config;
        config.frameRate        = 15;
        config.videoBitrateKbps = 0;

        const auto withNvenc = mp4Arguments(config, 0, 0, "C:/tmp/rec/frames", "C:/tmp/rec/out.mp4", {}, "h264_nvenc");
        EXPECT_EQ(withNvenc.value(withNvenc.indexOf(QStringLiteral("-c:v")) + 1), QStringLiteral("h264_nvenc"));
        EXPECT_FALSE(withNvenc.contains(QStringLiteral("-crf"))) << "选型换了编码器还留着 x264 的参数，整条命令开不起来";
        EXPECT_TRUE(withNvenc.contains(QStringLiteral("+faststart")));
    }

    // 端到端那一条要起进程：Linux 侧 QProcess 靠应用对象的 socket 通知器收 ffmpeg 的输出，
    // 而这一档不建任何窗口，QCoreApplication 就够
    char  applicationArgument[]  = "TestFfmpegVideoEncoders";
    char *applicationArguments[] = {applicationArgument, nullptr};

    std::unique_ptr<QCoreApplication> application;

    class VideoEncoderEndToEndFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            int argumentCount = 1;
            application       = std::make_unique<QCoreApplication>(argumentCount, applicationArguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }
    };

    /**
     * @brief 用真 ffmpeg 走一遍产品的编码那一步（环境变量门控）
     * @details 这一条是这次改动的原因本身：本机那份 ffmpeg 没有 libx264，而编码命令行写死了它，
     *          只按参数表断言看不见那种失败。这里让 encode() 自己去探选型、自己拼命令、自己起进程，
     *          所以选型、参数形状、成品检查三件事在同一轮里一起验。缺 GYANISSCRIBE_FFMPEG 就 SKIP，仓库里不留路径。
     */
    TEST_F(VideoEncoderEndToEndFixture, SelectedEncoderReallyWritesAnMp4)
    {
        const auto program = qEnvironmentVariable("GYANISSCRIBE_FFMPEG");
        if (program.isEmpty())
        {
            GTEST_SKIP() << "没有 GYANISSCRIBE_FFMPEG，这一条要真 ffmpeg";
        }

        // 产品那一侧只按 PATH 找 ffmpeg，所以把这一份的目录顶到 PATH 前面
        const auto path = QStringLiteral("%1%2%3").arg(QFileInfo(program).absolutePath(), QDir::listSeparator(), qEnvironmentVariable("PATH"));
        qputenv("PATH", path.toLocal8Bit());

        std::error_code ignored;
        const auto      scratchDirectory = std::filesystem::temp_directory_path() / "GyanisScribe-encoder-probe";
        std::filesystem::remove_all(scratchDirectory, ignored);
        const auto frameDirectory = scratchDirectory / "frames";
        std::filesystem::create_directories(frameDirectory, ignored);
        ASSERT_FALSE(ignored) << "工作目录建不出来：" << scratchDirectory.string();

        GyanisScribe::Record::PngSequenceSink sink{frameDirectory};
        for (int index = 0; index < 3; ++index)
        {
            QImage frame{QSize{320, 240}, QImage::Format_ARGB32};
            frame.fill(QColor{40, 90, 140});
            ASSERT_TRUE(static_cast<bool>(sink.write(frame, index))) << "第 " << index << " 帧没写成";
        }
        ASSERT_TRUE(static_cast<bool>(sink.seal()));

        GyanisScribe::Record::RecorderConfig config;
        config.frameRate       = 15;
        config.outputDirectory = scratchDirectory;

        // 三帧摊在两秒上：交出去的是「这一轮的帧数与时长」，编码就按这个达成率铺时间轴
        const auto encoded = GyanisScribe::Record::FfmpegRecordingEncoder{}.encode(config, frameDirectory, sink.writtenFrames(), 2000, {});
        ASSERT_TRUE(static_cast<bool>(encoded)) << encoded.error().message;

        const auto bytes = std::filesystem::file_size(*encoded, ignored);
        ASSERT_FALSE(ignored) << "encode() 报成功却没留下成品：" << encoded->string();
        EXPECT_GT(bytes, 1024u) << "一千字节以内的 mp4 只剩容器头，画面没进去";

        QFile container{GyanisScribe::Core::Paths::toDisplayText(*encoded)};
        ASSERT_TRUE(container.open(QIODevice::ReadOnly));
        const auto header = container.read(16);
        EXPECT_TRUE(header.contains("ftyp")) << "成品不是 MP4 容器：" << header.toStdString();
        container.close();

        // 让 ffmpeg 自己读容器的时长：三帧按 3000/2000 的达成率铺出来就是 2.00 秒。
        // 这一条钉的是用户实际看到的东西——按 config 里那 15 帧编，成品会变成 0.2 秒的快放
        QProcess report;
        report.setProgram(program);
        report.setArguments({QStringLiteral("-hide_banner"), QStringLiteral("-i"), GyanisScribe::Core::Paths::toDisplayText(*encoded)});
        report.start();
        ASSERT_TRUE(report.waitForFinished(5000)) << "ffmpeg 读自己的成品都没读完";
        const auto reading = QString::fromUtf8(report.readAllStandardError());
        EXPECT_TRUE(reading.contains(QStringLiteral("Duration: 00:00:02.0"))) << reading.toStdString();

        std::filesystem::remove_all(scratchDirectory, ignored);
    }
} // namespace
