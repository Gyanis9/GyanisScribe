// RecordingService 的直测。抓屏与真编码留给桌面会话，这里钉的是这一层自己的三个决定：
// 哪种输出该跳过取声（音频档位）、stop() 之后编码是不是真的不在界面线程上等（线程交接）、
// 以及临时帧的处置（编码失败留着好重编码，取消则整棵删掉且一次编码都不叫）。
// 假编码器把 encode() 停在门闩上，「调用已返回而活还没干完」因此成了可断言的事。

#include "Capture/IScreenCaptureSource.h"
#include "Core/Paths.h"
#include "Platform/IPlatformIntegration.h"
#include "Core/Types.h"
#include "Record/FfmpegCommand.h"
#include "Record/FfmpegRecordingEncoder.h"
#include "Record/RecorderConfig.h"
#include "Record/RecordingService.h"

#include <QApplication>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QEventLoop>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStandardPaths>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <semaphore>
#include <string>
#include <algorithm>
#include <ranges>
#include <thread>
#include <vector>

using GyanisScribe::Capture::IScreenCaptureSource;
using GyanisScribe::Core::CapturedImage;
using GyanisScribe::Core::Error;
using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Core::Expected;
using GyanisScribe::Record::AudioCaptureOutcome;
using GyanisScribe::Record::IRecordingEncoder;
using GyanisScribe::Record::locateFfmpeg;
using GyanisScribe::Record::OutputFormat;
using GyanisScribe::Record::RecorderConfig;
using GyanisScribe::Record::RecordingService;
using GyanisScribe::Record::RecordingState;
using GyanisScribe::Record::RecordMode;

namespace
{
    // argv 的缓冲要活过应用对象的整个生命期，所以放在文件作用域
    char  applicationArgument[]  = "TestRecordingService";
    char *applicationArguments[] = {applicationArgument, nullptr};

    std::unique_ptr<QApplication> application;

    /// 用例放开门闩之前，假编码器最多停这么久：同步实现也能在有限时间内把控制权交回用例
    constexpr std::chrono::milliseconds kGatePatience{2000};

    /// 等条件成立的上限：这一档只是「卡住了要能在有限时间内报出来」的兜底，不是快慢判据。
    /// 线程池的排班由这台机器上当时刻多忙决定，把预算掐在 2 秒会把「机器正忙」写成用例失败
    /// （实测：并行跑别的构建时，一次编码在 2 秒内没被排上，断言就红了）
    constexpr std::chrono::milliseconds kWaitPatience{10000};

    /// 开始一轮之后要转多久界面循环才攒到帧：15 帧率下 1.3 秒能落十几帧，够 stop() 收尾不报「一帧都没有」
    constexpr std::chrono::milliseconds kFrameSpin{1300};

    /// 底色取中性灰：高亮圈是蓝系主色，只有它会让蓝色分量压过红色
    const QColor kFrameBackground{32, 32, 32};

    /**
     * @brief 读回帧目录里第一份文件的像素
     * @return 读回来的图；一份都没有时返回空图
     * @details 落盘的帧就是「这一帧用户会看到什么」的唯一凭据，光标高亮的判据只能落在它上面。
     */
    [[nodiscard]] QImage readFirstFrame()
    {
        const auto      root = GyanisScribe::Core::Paths::cacheDirectory() / "Recording";
        std::error_code ignored;
        if (!std::filesystem::exists(root, ignored))
        {
            return {};
        }

        std::vector<std::filesystem::path> files;
        for (const auto &directory: std::filesystem::directory_iterator{root})
        {
            if (!directory.is_directory())
            {
                continue;
            }
            for (const auto &entry: std::filesystem::directory_iterator{directory.path()})
            {
                if (entry.is_regular_file())
                {
                    files.push_back(entry.path());
                }
            }
        }
        std::ranges::sort(files);
        if (files.empty())
        {
            return {};
        }
        return QImage{QString::fromStdWString(files.front().wstring())};
    }

