// RecordingService 的直测。抓屏与真编码留给桌面会话，这里钉的是这一层自己的两个决定：
// 哪种输出该跳过取声（音频档位），以及 stop() 之后编码是不是真的不在界面线程上等（线程交接）。
// 假编码器把 encode() 停在门闩上，「调用已返回而活还没干完」因此成了可断言的事。

#include "Capture/IScreenCaptureSource.h"
#include "Core/Paths.h"
#include "Core/Types.h"
#include "Record/FfmpegRecordingEncoder.h"
#include "Record/RecorderConfig.h"
#include "Record/RecordingService.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStandardPaths>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

using GyanisScribe::Capture::IScreenCaptureSource;
using GyanisScribe::Core::CapturedImage;
using GyanisScribe::Core::Error;
using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Core::Expected;
using GyanisScribe::Record::AudioCaptureOutcome;
using GyanisScribe::Record::IRecordingEncoder;
using GyanisScribe::Record::OutputFormat;
using GyanisScribe::Record::RecorderConfig;
using GyanisScribe::Record::RecordingService;

namespace
{
    // argv 的缓冲要活过应用对象的整个生命期，所以放在文件作用域
    char  applicationArgument[]  = "TestRecordingService";
    char *applicationArguments[] = {applicationArgument, nullptr};

    std::unique_ptr<QCoreApplication> application;

    /// 用例放开门闩之前，假编码器最多停这么久：同步实现也能在有限时间内把控制权交回用例
    constexpr std::chrono::milliseconds kGatePatience{2000};

    /// 等条件成立的上限
    constexpr std::chrono::milliseconds kWaitPatience{2000};

    /// 开始一轮之后要转多久界面循环才攒到帧：15 帧率下 1.3 秒能落十几帧，够 stop() 收尾不报「一帧都没有」
    constexpr std::chrono::milliseconds kFrameSpin{1300};

    /// 不真抓屏的来源：只给一张纯色帧，够会话落盘
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

    /// 一次编码调用的现场
    struct EncodeCall
    {
        std::thread::id       thread;         ///< 编发生在哪条线程
        std::filesystem::path frameDirectory; ///< 交给它的那一份帧目录（快照，不是服务的成员）
        int                   frameCount = 0; ///< 交过去的帧数
    };

    /**
     * @brief 停在门闩上的假编码器
     * @details 观测点只有原子量与加锁快照：测试线程与工作线程各读各的，不靠调度先后。
     */
    class GatedRecordingEncoder final : public IRecordingEncoder
    {
    public:
        [[nodiscard]] Expected<std::filesystem::path> encode(const RecorderConfig &, const std::filesystem::path &frameDirectory, const int frameCount,
                                                             const std::filesystem::path &) override
        {
            {
                const std::lock_guard guard{m_mutex};
                m_calls.push_back(EncodeCall{std::this_thread::get_id(), frameDirectory, frameCount});
            }

            const auto released = m_gate.try_acquire_for(kGatePatience);
            m_finished          = true;

            if (!released)
            {
                return std::unexpected(Error{ErrorCode::Busy, "假编码器等门闩等到超时"});
            }
            if (m_failsNext)
            {
                return std::unexpected(Error{ErrorCode::InternalError, "假编码器说编不动"});
            }
            return frameDirectory / "product.mp4";
        }

        /// 放开一次门闩，让一次编码走完
        void release()
        {
            m_gate.release();
        }

        /// 让下一次编码走失败那一路
        void failNext()
        {
            m_failsNext = true;
        }

        /// 已经进来的调用次数
        [[nodiscard]] int callCount() const
        {
            const std::lock_guard guard{m_mutex};
            return static_cast<int>(m_calls.size());
        }

        /// 第 index 次调用的现场
        [[nodiscard]] EncodeCall callAt(const int index) const
        {
            const std::lock_guard guard{m_mutex};
            return m_calls[static_cast<size_t>(index)];
        }

        /// 最近一次 encode() 有没有返回
        [[nodiscard]] bool isFinished() const
        {
            return m_finished;
        }

    private:
        mutable std::mutex         m_mutex;             ///< 保护 m_calls
        std::vector<EncodeCall>    m_calls;             ///< 按调用顺序记下的现场
        std::counting_semaphore<8> m_gate{0};           ///< 门闩：放开一次放行一次编码
        std::atomic<bool>          m_finished{false};   ///< 最近一次编码是否已返回
        bool                       m_failsNext = false; ///< 下一次编码该走失败出口
    };

