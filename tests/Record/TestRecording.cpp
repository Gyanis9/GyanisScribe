#include "Record/FfmpegCommand.h"
#include "Record/RecorderConfig.h"
#include "Record/RecordingSession.h"
#include "Record/RecordingSinks.h"
#include "Record/RecordingTiming.h"
#include "Record/FfmpegRecordingEncoder.h"


#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QStringList>
#include <QImage>
#include <QString>

#include <filesystem>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

using GyanisScribe::Core::CapturedImage;
using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Core::Expected;
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
                return std::unexpected(GyanisScribe::Core::Error{ErrorCode::FileWriteFailed, "第 2 帧写坏了"});
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
                return std::unexpected(GyanisScribe::Core::Error{ErrorCode::InternalError, "一帧都没有"});
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

    const QStringList silent = mp4Arguments(config, "C:/tmp/rec/frames", "C:/tmp/rec/out.mp4", {});
    EXPECT_EQ(silent.first(6).join(QLatin1Char('|')), QStringLiteral("-y|-framerate|10|-i|C:/tmp/rec/frames\\%06d.png|-vf")) << silent.join(QLatin1Char(' ')).toStdString();
    EXPECT_NE(silent.indexOf(QStringLiteral("-c:v")), -1);
    EXPECT_EQ(silent.value(silent.indexOf(QStringLiteral("-c:v")) + 1), QStringLiteral("libx264"));
    EXPECT_NE(silent.indexOf(QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2")), -1) << "奇数尺寸的截图会被 x264 拒掉";
    EXPECT_NE(silent.indexOf(QStringLiteral("-an")), -1) << "没声音时要显式 -an";
    EXPECT_NE(silent.indexOf(QStringLiteral("+faststart")), -1);
    EXPECT_EQ(silent.last(), QStringLiteral("C:/tmp/rec/out.mp4"));

    const auto withAudio = mp4Arguments(config, "C:/tmp/rec/frames", "C:/tmp/rec/out.mp4", "C:/tmp/rec/audio.wav");
    EXPECT_EQ(withAudio.indexOf(QStringLiteral("-an")), -1) << "有配音时不该再关声轨";
    EXPECT_NE(withAudio.indexOf(QStringLiteral("-shortest")), -1);
    EXPECT_NE(withAudio.indexOf(QStringLiteral("aac")), -1);

    const auto palette = gifPaletteArguments(config, "C:/tmp/rec/frames", "C:/tmp/rec/palette.png");
    EXPECT_TRUE(hasItemContaining(palette, QStringLiteral("palettegen=stats_mode=diff"))) << palette.join(QLatin1Char(' ')).toStdString();
    EXPECT_EQ(palette.last(), QStringLiteral("C:/tmp/rec/palette.png"));

    const auto gif = gifEncodeArguments(config, "C:/tmp/rec/frames", "C:/tmp/rec/palette.png", "C:/tmp/rec/out.gif");
    EXPECT_NE(gif.indexOf(QStringLiteral("-lavfi")), -1);
    EXPECT_TRUE(hasItemContaining(gif, QStringLiteral("paletteuse="))) << gif.join(QLatin1Char(' ')).toStdString();
    EXPECT_EQ(gif.count(QStringLiteral("-i")), 2) << "两遍法要有两路输入：帧序列与调色板";
    EXPECT_EQ(gif.last(), QStringLiteral("C:/tmp/rec/out.gif"));
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
 *          没找到就报 DependencyMissing 并写清怎么装，而不是让 QProcess 去抛一个看不懂的错。
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
