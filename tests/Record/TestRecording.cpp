#include "Record/FfmpegCommand.h"
#include "Record/RecorderConfig.h"
#include "Record/RecordingSession.h"
#include "Record/RecordingSinks.h"
#include "Record/RecordingTiming.h"
#include "Record/FfmpegRecordingEncoder.h"


#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QImage>
#include <QString>
#include <QtGlobal>

#include <filesystem>
#include <fstream>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

using GyanisScribe::Base::CapturedImage;
using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Base::Expected;
using GyanisScribe::Record::achievedFrameRateArgument;
using GyanisScribe::Record::FrameClock;
using GyanisScribe::Record::frameCountFor;
using GyanisScribe::Record::frameFileName;
using GyanisScribe::Record::gifEncodeArguments;
using GyanisScribe::Record::gifPaletteArguments;
using GyanisScribe::Record::IRecordingSink;
using GyanisScribe::Record::mp4Arguments;
using GyanisScribe::Record::OutputFormat;
using GyanisScribe::Record::PngSequenceSink;
using GyanisScribe::Record::RecorderConfig;
using GyanisScribe::Record::RecordingSession;
using GyanisScribe::Record::RecordingState;
using GyanisScribe::Record::RecordMode;
using GyanisScribe::Record::validateRecorderConfig;

namespace
{
    std::unique_ptr<QCoreApplication> application;

    /// 一份能过校验的基准参数，用例只改自己要判的那一格
    RecorderConfig baselineConfig()
    {
        RecorderConfig config;
        config.screenName             = QStringLiteral("HDMI1");
        config.outputDirectory        = std::filesystem::path{"C:/tmp/rec"};
        config.frameRate              = 10;
        config.maximumDurationSeconds = 60;
        return config;
    }

    QImage frameOf(const int width = 8, const int height = 4)
    {
        QImage image{width, height, QImage::Format_ARGB32};
        image.fill(QColor{10, 20, 30});
        return image;
    }

    /// 只数帧不写盘的去处，会话的状态推进全靠它
    class RecordingSink : public IRecordingSink
    {
    public:
        [[nodiscard]] Expected<void> write(const QImage &frame, const int index) override
        {
            if (index == failAt)
            {
                // 失败的那一帧不记账：否则 writtenFrames 会把没落盘的一帧也算进去
                return std::unexpected(GyanisScribe::Base::Error{ErrorCode::FileWriteFailed, "第 2 帧写坏了"});
            }
            indices.push_back(index);
            sizes.push_back({frame.width(), frame.height()});
            return {};
        }

        [[nodiscard]] Expected<void> seal() override
        {
            ++sealCalls;
            if (indices.empty())
            {
                return std::unexpected(GyanisScribe::Base::Error{ErrorCode::InternalError, "一帧都没有"});
            }
            return {};
        }

        [[nodiscard]] int writtenFrames() const override
        {
            return static_cast<int>(indices.size());
        }

        std::vector<int>             indices;        ///< 收到的帧序号
        std::vector<QPair<int, int>> sizes;          ///< 收到的帧尺寸
        int                          sealCalls = 0;  ///< seal 被调了几次
        int                          failAt    = -1; ///< 让哪一帧写失败，-1 表示全都能写
    };

    /// QStringList::indexOf 只按整项匹配，而滤镜参数长在 "fps=10,palettegen..." 这一整项里，得判子串
    bool hasItemContaining(const QStringList &items, const QString &needle)
    {
        return std::ranges::any_of(items, [&needle](const QString &item) { return item.contains(needle); });
    }

    std::filesystem::path probeDirectory(const std::string &name)
    {
        return std::filesystem::temp_directory_path() / (std::string{"GyanisScribe-Record-"} + name);
    }

} // namespace

/**
 * @brief 钉住录屏参数的校验表
 * @details 逐格改一个字段各判一次：这些值全部会在开始之后才暴露问题，
 *          而「录了十秒才发现帧率不合法」是把用户的时间换成一条报错。
 */
TEST(RecordingConfig, AcceptsBaselineAndRejectsEachBadField)
{
    EXPECT_TRUE(validateRecorderConfig(baselineConfig()).has_value());

    {
        auto bad = baselineConfig();
        bad.screenName.clear();
        const auto rejected = validateRecorderConfig(bad);
        ASSERT_FALSE(rejected.has_value());
        EXPECT_EQ(rejected.error().code, ErrorCode::InvalidArgument);
        EXPECT_NE(rejected.error().message.find("screenName"), std::string::npos);
    }
    {
        auto bad      = baselineConfig();
        bad.frameRate = 0;
        EXPECT_FALSE(validateRecorderConfig(bad).has_value());
    }
    {
        // MP4 允许到 60，GIF 只到 24：两档的上限不同，判据要各自成立
        auto withinMp4      = baselineConfig();
        withinMp4.frameRate = 60;
        EXPECT_TRUE(validateRecorderConfig(withinMp4).has_value());

        auto beyondGif      = baselineConfig();
        beyondGif.format    = OutputFormat::Gif;
        beyondGif.frameRate = 25;
        const auto rejected = validateRecorderConfig(beyondGif);
        ASSERT_FALSE(rejected.has_value());
        EXPECT_NE(rejected.error().message.find("GIF"), std::string::npos) << rejected.error().message;
    }
    {
        auto bad                   = baselineConfig();
        bad.maximumDurationSeconds = 0;
        EXPECT_FALSE(validateRecorderConfig(bad).has_value());
    }
    {
        // 0 是「按质量优先」这一档，必须放过去；越界的那一类要在开录之前点名，
        // 交给 ffmpeg 只会换回一句不带字段名的「无效参数」
        auto qualityFirst             = baselineConfig();
        qualityFirst.videoBitrateKbps = 0;
        EXPECT_TRUE(validateRecorderConfig(qualityFirst).has_value()) << "0 这一档被拒了，界面就没法表达按质量优先";

        auto bad             = baselineConfig();
        bad.videoBitrateKbps = -1;
        const auto rejected  = validateRecorderConfig(bad);
        ASSERT_FALSE(rejected.has_value());
        EXPECT_NE(rejected.error().message.find("videoBitrateKbps"), std::string::npos) << rejected.error().message;
    }
    {
        auto bad = baselineConfig();
        bad.outputDirectory.clear();
        EXPECT_FALSE(validateRecorderConfig(bad).has_value());
    }
    {
        auto bad = baselineConfig();
        bad.mode = RecordMode::Region;
        EXPECT_FALSE(validateRecorderConfig(bad).has_value()) << "选区模式没给矩形也放过去了";
    }
    {
        auto bad = baselineConfig();
        bad.mode = RecordMode::Window;
        EXPECT_FALSE(validateRecorderConfig(bad).has_value()) << "窗口模式句柄为 0 也放过去了";
    }
}