    /**
     * @brief 有界轮询等条件成立，其间把界面循环转起来
     * @param predicate 要等的条件
     * @return 成立返回 true；到上限仍不成立返回 false（用例据此判红而不是挂住）
     */
    template<typename Predicate>
    bool spinUntil(const Predicate &predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + kWaitPatience;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
            {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return predicate();
    }

    /// 转界面循环攒几帧：不转的话节拍定时器一跳都不会发生
    void spinFramesIn()
    {
        const auto deadline = std::chrono::steady_clock::now() + kFrameSpin;
        while (std::chrono::steady_clock::now() < deadline)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    }

    /**
     * @brief 一份能通过校验的配置
     * @param format 输出格式
     * @param withAudio true 表示把取声那一格填上——没有 ffmpeg 的现场那一轮会直接开不了
     * @return 交给 start() 的参数
     */
    RecorderConfig serviceConfig(const OutputFormat format, const bool withAudio)
    {
        auto config                   = RecorderConfig{};
        config.format                 = format;
        config.screenName             = QStringLiteral("screen0");
        config.frameRate              = 15;
        config.maximumDurationSeconds = 60;
        config.outputDirectory        = std::filesystem::temp_directory_path();
        if (withAudio)
        {
            config.microphoneDevice = QStringLiteral("@device_pnp_mock_mic");
        }
        return config;
    }

    class RecordingServiceFixture : public ::testing::Test
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

        void TearDown() override
        {
            // 开始一轮就在缓存里建一个带时间戳的帧目录，不等它收尾会一层层攒下来
            std::error_code ignored;
            std::filesystem::remove_all(GyanisScribe::Core::Paths::cacheDirectory() / "Recording", ignored);
        }
    };

    /**
     * @brief 钉住 GIF 那一档不为一声音起进程
     * @details 去掉服务里的格式判断，这条在两种现场都会红：这台机器没有 ffmpeg 时，取声那一步会把整轮录屏拦住；
     *          装了 ffmpeg 的机器上，它会真起一个进程去取一份没人读的声音。
     */
    TEST_F(RecordingServiceFixture, GifWithAudioRequestedStartsWithoutTakingSound)
    {
        StubCaptureSource     source;
        GatedRecordingEncoder encoder;
        RecordingService      service{source, encoder};

        const auto started = service.start(serviceConfig(OutputFormat::Gif, true), 0);
        ASSERT_TRUE(static_cast<bool>(started)) << started.error().message;
        EXPECT_EQ(service.audioOutcome(), AudioCaptureOutcome::NotRequested) << "GIF 带不了声音，这一轮不该把取声算成在跑";
    }