    /// 改动像素的统计：个数与质心。质心就是「圈心落在第几格」的读数，环形对称因此抗抗锯齿的毛边
    struct PaintedArea
    {
        int    count        = 0;     ///< 与底色不同的像素个数
        double centroidX    = 0.0;   ///< 横向质心，物理像素
        double centroidY    = 0.0;   ///< 纵向质心，物理像素
        bool   hasAccentHue = false; ///< 是否出现「蓝压过红」的一格，即主色那一圈
    };

    /// 扫一遍帧，把与底色不同的像素计进 PaintedArea
    [[nodiscard]] PaintedArea scanFrame(const QImage &image)
    {
        PaintedArea stats;
        double      sumX = 0.0;
        double      sumY = 0.0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QRgb pixel = image.pixel(x, y);
                if (qRed(pixel) == kFrameBackground.red() && qGreen(pixel) == kFrameBackground.green() && qBlue(pixel) == kFrameBackground.blue())
                {
                    continue;
                }
                ++stats.count;
                sumX += x;
                sumY += y;
                if (qBlue(pixel) > qRed(pixel) && qBlue(pixel) > qGreen(pixel))
                {
                    stats.hasAccentHue = true;
                }
            }
        }
        if (stats.count > 0)
        {
            stats.centroidX = sumX / static_cast<double>(stats.count);
            stats.centroidY = sumY / static_cast<double>(stats.count);
        }
        return stats;
    }

    /**
     * @brief 只演「屏上有这几扇窗」的替身
     * @details 替身纪律与热键那一份相同：能力位的 windowEnumeration 与 screenWindows() 的交回必须同解，
     *          否则「说不行却递出一个句柄」这种自相矛盾的形状会在用例里被当成正常输入。
     */
    class StubPlatformIntegration final : public GyanisScribe::Platform::IPlatformIntegration
    {
    public:
        /**
         * @brief 建替身
         * @param windows 要交出去的窗口清单
         * @param enumerates true 表示能力表里 windowEnumeration 置位；为假时一律回绝
         */
        explicit StubPlatformIntegration(std::vector<GyanisScribe::Platform::ScreenWindow> windows = {}, const bool enumerates = true) :
            m_windows(std::move(windows)), m_enumerates(enumerates)
        {
        }

        [[nodiscard]] GyanisScribe::Platform::PlatformCapabilities capabilities() const noexcept override
        {
            GyanisScribe::Platform::PlatformCapabilities table;
            table.windowEnumeration = m_enumerates;
            return table;
        }

        [[nodiscard]] QString displayName() const override
        {
            return QStringLiteral("窗口清单替身");
        }

        [[nodiscard]] std::unique_ptr<GyanisScribe::Platform::IHotkeyBackend> createHotkeyBackend() override
        {
            return nullptr;
        }

        [[nodiscard]] Expected<GyanisScribe::Platform::ForegroundWindow> foregroundWindow() const override
        {
            return std::unexpected(Error{ErrorCode::PlatformUnsupported, "替身不演取前台窗口这一项"});
        }

        [[nodiscard]] Expected<std::vector<GyanisScribe::Platform::ScreenWindow>> screenWindows() const override
        {
            if (!m_enumerates)
            {
                return std::unexpected(Error{ErrorCode::PlatformUnsupported, "本平台列不出窗口"});
            }
            return m_windows;
        }

    private:
        std::vector<GyanisScribe::Platform::ScreenWindow> m_windows;           ///< 摆好的那份清单
        bool                                              m_enumerates = true; ///< 能力位与交回内容要同解
    };

    /**
     * @brief 给一张 80×60 纯色帧的来源
     * @details 高亮圈半径 16，原来那张 8×8 放不下整个圈，判据就没法落在像素位置上。
     */
    class BlankFrameSource final : public IScreenCaptureSource
    {
    public:
        [[nodiscard]] Expected<CapturedImage> grabScreen(const QString &screenName) const override
        {
            return frame(screenName);
        }

        [[nodiscard]] Expected<CapturedImage> grabWindow(const quintptr) const override
        {
            return frame(QStringLiteral("screen0"));
        }

    private:
        [[nodiscard]] static CapturedImage frame(const QString &screenName)
        {
            QImage image{QSize{80, 60}, QImage::Format_ARGB32_Premultiplied};
            image.fill(kFrameBackground);
            return CapturedImage{image, QSize{80, 60}, 1.0, screenName};
        }
    };

    /**
     * @brief 把光标钉在已知点上的服务
     * @details 真实鼠标随时会动，「圈该落在帧里的第几格」这条判据因此需要一个不会动的光标。
     */
    class FixedCursorRecordingService final : public RecordingService
    {
    public:
        FixedCursorRecordingService(IScreenCaptureSource &source, IRecordingEncoder &encoder, GyanisScribe::Platform::IPlatformIntegration &integration, const QPoint &cursor) :
            RecordingService(source, encoder, integration), m_cursor(cursor)
        {
        }

    protected:
        [[nodiscard]] QPoint cursorGlobalPosition() const override
        {
            return m_cursor;
        }

    private:
        QPoint m_cursor; ///< 钉住的光标全局逻辑位置
    };

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
        std::thread::id       thread;                 ///< 编发生在哪条线程
        std::filesystem::path frameDirectory;         ///< 交给它的那一份帧目录（快照，不是服务的成员）
        int                   frameCount         = 0; ///< 交过去的帧数
        qint64                recordedDurationMs = 0; ///< 交过去的这一轮时长，毫秒（成品时长就按它与帧数铺开）
    };

    /**
     * @brief 停在门闩上的假编码器
     * @details 观测点只有原子量与加锁快照：测试线程与工作线程各读各的，不靠调度先后。
     */
    class GatedRecordingEncoder final : public IRecordingEncoder
    {
    public:
        [[nodiscard]] Expected<std::filesystem::path> encode(const RecorderConfig &, const std::filesystem::path &frameDirectory, const int frameCount,
                                                             const qint64 recordedDurationMs, const std::filesystem::path &) override
        {
            {
                const std::lock_guard guard{m_mutex};
                m_calls.push_back(EncodeCall{std::this_thread::get_id(), frameDirectory, frameCount, recordedDurationMs});
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
     * @brief 这一轮落在缓存里的帧目录
     * @return 唯一的子目录路径；一个都没有时返回空路径
     * @details 整组用例按 RESOURCE_LOCK 串行（Recording 那棵树是共用的），因此「恰好一个」这条前提成立。
     */
    [[nodiscard]] std::filesystem::path onlyFrameDirectory()
    {
        const auto root = GyanisScribe::Core::Paths::cacheDirectory() / "Recording";
        // 环里带条件才返回：无条件 return 的循环会让 MSVC 在 Debug 档把循环之后那句当成不可达代码（C4702），
        // 而 /W4 /WX 把那条警告直接变成构建失败
        for (const auto &entry: std::filesystem::directory_iterator{root})
        {
            if (entry.is_directory())
            {
                return entry.path();
            }
        }
        return {};
    }

    /// 目录里有几份文件：取消该把整棵树带走，一帧都不留
    [[nodiscard]] std::size_t countFilesIn(const std::filesystem::path &directory)
    {
        std::error_code ignored;
        if (directory.empty() || !std::filesystem::exists(directory, ignored))
        {
            return 0;
        }
        std::size_t count = 0;
        for (const auto &entry: std::filesystem::directory_iterator{directory})
        {
            count += entry.is_regular_file() ? 1U : 0U;
        }
        return count;
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
            application       = std::make_unique<QApplication>(argumentCount, applicationArguments);
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
        StubCaptureSource       source;
        GatedRecordingEncoder   encoder;
        StubPlatformIntegration platform;
        RecordingService        service{source, encoder, platform};

        const auto started = service.start(serviceConfig(OutputFormat::Gif, true), 0);
        ASSERT_TRUE(static_cast<bool>(started)) << started.error().message;
        EXPECT_EQ(service.audioOutcome(), AudioCaptureOutcome::NotRequested) << "GIF 带不了声音，这一轮不该把取声算成在跑";
    }

    /**
     * @brief 配对的那一条：MP4 选了取声仍然真的要去起 ffmpeg
     * @details 上一那条容易被写成「什么格式都跳过取声」——那种做法会把 MP4 的音轨一起关掉。
     *          这里在没有 ffmpeg 的现场断「起不动就拒绝开始」，跳过用在了对的档位上才算得住。
     *          前置条件要按产品自己那条查找顺序判（程序目录里随包那份 → PATH），
     *          只看 PATH 会漏掉「可执行体旁边就躺着一份」这种现场——随包分发之后那是常态。
     */
    TEST_F(RecordingServiceFixture, Mp4WithAudioRequestedStillWantsFfmpeg)
    {
        if (locateFfmpeg())
        {
            GTEST_SKIP() << "这份现场找得到 ffmpeg，这一条要的是「没有它」的那一种情况";
        }

        StubCaptureSource       source;
        GatedRecordingEncoder   encoder;
        StubPlatformIntegration platform;
        RecordingService        service{source, encoder, platform};

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
        StubCaptureSource       source;
        GatedRecordingEncoder   encoder;
        StubPlatformIntegration platform;
        RecordingService        service{source, encoder, platform};

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
        StubCaptureSource       source;
        GatedRecordingEncoder   encoder;
        StubPlatformIntegration platform;
        encoder.failNext();
        RecordingService service{source, encoder, platform};

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
        StubCaptureSource       source;
        GatedRecordingEncoder   encoder;
        StubPlatformIntegration platform;
        RecordingService        service{source, encoder, platform};

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
        StubCaptureSource       source;
        GatedRecordingEncoder   encoder;
        StubPlatformIntegration platform;
        auto                    service = std::make_unique<RecordingService>(source, encoder, platform);

        ASSERT_TRUE(static_cast<bool>(service->start(serviceConfig(OutputFormat::Mp4, false), 0)));
        spinFramesIn();
        ASSERT_TRUE(static_cast<bool>(service->stop()));
        ASSERT_TRUE(spinUntil([&] { return encoder.callCount() == 1; }));

        service.reset();
        EXPECT_TRUE(encoder.isFinished()) << "析构直接返回了：编码还在跑，编码器却已经没人管了";
    }

    /**
     * @brief 钉住「取消」既不出成品也不留帧，而且把服务放回能开下一轮的位置
     * @details 取消的理由是「这一段白录了」：把 cancel() 做成 stop() 的别名，这三条都会反过来——
     *          编码照跑、成品照出、下一轮还会被 Busy 拦住。
     */
    TEST_F(RecordingServiceFixture, CancelDropsTheFramesAndNeverAsksTheEncoder)
    {
        StubCaptureSource       source;
        GatedRecordingEncoder   encoder;
        StubPlatformIntegration platform;
        RecordingService        service{source, encoder, platform};

        ASSERT_TRUE(static_cast<bool>(service.start(serviceConfig(OutputFormat::Mp4, false), 0)));
        spinFramesIn();
        const auto frameDirectory = onlyFrameDirectory();
        ASSERT_GT(countFilesIn(frameDirectory), 0U) << "这一轮一帧都没落，取消就没有可删的东西";

        int  abortedCount = 0;
        bool gotFinished  = false;
        QObject::connect(&service, &RecordingService::aborted, [&abortedCount] { ++abortedCount; });
        QObject::connect(&service, &RecordingService::finished, [&gotFinished](const QString &) { gotFinished = true; });

        const auto cancelled = service.cancel();
        ASSERT_TRUE(static_cast<bool>(cancelled)) << cancelled.error().message;
        EXPECT_EQ(abortedCount, 1) << "没发 aborted：控制条与托盘会停在「正在录制」那句假话上";
        EXPECT_FALSE(gotFinished) << "取消还送出了成品";
        EXPECT_EQ(encoder.callCount(), 0) << "取消还去叫了编码器：扔掉的一段不该再花一次 CPU";
        EXPECT_FALSE(std::filesystem::exists(frameDirectory)) << "帧目录还留着，取消与停止并保存在磁盘上就没分别了";
        EXPECT_EQ(service.state(), RecordingState::Idle) << "状态没归位，下一轮会被「上一轮还没收尾」拦住";

        EXPECT_TRUE(static_cast<bool>(service.start(serviceConfig(OutputFormat::Mp4, false), 0))) << "取消之后开不了新一轮";
    }

    /**
     * @brief 拒绝面：没在录的时候取消要报清楚，也不许发 aborted
     * @details 空操作静默通过的话，用户按下取消而什么都没发生，只会以为「取消过了」。
     */
    TEST_F(RecordingServiceFixture, CancelWithoutAnActiveRoundIsRefused)
    {
        StubCaptureSource       source;
        GatedRecordingEncoder   encoder;
        StubPlatformIntegration platform;
        RecordingService        service{source, encoder, platform};

        int abortedCount = 0;
        QObject::connect(&service, &RecordingService::aborted, [&abortedCount] { ++abortedCount; });

        const auto cancelled = service.cancel();
        ASSERT_FALSE(static_cast<bool>(cancelled)) << "没在录也报了取消成功";
        EXPECT_NE(cancelled.error().message.find("没有正在进行的录屏"), std::string::npos) << cancelled.error().message;
        EXPECT_EQ(abortedCount, 0) << "拒掉了却还是发了 aborted";
    }

    /**
     * @brief 钉住「stop 之后再取消」被拒，而且不动后台编码正在读的那一份帧
     * @details 取消删的目录正是那一次编码的输入：放进那一档等于把一轮「已经要出成品」的录屏做成编码失败。
     */
    TEST_F(RecordingServiceFixture, CancelAfterStopIsRefusedAndLeavesTheEncodeInputAlone)
    {
        StubCaptureSource       source;
        GatedRecordingEncoder   encoder;
        StubPlatformIntegration platform;
        RecordingService        service{source, encoder, platform};

        ASSERT_TRUE(static_cast<bool>(service.start(serviceConfig(OutputFormat::Mp4, false), 0)));
        spinFramesIn();
        ASSERT_TRUE(static_cast<bool>(service.stop()));
        ASSERT_TRUE(spinUntil([&] { return encoder.callCount() == 1; }));
        const auto frameDirectory = encoder.callAt(0).frameDirectory;
        ASSERT_TRUE(std::filesystem::exists(frameDirectory));

        int abortedCount = 0;
        QObject::connect(&service, &RecordingService::aborted, [&abortedCount] { ++abortedCount; });

        const auto cancelled = service.cancel();
        ASSERT_FALSE(static_cast<bool>(cancelled)) << "取消接了正在编的那一轮：成品会以「编码失败」的面目回来";
        EXPECT_NE(cancelled.error().message.find("已经停止并在编"), std::string::npos) << cancelled.error().message;
        EXPECT_TRUE(std::filesystem::exists(frameDirectory)) << "目录被取消删掉了，那正是后台编码在读的输入";
        EXPECT_EQ(abortedCount, 0) << "拒了却还是发了 aborted";

        QString product;
        QObject::connect(&service, &RecordingService::finished, [&product](const QString &path) { product = path; });
        encoder.release();
        EXPECT_TRUE(spinUntil([&] { return !product.isEmpty(); })) << "取消被拒之后这一轮该照常出成品";
    }

    /**
     * @brief 钉住「只认客户区读数」这条选择
     * @details 三种形状都要钉住：句柄对上就交回客户区左上角；只有整窗读数时交回空（不许拿整窗顶替）；
     *          清单里没有这扇窗也交回空。resolver 一旦「退而用整窗」，圈就会画到标题栏上而看起来正常。
     */
    TEST_F(RecordingServiceFixture, WindowFrameOriginAcceptsOnlyTheClientRect)
    {
        GyanisScribe::Platform::ScreenWindow withClient;
        withClient.handle              = 7;
        withClient.logicalGlobal       = QRect{10, 20, 90, 80};
        withClient.clientLogicalGlobal = QRect{30, 55, 80, 60};

        GyanisScribe::Platform::ScreenWindow withoutClient;
        withoutClient.handle        = 8;
        withoutClient.logicalGlobal = QRect{100, 100, 90, 80};

        const std::vector<GyanisScribe::Platform::ScreenWindow> windows{withClient, withoutClient};

        const auto origin = GyanisScribe::Record::windowFrameOrigin(windows, 7);
        ASSERT_TRUE(origin.has_value());
        EXPECT_EQ(*origin, QPoint(30, 55)) << "对上了句柄就要交回客户区的左上角";

        EXPECT_FALSE(GyanisScribe::Record::windowFrameOrigin(windows, 8).has_value()) << "只有整窗读数时不许拿它顶替客户区";
        EXPECT_FALSE(GyanisScribe::Record::windowFrameOrigin(windows, 9).has_value()) << "清单里没这扇窗却编出一个位置";
        EXPECT_FALSE(GyanisScribe::Record::windowFrameOrigin({}, 7).has_value());
        EXPECT_FALSE(GyanisScribe::Record::windowFrameOrigin(windows, 0).has_value()) << "句柄为 0 是「没指定窗口」，不该去匹配任何一条";
    }

    /**
     * @brief 钉住窗口模式真的按平台给的客户区位置画出高亮圈
     * @details 改之前这一档只会记一句「圈不出光标」。替身交的客户区左上角在 (100,50)，光标钉在
     *          (130,80) → 圈心必须落在帧内 (30,30)；拿整窗那份 (90,20) 算就会落到 (40,60)。
     */
    TEST_F(RecordingServiceFixture, WindowModePaintsHighlightWhereTheClientRectSays)
    {
        BlankFrameSource                     source;
        GatedRecordingEncoder                encoder;
        GyanisScribe::Platform::ScreenWindow target;
        target.handle              = 7;
        target.logicalGlobal       = QRect{90, 20, 90, 80};
        target.clientLogicalGlobal = QRect{100, 50, 80, 60};
        StubPlatformIntegration     platform{{target}};
        FixedCursorRecordingService service{source, encoder, platform, QPoint{130, 80}};

        auto config            = serviceConfig(OutputFormat::Mp4, false);
        config.mode            = RecordMode::Window;
        config.windowHandle    = 7;
        config.cursorHighlight = true;
        ASSERT_TRUE(static_cast<bool>(service.start(config, 0)));
        spinFramesIn();

        const auto painted = scanFrame(readFirstFrame());
        ASSERT_GT(painted.count, 0) << "帧里没有高亮圈：窗口模式没把平台给的客户区读数接上";
        EXPECT_TRUE(painted.hasAccentHue) << "改动里没有主色那一圈";
        EXPECT_NEAR(painted.centroidX, 30.0, 3.0) << "圈心横坐标不对，多半用错了窗口的哪一份读数";
        EXPECT_NEAR(painted.centroidY, 30.0, 3.0) << "圈心纵坐标不对，多半用错了窗口的哪一份读数";

        ASSERT_TRUE(static_cast<bool>(service.cancel()));
    }

    /**
     * @brief 钉住「平台给不出客户区读数」时窗口模式保持不画，而不是画个错位的
     * @details 这是唯一能钉住「宁可不画」那条注记的地方：替身只交整窗读数，实现若退去用它，
     *          帧里就会凭空多出一圈——判据正是「一个像素都不该变」。
     */
    TEST_F(RecordingServiceFixture, WindowModeLeavesFrameCleanWhenThePlatformGivesNoClientRect)
    {
        BlankFrameSource                     source;
        GatedRecordingEncoder                encoder;
        GyanisScribe::Platform::ScreenWindow target;
        target.handle = 7;
        // 整窗那份刻意摆在「光标会落在帧内 (40,20)」的位置：实现一旦退去用它，那一圈就会凭空出现
        target.logicalGlobal = QRect{90, 60, 90, 80};
        StubPlatformIntegration     platform{{target}};
        FixedCursorRecordingService service{source, encoder, platform, QPoint{130, 80}};

        auto config            = serviceConfig(OutputFormat::Mp4, false);
        config.mode            = RecordMode::Window;
        config.windowHandle    = 7;
        config.cursorHighlight = true;
        ASSERT_TRUE(static_cast<bool>(service.start(config, 0)));
        spinFramesIn();

        const auto painted = scanFrame(readFirstFrame());
        EXPECT_EQ(painted.count, 0) << "没有客户区读数却仍然画了圈：那就是把整窗左上角当成了帧原点";

        ASSERT_TRUE(static_cast<bool>(service.cancel()));
    }
    /**
     * @brief 钉住「开始那一刻就把成品目录建出来」
     * @details 成品目录由编码那一步写，而编码在几十秒之后：那时才报「写不进去」等于把整轮录制扔掉。
     *          RecorderConfig 的校验一律写在开始之前判，目录这一项也得在同一处判掉。
     */
    TEST_F(RecordingServiceFixture, StartCreatesTheOutputDirectorySoTheEncodeHasSomewhereToWrite)
    {
        StubCaptureSource       source;
        GatedRecordingEncoder   encoder;
        StubPlatformIntegration platform;
        RecordingService        service{source, encoder, platform};

        const auto      target = std::filesystem::temp_directory_path() / "GyanisScribeFreshOutputDir";
        std::error_code ignored;
        std::filesystem::remove_all(target, ignored);
        ASSERT_FALSE(std::filesystem::exists(target));

        auto config            = serviceConfig(OutputFormat::Mp4, false);
        config.outputDirectory = target;

        const auto started = service.start(config, 0);
        ASSERT_TRUE(static_cast<bool>(started)) << started.error().message;
        EXPECT_TRUE(std::filesystem::exists(target)) << "开始录屏没建成品目录：几十秒后编码才会报写不进去，那一轮的帧白录了";

        ASSERT_TRUE(static_cast<bool>(service.cancel()));
        std::filesystem::remove_all(target, ignored);
    }

    /**
     * @brief 配对的那一条：成品路径被一个普通文件占住时，开始就地拒绝
     * @details 只补「建目录」不补拒绝，就会把这条路径当成可用而一路录到底；
     *          拒绝必须落在开始这一步，且不留下临时帧目录。
     */
    TEST_F(RecordingServiceFixture, StartRefusesWhenTheOutputPathIsNotADirectory)
    {
        StubCaptureSource       source;
        GatedRecordingEncoder   encoder;
        StubPlatformIntegration platform;
        RecordingService        service{source, encoder, platform};

        const auto      blocker = std::filesystem::temp_directory_path() / "GyanisScribeOutputBlocker";
        std::error_code ignored;
        std::filesystem::remove(blocker, ignored);
        {
            std::ofstream file(blocker, std::ios::binary);
            file << "占位";
        }

        auto config            = serviceConfig(OutputFormat::Mp4, false);
        config.outputDirectory = blocker;

        const auto started = service.start(config, 0);
        ASSERT_FALSE(static_cast<bool>(started)) << "成品路径是个文件却仍然开始录：这一轮到最后一步才失败";
        EXPECT_EQ(started.error().code, GyanisScribe::Core::ErrorCode::FileWriteFailed);
        EXPECT_EQ(service.state(), RecordingState::Idle) << "拒绝之后状态没回到空闲，下一轮会被「上一轮还没收尾」拦住";

        std::filesystem::remove(blocker, ignored);
    }

    /**
     * @brief 钉住 stop() 把「这一轮的帧数与时长」成对交给编码器
     * @details 成品时长按帧数除以这个时长铺开：只交帧数、让编码按目标帧率铺，
     *          机器跟不上时就会把六秒编成两秒多（本机实测）。
     */
    TEST_F(RecordingServiceFixture, StopHandsTheEncoderThisRoundsFramesAndDuration)
    {
        StubCaptureSource       source;
        GatedRecordingEncoder   encoder;
        StubPlatformIntegration platform;
        RecordingService        service{source, encoder, platform};

        QElapsedTimer wall;
        wall.start();
        ASSERT_TRUE(static_cast<bool>(service.start(serviceConfig(OutputFormat::Mp4, false), 0)));
        spinFramesIn();

        ASSERT_TRUE(static_cast<bool>(service.stop()));
        ASSERT_TRUE(spinUntil([&] { return encoder.callCount() == 1; }));

        const auto call = encoder.callAt(0);
        EXPECT_GT(call.frameCount, 0) << "一帧都没交出去，成品时长也就没有可对齐的分子";
        EXPECT_GT(call.recordedDurationMs, 0) << "时长没交出去：编码只能按目标帧率铺，短了的帧序列会被编成快放";
        // 这两条一起排掉了两种错接：拿配置里的时长上限（60 秒）或拿墙钟整段（含倒计时）当这一轮时长
        EXPECT_LE(call.recordedDurationMs, wall.elapsed()) << "交出去的时长比这一轮从开始到停止还长";

        encoder.release();
    }
} // namespace