/**
 * @brief 钉住节拍：一格只取一帧，落后不补发
 * @details 补发会把一次卡顿变成几十帧重复画面；这里手算 10 帧率的到期点逐个喂时间。
 */
TEST(RecordingTiming, ClockTakesOneFramePerSlotAndNeverBursts)
{
    FrameClock clock{10, 0};
    EXPECT_EQ(clock.nextDueMs(), 100) << "第 0 帧算已取，下一格应在 100ms";
    EXPECT_FALSE(clock.shouldTake(99));
    EXPECT_TRUE(clock.shouldTake(100));
    EXPECT_EQ(clock.takenFrames(), 2);
    EXPECT_EQ(clock.nextDueMs(), 200);

    // 一次跳到 1000ms：只取一帧，并把节拍对齐到那一格，中间那八格丢掉
    EXPECT_TRUE(clock.shouldTake(1000));
    EXPECT_EQ(clock.takenFrames(), 11) << "第 10 格被取走，中间丢掉的格子也算已消耗";
    EXPECT_FALSE(clock.shouldTake(1000));

    // 帧率非法时按 1 兜住，而不是除零或永远不到点
    FrameClock zeroRate{0, 0};
    EXPECT_EQ(zeroRate.frameRate(), 1);
    EXPECT_TRUE(zeroRate.shouldTake(1000));
}

/**
 * @brief 钉住「时长换帧数」向下取整
 * @details 进度条与编码都用它，向上取整会多算出一帧不存在的画面。
 */
TEST(RecordingTiming, FrameCountFloorsPartialSlots)
{
    EXPECT_EQ(frameCountFor(1500, 10), 15);
    EXPECT_EQ(frameCountFor(999, 10), 9);
    EXPECT_EQ(frameCountFor(1000, 0), 0);
}

/**
 * @brief 钉住帧文件名定宽补零
 * @details 不补零时第 10 帧会排到第 2 帧前面，成品的顺序就乱了。
 */
TEST(RecordingTiming, FrameFileNamesAreZeroPadded)
{
    EXPECT_EQ(frameFileName(0).string(), "000000.png");
    EXPECT_EQ(frameFileName(42).string(), "000042.png");
}

/**
 * @brief 钉住 ffmpeg 的实参表
 * @details 逐条列出期望参数：少一个 -an、或者把偶数尺寸那条缩放滤镜弄丢，
 *          都会让 x264 直接退出，而报错在用户那边只是一句「编码失败」。
 */