    /**
     * @brief 配对的那一条：MP4 选了取声仍然真的要去起 ffmpeg
     * @details 上一那条容易被写成「什么格式都跳过取声」——那种做法会把 MP4 的音轨一起关掉。
     *          这里在没有 ffmpeg 的现场断「起不动就拒绝开始」，跳过用在了对的档位上才算得住。
     */
    TEST_F(RecordingServiceFixture, Mp4WithAudioRequestedStillWantsFfmpeg)
    {
        if (!QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
        {
            GTEST_SKIP() << "这台机器 PATH 上有 ffmpeg，这一条要的是「没有它」的那一种现场";
        }

        StubCaptureSource     source;
        GatedRecordingEncoder encoder;
        RecordingService      service{source, encoder};

        const auto started = service.start(serviceConfig(OutputFormat::Mp4, true), 0);
        ASSERT_FALSE(static_cast<bool>(started)) << "MP4 也要跳过取声了：音轨这一档整个没了";
        EXPECT_NE(started.error().message.find("ffmpeg"), std::string::npos) << started.error().message;
    }

    /**
     * @brief 钉住「stop 返回时编码还在跑」
     * @details 这是这次改动的全部理由：编码要几秒到几十秒，同步等就是把界面停在那儿。
     */
    TEST_F(RecordingServiceFixture, StopReturnsWhileTheEncodeIsStillRunning)
    {
        StubCaptureSource     source;
        GatedRecordingEncoder encoder;
        RecordingService      service{source, encoder};

        ASSERT_TRUE(static_cast<bool>(service.start(serviceConfig(OutputFormat::Mp4, false), 0)));
        spinFramesIn();

        QString         finishedPath;
        std::thread::id deliveryThread;
        QObject::connect(&service, &RecordingService::finished,
                         [&finishedPath, &deliveryThread](const QString &path)
                         {
                             finishedPath   = path;
                             deliveryThread = std::this_thread::get_id();
                         });

        const auto stopped = service.stop();
        ASSERT_TRUE(static_cast<bool>(stopped)) << stopped.error().message;
        ASSERT_TRUE(spinUntil([&] { return encoder.callCount() == 1; })) << "stop 之后一次编码都没开始";
        EXPECT_FALSE(encoder.isFinished()) << "stop() 返回时编码已经跑完：它还堵在界面线程上";
        EXPECT_NE(encoder.callAt(0).thread, std::this_thread::get_id()) << "encode() 跑在界面线程上";
        EXPECT_GT(encoder.callAt(0).frameCount, 0) << "交给编码的帧数是 0，说明这一轮根本没落帧";
        EXPECT_TRUE(finishedPath.isEmpty()) << "成品信号在编码跑完之前就发了";

        encoder.release();
        ASSERT_TRUE(spinUntil([&] { return !finishedPath.isEmpty(); })) << "放开门闩之后成品一直没送回来";
        EXPECT_TRUE(finishedPath.contains(QStringLiteral("product.mp4"))) << finishedPath.toStdString();
        EXPECT_EQ(deliveryThread, std::this_thread::get_id()) << "成品不在界面线程上送，处置目录与发信号就都落在别的线程上了";
    }

    /**
     * @brief 钉住编码失败那一路：报 failed，且临时帧留着
     * @details 失败时删帧目录等于把用户已经录下的内容抹掉，连换参数重编码的余地都没了。
     */
    TEST_F(RecordingServiceFixture, EncodeFailureKeepsTheFramesAndReports)
    {
        StubCaptureSource     source;
        GatedRecordingEncoder encoder;
        encoder.failNext();
        RecordingService service{source, encoder};

        ASSERT_TRUE(static_cast<bool>(service.start(serviceConfig(OutputFormat::Mp4, false), 0)));
        spinFramesIn();

        QString failure;
        QObject::connect(&service, &RecordingService::failed, [&failure](const QString &reason) { failure = reason; });

        ASSERT_TRUE(static_cast<bool>(service.stop()));
        ASSERT_TRUE(spinUntil([&] { return encoder.callCount() == 1; }));
        const auto frameDirectory = encoder.callAt(0).frameDirectory;
        ASSERT_TRUE(std::filesystem::exists(frameDirectory));

        encoder.release();
        ASSERT_TRUE(spinUntil([&] { return !failure.isEmpty(); })) << "编码失败了却没报出来";
        EXPECT_NE(failure.toStdString().find("编不动"), std::string::npos) << failure.toStdString();
        EXPECT_TRUE(std::filesystem::exists(frameDirectory)) << "编码失败却把临时帧删了，用户连重编码的余地都没有";
    }

    /**
     * @brief 钉住「上一轮还在编码时开下一轮」用的是各自的帧目录
     * @details 成员那份 m_frameDirectory 会被下一轮覆盖，编码与收尾处置必须认快照那一份；
     *          认错了就会拿第二轮的目录去做第一轮该删的事。
     */
    TEST_F(RecordingServiceFixture, SecondRecordingGetsItsOwnFrameDirectoryWhileTheFirstEncodes)
    {
        StubCaptureSource     source;
        GatedRecordingEncoder encoder;
        RecordingService      service{source, encoder};

        ASSERT_TRUE(static_cast<bool>(service.start(serviceConfig(OutputFormat::Mp4, false), 0)));
        spinFramesIn();
        ASSERT_TRUE(static_cast<bool>(service.stop()));
        ASSERT_TRUE(spinUntil([&] { return encoder.callCount() == 1; }));
        const auto firstDirectory = encoder.callAt(0).frameDirectory;
        EXPECT_TRUE(std::filesystem::exists(firstDirectory)) << "第一轮还在编，它的目录就已经被人删了";

        ASSERT_TRUE(static_cast<bool>(service.start(serviceConfig(OutputFormat::Mp4, false), 0)));
        spinFramesIn();
        ASSERT_TRUE(static_cast<bool>(service.stop()));
        ASSERT_TRUE(spinUntil([&] { return encoder.callCount() == 2; })) << "上一轮还在编时开不了新一轮";
        const auto secondDirectory = encoder.callAt(1).frameDirectory;
        EXPECT_NE(firstDirectory, secondDirectory) << "两轮录屏写进了同一个帧目录";

        int finishedCount = 0;
        QObject::connect(&service, &RecordingService::finished, [&finishedCount](const QString &) { ++finishedCount; });

        encoder.release();
        encoder.release();
        ASSERT_TRUE(spinUntil([&] { return finishedCount == 2; })) << "两轮编码没收齐成品";
        EXPECT_FALSE(std::filesystem::exists(firstDirectory)) << "第一轮编完该删的是它自己那一份目录";
        EXPECT_FALSE(std::filesystem::exists(secondDirectory)) << "第二轮编完该删的是它自己那一份目录";
    }

    /**
     * @brief 钉住析构会等手上的编码跑完
     * @details 编码器是服务持有的引用，那条线程还在用它；不等就是让工作线程拿着已析构的对象继续编。
     */
    TEST_F(RecordingServiceFixture, DestructionWaitsForTheEncodeInFlight)
    {
        StubCaptureSource     source;
        GatedRecordingEncoder encoder;
        auto                  service = std::make_unique<RecordingService>(source, encoder);

        ASSERT_TRUE(static_cast<bool>(service->start(serviceConfig(OutputFormat::Mp4, false), 0)));
        spinFramesIn();
        ASSERT_TRUE(static_cast<bool>(service->stop()));
        ASSERT_TRUE(spinUntil([&] { return encoder.callCount() == 1; }));

        service.reset();
        EXPECT_TRUE(encoder.isFinished()) << "析构直接返回了：编码还在跑，编码器却已经没人管了";
    }
} // namespace