TEST(RecordingFfmpeg, ArgumentTablesMatchTheDocumentedCommands)
{
    const auto config = baselineConfig();

    const QStringList silent = mp4Arguments(config, 0, 0, "C:/tmp/rec/frames", "C:/tmp/rec/out.mp4", {}, "libx264");
    // 输入那一格按平台各自的分隔符拼，所以比「前四个参数 + 帧序列模式串」而不是整条字符串：
    // 写死反斜杠只能在 Windows 上过，容器里分隔符是 / 会红一条与实现无关的用例
    EXPECT_EQ(silent.first(4).join(QLatin1Char('|')), QStringLiteral("-y|-framerate|10|-i")) << silent.join(QLatin1Char(' ')).toStdString();
    EXPECT_TRUE(silent.value(4).startsWith(QStringLiteral("C:/tmp/rec/frames"))) << silent.value(4).toStdString();
    EXPECT_TRUE(silent.value(4).endsWith(QStringLiteral("%06d.png"))) << "帧序列模式串丢了，ffmpeg 会把目录当成一个文件读";
    EXPECT_EQ(silent.value(5), QStringLiteral("-vf"));
    EXPECT_NE(silent.indexOf(QStringLiteral("-c:v")), -1);
    EXPECT_EQ(silent.value(silent.indexOf(QStringLiteral("-c:v")) + 1), QStringLiteral("libx264"));
    EXPECT_NE(silent.indexOf(QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2")), -1) << "奇数尺寸的截图会被 x264 拒掉";
    EXPECT_NE(silent.indexOf(QStringLiteral("-an")), -1) << "没声音时要显式 -an";
    EXPECT_NE(silent.indexOf(QStringLiteral("+faststart")), -1);
    EXPECT_TRUE(silent.last().endsWith(QStringLiteral("out.mp4"))) << silent.last().toStdString();

    const auto withAudio = mp4Arguments(config, 0, 0, "C:/tmp/rec/frames", "C:/tmp/rec/out.mp4", "C:/tmp/rec/audio.wav", "libx264");
    EXPECT_EQ(withAudio.indexOf(QStringLiteral("-an")), -1) << "有配音时不该再关声轨";
    EXPECT_NE(withAudio.indexOf(QStringLiteral("-shortest")), -1);
    EXPECT_NE(withAudio.indexOf(QStringLiteral("aac")), -1);

    // 没给码率时按质量优先，走 CRF
    EXPECT_NE(silent.indexOf(QStringLiteral("-crf")), -1) << "缺省档要留下 CRF，否则成品质量没人负责";
    EXPECT_EQ(silent.indexOf(QStringLiteral("-b:v")), -1) << "没给码率就不该出现码率控";

    // 给了码率就换成码率控：两条同时给 ffmpeg 听码率而把 crf 当摆设，留着那条会让人以为改的是质量
    auto byBitrate             = baselineConfig();
    byBitrate.videoBitrateKbps = 4000;
    const QStringList shaped   = mp4Arguments(byBitrate, 0, 0, "C:/tmp/rec/frames", "C:/tmp/rec/out.mp4", {}, "h264_nvenc");
    EXPECT_EQ(shaped.indexOf(QStringLiteral("-crf")), -1) << shaped.join(QLatin1Char(' ')).toStdString();
    EXPECT_EQ(shaped.value(shaped.indexOf(QStringLiteral("-b:v")) + 1), QStringLiteral("4000k"));
    EXPECT_NE(shaped.indexOf(QStringLiteral("yuv420p")), -1) << "换成码率控那一段时把像素格式弄丢了";
    EXPECT_NE(shaped.indexOf(QStringLiteral("+faststart")), -1) << "重排参数时把 faststart 弄丢了";

    const auto palette = gifPaletteArguments(config, 0, 0, "C:/tmp/rec/frames", "C:/tmp/rec/palette.png");
    EXPECT_TRUE(hasItemContaining(palette, QStringLiteral("palettegen=stats_mode=diff"))) << palette.join(QLatin1Char(' ')).toStdString();
    EXPECT_EQ(palette.last(), QStringLiteral("C:/tmp/rec/palette.png"));

    const auto gif = gifEncodeArguments(config, 0, 0, "C:/tmp/rec/frames", "C:/tmp/rec/palette.png", "C:/tmp/rec/out.gif");
    EXPECT_NE(gif.indexOf(QStringLiteral("-lavfi")), -1);
    EXPECT_TRUE(hasItemContaining(gif, QStringLiteral("paletteuse="))) << gif.join(QLatin1Char(' ')).toStdString();
    EXPECT_EQ(gif.count(QStringLiteral("-i")), 2) << "两遍法要有两路输入：帧序列与调色板";
    EXPECT_EQ(gif.last(), QStringLiteral("C:/tmp/rec/out.gif"));

    // GIF 走两遍调色板，画面码率对它没有意义：给了也不该混进命令行
    auto gifBitrate                  = baselineConfig();
    gifBitrate.format                = OutputFormat::Gif;
    gifBitrate.videoBitrateKbps      = 4000;
    const auto gifPaletteWithBitrate = gifPaletteArguments(gifBitrate, 0, 0, "C:/tmp/rec/frames", "C:/tmp/rec/palette.png");
    const auto gifEncodeWithBitrate  = gifEncodeArguments(gifBitrate, 0, 0, "C:/tmp/rec/frames", "C:/tmp/rec/palette.png", "C:/tmp/rec/out.gif");
    EXPECT_EQ(gifPaletteWithBitrate.indexOf(QStringLiteral("-b:v")), -1) << gifPaletteWithBitrate.join(QLatin1Char(' ')).toStdString();
    EXPECT_EQ(gifEncodeWithBitrate.indexOf(QStringLiteral("-b:v")), -1) << gifEncodeWithBitrate.join(QLatin1Char(' ')).toStdString();
}

/**
 * @brief 钉住「成品时长按这一轮实际达成的帧率铺开」
 * @details 机器跟不上目标帧率时帧序列是短的：本机实测按目标帧率编会把 6 秒编成 2.33 秒，
 *          画面快放、音轨还要被 -shortest 截尾。期望的分数按手算给，不从实现里再算一遍。
 */
TEST(RecordingFfmpeg, AchievedFrameRateDrivesTheCommandLine)
{
    // 70 帧摊在 6000ms 上：写成分数交给 ffmpeg，不做整数取整（11.5 取整成 12 会把十分钟拧成九分半）
    EXPECT_EQ(achievedFrameRateArgument(70, 6000, 30).toStdString(), "70000/6000");
    // 量不出帧数或时长才退回目标帧率：按 0 时长除会算出一个说不出含义的分数
    EXPECT_EQ(achievedFrameRateArgument(0, 6000, 30).toStdString(), "30");
    EXPECT_EQ(achievedFrameRateArgument(70, 0, 30).toStdString(), "30");

    const auto config = baselineConfig();

    const auto mp4 = mp4Arguments(config, 70, 6000, "C:/tmp/rec/frames", "C:/tmp/rec/out.mp4", {}, "libx264");
    EXPECT_EQ(mp4.value(mp4.indexOf(QStringLiteral("-framerate")) + 1).toStdString(), "70000/6000") << mp4.join(QLatin1Char(' ')).toStdString();

    // GIF 那一档同一个数要落在两处：输入按它铺开，滤镜按它抽帧，两处不同解就让调色板与画面错位
    const auto palette = gifPaletteArguments(config, 70, 6000, "C:/tmp/rec/frames", "C:/tmp/rec/palette.png");
    EXPECT_EQ(palette.value(palette.indexOf(QStringLiteral("-framerate")) + 1).toStdString(), "70000/6000");
    EXPECT_TRUE(hasItemContaining(palette, QStringLiteral("fps=70000/6000,palettegen=stats_mode=diff"))) << palette.join(QLatin1Char(' ')).toStdString();

    const auto gif = gifEncodeArguments(config, 70, 6000, "C:/tmp/rec/frames", "C:/tmp/rec/palette.png", "C:/tmp/rec/out.gif");
    EXPECT_TRUE(hasItemContaining(gif, QStringLiteral("fps=70000/6000[x]"))) << gif.join(QLatin1Char(' ')).toStdString();
}

/**
 * @brief 钉住编码超时的算法
 * @details 短片不能被余量算得太小误杀，长片也不能无限等。
 */
TEST(RecordingFfmpeg, EncodeTimeoutScalesWithDuration)
{
    EXPECT_EQ(GyanisScribe::Record::FfmpegRecordingEncoder::encodeTimeoutMs(10, 100), 70000) << "30 秒是底，10 秒的片子还要加 4 倍余量";
    EXPECT_EQ(GyanisScribe::Record::FfmpegRecordingEncoder::encodeTimeoutMs(10, 600), 270000) << "60 秒的片子要给 30 秒底 + 4 倍余量";
    EXPECT_EQ(GyanisScribe::Record::FfmpegRecordingEncoder::encodeTimeoutMs(0, 100), 30000) << "帧率非法时不许除零";
}

/**
 * @brief 钉住「找不到 ffmpeg」的拒绝面
 * @details 装没装ffmpeg是环境事实，两种结果都要有确定行为：找到就给绝对路径，
 *          没找到就报 DependencyMissing 并写清怎么补（重装或自装一份），而不是让 QProcess 抛一个看不懂的错。
 */
TEST(RecordingFfmpeg, LocatesOrRejectsWithInstallHint)
{
    const auto located = GyanisScribe::Record::locateFfmpeg();
    if (located)
    {
        EXPECT_TRUE(QFileInfo{*located}.isAbsolute()) << "找到的可执行文件要给绝对路径";
        return;
    }
    EXPECT_EQ(located.error().code, ErrorCode::DependencyMissing);
    EXPECT_NE(located.error().message.find("ffmpeg"), std::string::npos) << located.error().message;
    EXPECT_NE(located.error().message.find("PATH"), std::string::npos) << "文案要告诉用户怎么装";
}

/**
 * @brief 钉住倒计时期间不写帧、到点写第一帧
 * @details 第一帧无条件写：节拍把第 0 帧算作已取，等下去等于白丢开头一段画面。
 */
TEST(RecordingSessionCase, CountdownHoldsFramesThenStoresTheFirstOne)
{
    RecordingSink    sink;
    auto             config = baselineConfig();
    RecordingSession session{config, sink, 0, 3};

    const auto during = session.tick(0, frameOf());
    EXPECT_EQ(during.state, RecordingState::Countdown);
    EXPECT_EQ(during.countdownRemainingSeconds, 3);
    EXPECT_TRUE(during.frameStored == false);
    EXPECT_TRUE(sink.indices.empty());

    const auto almostThere = session.tick(2000, frameOf());
    EXPECT_EQ(almostThere.countdownRemainingSeconds, 1) << "剩 1 秒时报 0 会让用户以为已经开录了";

    const auto started = session.tick(3000, frameOf());
    EXPECT_EQ(started.state, RecordingState::Recording);
    EXPECT_TRUE(started.frameStored) << "倒计时结束那一帧必须落盘";
    EXPECT_EQ(sink.indices.size(), 1u);
    EXPECT_EQ(sink.indices[0], 0);
}

/**
 * @brief 钉住暂停：不写帧、不计时长，恢复后接原节拍
 * @details 用「虚拟时间」实现，判据是暂停 5 秒后恢复，第一帧仍然出现在恢复后的一格内，
 *          而不是按墙上时钟补发五秒的重复画面。
 */
TEST(RecordingSessionCase, PauseSkipsFramesAndDoesNotConsumeDuration)
{
    RecordingSink    sink;
    auto             config = baselineConfig();
    RecordingSession session{config, sink, 0, 0};

    EXPECT_TRUE(session.tick(0, frameOf()).frameStored);
    session.pause(100);
    EXPECT_EQ(session.state(), RecordingState::Paused);

    for (int now = 200; now <= 5000; now += 100)
    {
        EXPECT_FALSE(session.tick(now, frameOf()).frameStored) << now;
    }
    EXPECT_EQ(session.recordedDurationMs(5000), 100) << "时长要冻结在暂停那一刻：暂停前确实录了 100ms，之后不该再涨";

    session.resume(5100);
    EXPECT_EQ(session.state(), RecordingState::Recording);
    // 恢复后虚拟时间落在 1100（5100 - 累计暂停 5000），第 0 帧已在 100 前取过，下一格是 200 → 立刻可写
    EXPECT_TRUE(session.tick(5100, frameOf()).frameStored);
    EXPECT_EQ(sink.indices.size(), 2u);
}

/**
 * @brief 钉住时长上限与收尾
 * @details 触到上限时报给调用方 durationReached，由它收尾；finish 只封盘一次，
 *          收尾时一帧都没有要报错而不是把空目录交给编码器。
 */
TEST(RecordingSessionCase, DurationCapReportsAndFinishSealsOnce)
{
    RecordingSink sink;
    auto          config          = baselineConfig();
    config.maximumDurationSeconds = 1;
    RecordingSession session{config, sink, 0, 0};

    EXPECT_FALSE(session.tick(0, frameOf()).durationReached);
    const auto atCap = session.tick(1100, frameOf());
    EXPECT_TRUE(atCap.durationReached) << "超过 1 秒还不报上限，用户会以为还能一直录";

    EXPECT_TRUE(session.finish(1200).has_value());
    EXPECT_EQ(session.state(), RecordingState::Done);
    EXPECT_EQ(sink.sealCalls, 1);
    // 收尾之后再 tick 什么都不做
    const auto after = session.tick(2000, frameOf());
    EXPECT_EQ(after.state, RecordingState::Done);
    EXPECT_FALSE(after.frameStored);
    EXPECT_EQ(sink.sealCalls, 1);
}

/**
 * @brief 钉住「一帧都没录」时收尾报错
 * @details 空帧序列交给 ffmpeg 只会得到一句看不懂的报错，问题要在自己这一层就说清。
 */
TEST(RecordingSessionCase, FinishWithoutFramesRejects)
{
    RecordingSink    sink;
    auto             config = baselineConfig();
    RecordingSession session{config, sink, 0, 1};

    EXPECT_FALSE(session.tick(0, frameOf()).frameStored);
    const auto finished = session.finish(100);
    ASSERT_FALSE(finished.has_value());
    EXPECT_EQ(finished.error().code, ErrorCode::InternalError);
    EXPECT_EQ(session.state(), RecordingState::Failed);
}

/**
 * @brief 钉住写盘失败会中止整轮
 * @details 继续录只会得到一份缺帧的成品，那种文件比不录更坏；原因要留在 failure() 里给界面。
 */
TEST(RecordingSessionCase, SinkFailureAbortsTheSession)
{
    RecordingSink sink;
    sink.failAt      = 2;
    auto config      = baselineConfig();
    config.frameRate = 10;
    RecordingSession session{config, sink, 0, 0};

    EXPECT_TRUE(session.tick(0, frameOf()).frameStored);
    EXPECT_TRUE(session.tick(100, frameOf()).frameStored);
    const auto third = session.tick(200, frameOf());
    EXPECT_EQ(third.state, RecordingState::Failed);
    EXPECT_EQ(session.failure().code, ErrorCode::FileWriteFailed);
    EXPECT_NE(session.failure().message.find("第 2 帧写坏了"), std::string::npos);

    // 失败之后再 tick 一律不动
    const auto later = session.tick(300, frameOf());
    EXPECT_EQ(later.state, RecordingState::Failed);
    EXPECT_FALSE(later.frameStored);
    EXPECT_EQ(sink.indices.size(), 2u);
}

/**
 * @brief 钉住 PNG 序列真的落盘，文件名与序号同解
 * @details 这一条在临时目录里真写文件：只有写盘路径与文件名模板一致时，ffmpeg 才认这段序列。
 */
TEST(RecordingSinksCase, PngSequenceWritesNumberedFiles)
{
    const auto      directory = probeDirectory("PngSequence");
    std::error_code cleanup;
    std::filesystem::remove_all(directory, cleanup);
    ASSERT_NO_THROW(std::filesystem::create_directories(directory));

    PngSequenceSink sink{directory};
    ASSERT_TRUE(sink.write(frameOf(16, 8), 0).has_value());
    ASSERT_TRUE(sink.write(frameOf(16, 8), 1).has_value());
    EXPECT_EQ(sink.writtenFrames(), 2);
    EXPECT_TRUE(std::filesystem::exists(directory / "000000.png"));
    EXPECT_TRUE(std::filesystem::exists(directory / "000001.png"));

    const auto nullFrame = QImage{};
    const auto rejected  = sink.write(nullFrame, 2);
    ASSERT_FALSE(rejected.has_value()) << "空图写盘应当被拒而不是落一张 0 字节的 PNG";
    EXPECT_EQ(rejected.error().code, ErrorCode::InvalidArgument);

    EXPECT_TRUE(sink.seal().has_value());
    std::filesystem::remove_all(directory, cleanup);
}

/**
 * @brief 钉住空序列的封盘被拒
 * @details 与上一条件互补：一个帧都没写时 seal 必须报错，否则编码器会拿到一个空目录。
 */
TEST(RecordingSinksCase, EmptySequenceCannotBeSealed)
{
    const auto      directory = probeDirectory("EmptySeal");
    PngSequenceSink sink{directory};
    const auto      sealed = sink.seal();
    ASSERT_FALSE(sealed.has_value());
    EXPECT_EQ(sealed.error().code, ErrorCode::InternalError);
}

/**
 * @brief 钉住配置里默认帧率的可用范围与拒绝面
 * @details 拒绝面要给出范围与恢复办法：帧率是用户在设置页里填的，
 *          只回「不合法」等于让他自己猜上限在哪儿。
 */
TEST(RecorderFrameRateSetting, AcceptsInRangeAndRejectsOutside)
{
    EXPECT_EQ(GyanisScribe::Record::parseFrameRateSetting(1).value(), 1);
    EXPECT_EQ(GyanisScribe::Record::parseFrameRateSetting(GyanisScribe::Record::kDefaultFrameRate).value(), 15);
    EXPECT_EQ(GyanisScribe::Record::parseFrameRateSetting(GyanisScribe::Record::kMaximumFrameRate).value(), 60);

    for (const int bad: {0, -1, 61, 1000})
    {
        const auto parsed = GyanisScribe::Record::parseFrameRateSetting(bad);
        ASSERT_FALSE(parsed.has_value()) << bad;
        EXPECT_EQ(parsed.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument) << bad;
        EXPECT_NE(parsed.error().message.find("60"), std::string::npos) << bad;
        EXPECT_NE(parsed.error().message.find("Record/DefaultFrameRate"), std::string::npos) << bad;
    }
}

/**
 * @brief 钉住最长录制时长的范围、拒绝面与缺省那一处真源
 * @details 上限必须与 `validateRecorderConfig` 用的是同一个常量：设置页摆得出去、开始那一刻却拒收，
 *          是这一格最容易出的岔子。缺省那一条同时钉住「结构体初值与解析回落是同一个数」——
 *          两处各写 600 时改一处漏一处，用户会看到「设置里默认十分钟、录起来却是别的」。
 */
TEST(RecorderDurationSetting, DefaultsComeFromOneConstantAndOutOfRangeIsRejected)
{
    EXPECT_EQ(GyanisScribe::Record::RecorderConfig{}.maximumDurationSeconds, GyanisScribe::Record::kDefaultMaximumDurationSeconds) << "结构体初值与该常量分家";
    EXPECT_EQ(GyanisScribe::Record::parseMaximumDurationSetting(1).value(), 1) << "1 秒也是可用值，不许当坏值";
    EXPECT_EQ(GyanisScribe::Record::parseMaximumDurationSetting(GyanisScribe::Record::kDefaultMaximumDurationSeconds).value(), 600);
    EXPECT_EQ(GyanisScribe::Record::parseMaximumDurationSetting(GyanisScribe::Record::kMaximumDurationSeconds).value(), 7200);

    for (const int bad: {0, -1, 7201})
    {
        const auto parsed = GyanisScribe::Record::parseMaximumDurationSetting(bad);
        ASSERT_FALSE(parsed.has_value()) << bad;
        EXPECT_EQ(parsed.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument) << bad;
        EXPECT_NE(parsed.error().message.find("Record/MaximumDurationSeconds"), std::string::npos) << bad;
    }
}

/**
 * @brief 钉住画面码率的读法：0 是一档而不是「没填」，越界要点名范围与配置键
 * @details 0 这一档叫「按质量优先」，把它当成没配而回落某个具体数值，就等于替用户选了另一种控法。
 */
TEST(RecorderVideoBitrateSetting, ZeroIsAChoiceAndOutsideRangeIsRejected)
{
    EXPECT_EQ(GyanisScribe::Record::parseVideoBitrateSetting(GyanisScribe::Record::kDefaultVideoBitrateKbps).value(), 0);
    EXPECT_EQ(GyanisScribe::Record::parseVideoBitrateSetting(4000).value(), 4000);
    EXPECT_EQ(GyanisScribe::Record::parseVideoBitrateSetting(GyanisScribe::Record::kMaximumVideoBitrateKbps).value(), 50000);

    for (const int bad: {-1, 50001, 5000000})
    {
        const auto parsed = GyanisScribe::Record::parseVideoBitrateSetting(bad);
        ASSERT_FALSE(parsed.has_value()) << bad;
        EXPECT_EQ(parsed.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument) << bad;
        EXPECT_NE(parsed.error().message.find("50000"), std::string::npos) << bad;
        EXPECT_NE(parsed.error().message.find("Record/VideoBitrateKbps"), std::string::npos) << bad;
    }
}

/**
 * @brief 钉住倒计时秒数的解析：0 是「立即开始」这一档，越界点名范围与键名
 * @details 0 会被当成「没填」的话，选「立即开始」的用户每次都被莫名其妙数三秒。
 */
TEST(RecordCountdownSetting, ParsesWithItsOwnRange)
{
    EXPECT_EQ(GyanisScribe::Record::parseCountdownSetting(0).value(), 0) << "0 是「立即开始」这一档，不是没填";
    EXPECT_EQ(GyanisScribe::Record::parseCountdownSetting(GyanisScribe::Record::kDefaultCountdownSeconds).value(), 3);
    EXPECT_EQ(GyanisScribe::Record::parseCountdownSetting(GyanisScribe::Record::kMaximumCountdownSeconds).value(), 10);

    for (const int bad: {-1, 11, 900})
    {
        const auto parsed = GyanisScribe::Record::parseCountdownSetting(bad);
        ASSERT_FALSE(parsed.has_value()) << bad;
        EXPECT_EQ(parsed.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument) << bad;
        EXPECT_NE(parsed.error().message.find("10"), std::string::npos) << bad;
        EXPECT_NE(parsed.error().message.find("Record/CountdownSeconds"), std::string::npos) << bad;
    }
}

/**
 * @brief 钉住「哪种输出带得了声音」：词表里只有 GIF 带不了
 * @details 这一句同时管着设置页置灰哪两格与 RecordingService 跳过不跳过取声。新加一档格式时
 *          循环那条会要求作者表态它带不带音轨，而不是让取回来的声音默默落进一个没人读的容器。
 */
TEST(OutputFormatCarriesAudio, OnlyGifCannotKeepASoundTrack)
{
    EXPECT_TRUE(GyanisScribe::Record::outputFormatCarriesAudio(GyanisScribe::Record::OutputFormat::Mp4));
    EXPECT_FALSE(GyanisScribe::Record::outputFormatCarriesAudio(GyanisScribe::Record::OutputFormat::Gif));

    for (const auto &choice: GyanisScribe::Record::kRecordFormatChoices)
    {
        EXPECT_EQ(GyanisScribe::Record::outputFormatCarriesAudio(choice.format), choice.format == GyanisScribe::Record::OutputFormat::Mp4) << std::string{choice.token};
    }
}

/**
 * @brief 钉住录制范围词表：每一档写得出去也读得回来，读不出时报错要点名可取的值
 * @details 这张表是设置页、配置文件与录屏读端三方共用的唯一出处。漏一档就有一档在界面上选得出来
 *          却读不回去；两行同 token 则 parseRecordScope 先命中哪条说不准，下拉框也会出现同义的两行。
 */
TEST(RecordScopeSetting, TableRoundTripsEveryModeAndRejectsUnknownToken)
{
    for (const auto &choice: GyanisScribe::Record::kRecordScopeChoices)
    {
        const auto parsed = GyanisScribe::Record::parseRecordScope(QString::fromUtf8(std::string{choice.token}));
        ASSERT_TRUE(static_cast<bool>(parsed)) << choice.token;
        EXPECT_EQ(*parsed, choice.mode) << choice.token;
        EXPECT_EQ(GyanisScribe::Record::recordScopeText(choice.mode), choice.token) << choice.token;
    }

    // 枚举里每一档都要在表里有一行：新加 RecordMode 而忘了补表时，recordScopeText 交回的是空串
    for (const auto mode: {GyanisScribe::Record::RecordMode::FullScreen, GyanisScribe::Record::RecordMode::Region, GyanisScribe::Record::RecordMode::Window})
    {
        EXPECT_FALSE(GyanisScribe::Record::recordScopeText(mode).empty()) << "词表漏了 RecordMode 的某一档";
    }

    // 大小写、尾随空格与中文标签都不认：认了就等于替用户猜一档，录下来的画面可能与他要的完全不同
    for (const QString &bad: {QStringLiteral(""), QStringLiteral("region"), QStringLiteral("FullScreen "), QStringLiteral("整屏")})
    {
        const auto parsed = GyanisScribe::Record::parseRecordScope(bad);
        ASSERT_FALSE(static_cast<bool>(parsed)) << bad.toStdString();
        EXPECT_EQ(parsed.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument) << bad.toStdString();
        EXPECT_NE(parsed.error().message.find("FullScreen"), std::string::npos) << bad.toStdString();
        EXPECT_NE(parsed.error().message.find("Record/Scope"), std::string::npos) << bad.toStdString();
    }
}

/**
 * @brief 钉住取声命令的一路、两路与「一路都没给」三种形状
 * @details 一路时不能出现 amix：那是两个输入才有的滤镜，多出去 ffmpeg 会直接报输入数不对。
 *          两路时必须按麦克风在前、系统声在后数 [0:a]/[1:a]，数反了用户选的就不是他听见的。
 */
TEST(FfmpegAudioCaptureArguments, BuildsOneOrTwoInputsAndRefusesNone)
{
    const QString mic{QStringLiteral("@device_cm_{GUID}") + QStringLiteral("\\wave_{MIC}")};
    const QString mix{QStringLiteral("@device_cm_{GUID}") + QStringLiteral("\\wave_{MIX}")};
    const auto    wav = std::filesystem::path("C:/tmp/audio.wav");

    const auto none = GyanisScribe::Record::audioCaptureArguments(QString(), QString(), wav);
    ASSERT_FALSE(static_cast<bool>(none)) << "两路都没给却拼出了一条命令";
    EXPECT_EQ(none.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument);
    EXPECT_NE(none.error().message.find("设备"), std::string::npos);

    const auto one = GyanisScribe::Record::audioCaptureArguments(mic, QString(), wav);
    ASSERT_TRUE(static_cast<bool>(one)) << one.error().message;
    EXPECT_EQ(one->count(QStringLiteral("-i")), 1);
    EXPECT_TRUE(one->contains(QStringLiteral("audio=%1").arg(mic))) << one->join(QStringLiteral(" ")).toStdString();
    EXPECT_FALSE(one->contains(QStringLiteral("-filter_complex"))) << "一路输入不该带混音滤镜";
    EXPECT_TRUE(one->contains(QStringLiteral("pcm_s16le")));
    EXPECT_TRUE(one->last().endsWith(QStringLiteral("audio.wav"))) << one->last().toStdString();

    const auto two = GyanisScribe::Record::audioCaptureArguments(mic, mix, wav);
    ASSERT_TRUE(static_cast<bool>(two)) << two.error().message;
    EXPECT_EQ(two->count(QStringLiteral("-i")), 2);
    EXPECT_TRUE(two->contains(QStringLiteral("[0:a][1:a]amix=inputs=2:duration=longest:dropout_transition=0[a]")));
    // 顺序就是 [0:a]/[1:a] 的次序：麦克风必须排在第一个 -i 上
    EXPECT_EQ(two->indexOf(QStringLiteral("audio=%1").arg(mic)) < two->indexOf(QStringLiteral("audio=%1").arg(mix)), true) << "两路的顺序反了";
}

/**
 * @brief 用真 ffmpeg 验一次取声命令的形状（环境变量门控）
 * @details 拼装函数只按我猜的形状断言是不够的：滤镜图、-f 与 -i 的配对方式只要有一处 ffmpeg
 *          不认，界面上就只会剩一句「起不来」。给它一台真 ffmpeg 与一路真设备，这条就跑到底。
 *          缺任一环境变量就 SKIP，仓库里不留任何设备名或路径。
 */
TEST(FfmpegAudioCaptureEndToEnd, CapturesAReadableWavWithARealFfmpeg)
{
    const auto program = qEnvironmentVariable("GYANISSCRIBE_FFMPEG");
    const auto device  = qEnvironmentVariable("GYANISSCRIBE_AUDIO_DEVICE");
    if (program.isEmpty() || device.isEmpty())
    {
        GTEST_SKIP() << "没有 GYANISSCRIBE_FFMPEG 或 GYANISSCRIBE_AUDIO_DEVICE，这一条要真 ffmpeg 与一路真设备";
    }

    const auto wav   = std::filesystem::temp_directory_path() / "GyanisScribe-audio-capture-probe.wav";
    const auto built = GyanisScribe::Record::audioCaptureArguments(device, QString(), wav);
    ASSERT_TRUE(static_cast<bool>(built)) << built.error().message;

    QStringList arguments = *built;
    // 探针只采两秒：-t 是输出的选项，必须落在输出文件名之前，把文件名先摘下来再补回去最直白
    const auto outputFile = arguments.takeLast();
    arguments << QStringLiteral("-t") << QStringLiteral("2") << outputFile;

    std::error_code ignored;
    std::filesystem::remove(wav, ignored);

    QProcess recorder;
    recorder.setProcessChannelMode(QProcess::MergedChannels);
    recorder.start(program, arguments);
    ASSERT_TRUE(recorder.waitForStarted(5000)) << "ffmpeg 起不来：" << program.toStdString();
    ASSERT_TRUE(recorder.waitForFinished(20000)) << "两秒的采集没在 20 秒内收尾：" << recorder.readAll().toStdString();

    const auto bytes = std::filesystem::file_size(wav, ignored);
    ASSERT_FALSE(ignored) << "取声命令跑完了却没留下 wav。argv=[" << arguments.join(QStringLiteral(" ")).toStdString() << "] 输出=" << wav.string()
                          << " 进程输出=" << QString::fromUtf8(recorder.readAll()).toStdString();
    EXPECT_GT(bytes, 44u) << "只有 wav 头等于什么都没录到";
    std::filesystem::remove(wav, ignored);
}

/**
 * @brief 钉住输出格式词表：两档都写得出去也读得回来，读不出时报错点名可取的值与键
 * @details 猜错格式比猜错范围更贵——帧已经抓完才按错的容器压出去，整段成品作废；
 *          而 GIF 的帧率上限另有一档（见 kMaximumGifFrameRate），报错文案得让人看得懂该改哪一格。
 */
TEST(RecordFormatSetting, TableRoundTripsEveryFormatAndRejectsUnknownToken)
{
    for (const auto &choice: GyanisScribe::Record::kRecordFormatChoices)
    {
        const auto parsed = GyanisScribe::Record::parseRecordFormat(QString::fromUtf8(std::string{choice.token}));
        ASSERT_TRUE(static_cast<bool>(parsed)) << choice.token;
        EXPECT_EQ(*parsed, choice.format) << choice.token;
        EXPECT_EQ(GyanisScribe::Record::recordFormatText(choice.format), choice.token) << choice.token;
    }

    // 枚举里每一档都要在表里有一行：新加 OutputFormat 而忘了补表时，recordFormatText 交回的是空串
    for (const auto format: {GyanisScribe::Record::OutputFormat::Mp4, GyanisScribe::Record::OutputFormat::Gif})
    {
        EXPECT_FALSE(GyanisScribe::Record::recordFormatText(format).empty()) << "词表漏了 OutputFormat 的某一档";
    }

    for (const QString &bad: {QStringLiteral("mp4"), QStringLiteral("GIF"), QStringLiteral("Mp4 ")})
    {
        const auto parsed = GyanisScribe::Record::parseRecordFormat(bad);
        ASSERT_FALSE(static_cast<bool>(parsed)) << bad.toStdString();
        EXPECT_EQ(parsed.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument) << bad.toStdString();
        EXPECT_NE(parsed.error().message.find("Gif"), std::string::npos) << bad.toStdString();
        EXPECT_NE(parsed.error().message.find("Record/Format"), std::string::npos) << bad.toStdString();
    }
}

/**
 * @brief 钉住「随包的 ffmpeg 排在 PATH 之前」
 * @details 安装包自带这一件才能装完即用，只有程序目录里没有它时才回落到 PATH。
 *          文件名按平台不同（Windows 带 .exe），所以两头都钉：没有时报空、放了同名文件才报路径。
 */
TEST(RecordingFfmpeg, PrefersTheCopyShippedNextToTheExecutable)
{
    const auto      root = std::filesystem::temp_directory_path() / "GyanisScribe-FfmpegLookup";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root);

    const auto directory = QString::fromStdWString(root.wstring());
    EXPECT_TRUE(GyanisScribe::Record::bundledFfmpegPath(directory).isEmpty()) << "程序目录里没有它时不该报出路径";
    EXPECT_TRUE(GyanisScribe::Record::bundledFfmpegPath(QString{}).isEmpty()) << "空目录名要按「没有」处理";

#ifdef Q_OS_WIN
    constexpr const char *kProgramFileName = "ffmpeg.exe";
#else
    constexpr const char *kProgramFileName = "ffmpeg";
#endif
    std::ofstream{root / kProgramFileName} << "假可执行文件，只用来验查找";

    const auto found = GyanisScribe::Record::bundledFfmpegPath(directory);
    // 两边都过一遍 QFileInfo：std::filesystem 给反斜杠、Qt 给正斜杠，直接比字符串会假红
    EXPECT_EQ(found, QFileInfo{QString::fromStdWString((root / kProgramFileName).wstring())}.absoluteFilePath()) << "找到的必须是程序目录里那一份";

    std::filesystem::remove_all(root, ignored);
}
