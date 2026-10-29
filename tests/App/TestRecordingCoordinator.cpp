// 录屏协调器的两条直测：到上限自动停止时那句说明，与停止失败时危险提示的条数。
// 注入构造把真服务接上替身编码器，控制条的可见性因此可以在信号发出的那一刻直接读。
// 两处判据都落在「用户看到几条、看到时控制条还在不在」，不依赖真实抓屏与真实编码。

#include "App/RecordingCoordinator.h"

#include "Base/Paths.h"
#include "Core/Capture/IScreenCaptureSource.h"
#include "Core/Record/IRecordingEncoder.h"
#include "Core/Record/RecorderConfig.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"
#include "Gui/Controls/Toast.h"
#include "Gui/Resources/IconProvider.h"
#include "Platform/IPlatformIntegration.h"
#include "Plugins/Recording/RecordingService.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QSize>
#include <QString>

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using GyanisScribe::App::RecordingCoordinator;
using GyanisScribe::Base::CapturedImage;
using GyanisScribe::Base::Error;
using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Base::Expected;
using GyanisScribe::Core::IRecordingEncoder;
using GyanisScribe::Core::IScreenCaptureSource;
using GyanisScribe::Core::RecorderConfig;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Gui::IconProvider;
using GyanisScribe::Gui::ToastKind;
using GyanisScribe::Plugins::RecordingService;

namespace
{
    // argv 的缓冲要活过应用对象的整个生命期，所以放在文件作用域
    char  applicationArgument[]  = "TestRecordingCoordinator";
    char *applicationArguments[] = {applicationArgument, nullptr};

    std::unique_ptr<QApplication> application;

    /// 等成品或超时的上限：到上限仍不成立时用例判红，而不是挂住
    constexpr qint64 kWaitPatienceMs = 10'000;

    /// 起录后立刻停止到处理完事件的观察窗口：够一条危险提示走完全程
    constexpr qint64 kStopSettleMs = 200;

    /**
     * @brief 给一张 8×8 纯色帧的抓帧来源
     * @details 只要求会话能落帧：像素内容不参与本文件的判据。
     */
    class StubCaptureSource final : public IScreenCaptureSource
    {
    public:
        /**
         * @brief 抓一块屏
         * @param screenName 目标屏名，原样写进帧里
         * @return 8×8 的纯色帧
         */
        [[nodiscard]] Expected<CapturedImage> grabScreen(const QString &screenName) const override
        {
            return frame(screenName);
        }

        /**
         * @brief 抓一个窗口
         * @param windowHandle 不使用
         * @return 屏名固定为 screen0 的 8×8 纯色帧
         */
        [[nodiscard]] Expected<CapturedImage> grabWindow(const quintptr windowHandle) const override
        {
            static_cast<void>(windowHandle);
            return frame(QStringLiteral("screen0"));
        }

    private:
        /**
         * @brief 造一张纯色帧
         * @param screenName 来源屏名
         * @return 逻辑与物理尺寸一致的帧
         */
        [[nodiscard]] static CapturedImage frame(const QString &screenName)
        {
            QImage image{QSize{8, 8}, QImage::Format_ARGB32_Premultiplied};
            image.fill(QColor(32, 32, 32));
            return CapturedImage{image, QSize{8, 8}, 1.0, screenName};
        }
    };

    /**
     * @brief 立即返回成品的编码器
     * @details 调用次数与线程只作读数：本文件判的是界面侧提示，不判编码跑在哪条线程。
     */
    class InstantEncoder final : public IRecordingEncoder
    {
    public:
        /**
         * @brief 编一段成品
         * @param config 不使用
         * @param frameDirectory 帧目录，成品按它拼出路径
         * @param frameCount 不使用
         * @param recordedDurationMs 不使用
         * @param audioWav 不使用
         * @return 帧目录下的 product.mp4
         */
        [[nodiscard]] Expected<std::filesystem::path> encode(const RecorderConfig &config, const std::filesystem::path &frameDirectory, const int frameCount,
                                                             const qint64 recordedDurationMs, const std::filesystem::path &audioWav) override
        {
            static_cast<void>(config);
            static_cast<void>(frameCount);
            static_cast<void>(recordedDurationMs);
            static_cast<void>(audioWav);
            m_calls.fetch_add(1);
            m_thread.store(std::this_thread::get_id());
            return frameDirectory / "product.mp4";
        }

        /// 已经进来的调用次数
        [[nodiscard]] int callCount() const
        {
            return m_calls.load();
        }

        /// 最近一次编发生在哪条线程
        [[nodiscard]] std::thread::id encodeThread() const
        {
            return m_thread.load();
        }

    private:
        std::atomic<int>             m_calls{0}; ///< 调用次数
        std::atomic<std::thread::id> m_thread{}; ///< 最近一次编所在的线程
    };

    /**
     * @brief 不演任何平台能力的平台集成
     * @details 协调器只在录窗口那一档问前台窗口，本文件录的是整屏，替身一律回绝即可。
     */
    class StubPlatformIntegration final : public GyanisScribe::Platform::IPlatformIntegration
    {
    public:
        /**
         * @brief 本平台能力表
         * @return 全部为假
         */
        [[nodiscard]] GyanisScribe::Platform::PlatformCapabilities capabilities() const noexcept override
        {
            return {};
        }

        /**
         * @brief 平台显示名
         * @return 固定的替身名
         */
        [[nodiscard]] QString displayName() const override
        {
            return QStringLiteral("协调器替身");
        }

        /**
         * @brief 创建全局热键后端
         * @return 空，替身不演这一项
         */
        [[nodiscard]] std::unique_ptr<GyanisScribe::Platform::IHotkeyBackend> createHotkeyBackend() override
        {
            return nullptr;
        }

        /**
         * @brief 取当前前台窗口
         * @return PlatformUnsupported
         */
        [[nodiscard]] Expected<GyanisScribe::Platform::ForegroundWindow> foregroundWindow() const override
        {
            return std::unexpected(Error{ErrorCode::PlatformUnsupported, "协调器替身不演取前台窗口这一项"});
        }

        /**
         * @brief 本程序有没有登记开机启动
         * @return PlatformUnsupported
         */
        [[nodiscard]] Expected<bool> autostartEnabled() const override
        {
            return std::unexpected(Error{ErrorCode::PlatformUnsupported, "协调器替身不演开机启动这一项"});
        }

        /**
         * @brief 开/关开机启动
         * @param enabled 不使用
         * @return PlatformUnsupported
         */
        [[nodiscard]] Expected<void> setAutostart(const bool enabled) override
        {
            static_cast<void>(enabled);
            return std::unexpected(Error{ErrorCode::PlatformUnsupported, "协调器替身不演开机启动这一项"});
        }

        /**
         * @brief 桌面上看得见的顶层窗口
         * @return PlatformUnsupported
         */
        [[nodiscard]] Expected<std::vector<GyanisScribe::Platform::ScreenWindow>> screenWindows() const override
        {
            return std::unexpected(Error{ErrorCode::PlatformUnsupported, "协调器替身不演枚举窗口这一项"});
        }

        /**
         * @brief 光标处一摞界面元素
         * @param globalLogicalPoint 不使用
         * @param excludeWindowHandles 不使用
         * @return PlatformUnsupported
         */
        [[nodiscard]] Expected<std::vector<QRect>> elementChainAtPoint(const QPoint &globalLogicalPoint, const std::vector<quintptr> &excludeWindowHandles) const override
        {
            static_cast<void>(globalLogicalPoint);
            static_cast<void>(excludeWindowHandles);
            return std::unexpected(Error{ErrorCode::PlatformUnsupported, "协调器替身不演界面元素这一项"});
        }
    };

    /**
     * @brief 有界轮询等条件成立，其间把界面循环转起来
     * @param predicate 要等的条件
     * @return 成立返回 true；到上限仍不成立返回 false
     */
    template<typename Predicate>
    bool spinUntil(const Predicate &predicate)
    {
        QElapsedTimer wall;
        wall.start();
        while (wall.elapsed() < kWaitPatienceMs)
        {
            if (predicate())
            {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        return predicate();
    }

    /**
     * @brief 把界面循环转够一段观察窗口
     * @param milliseconds 要转的毫秒数
     */
    void spinFor(const qint64 milliseconds)
    {
        QElapsedTimer wall;
        wall.start();
        while (wall.elapsed() < milliseconds)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }

    /**
     * @brief 把录屏屏名钉成固定值的协调器
     * @details 离屏平台给不出屏名，而录屏参数必须带屏名；覆写这一格让 makeConfig 的其余部分照常跑，
     *          用例因此不必占一张交互桌面。
     */
    class TestRecordingCoordinator final : public RecordingCoordinator
    {
    public:
        /**
         * @brief 构造：录屏服务由用例交进来
         * @param captureSource 抓帧来源，交给录屏服务
         * @param integration 平台能力
         * @param settings 录制参数来源
         * @param icons 控制条图标出口
         * @param service 录屏服务本体，所有权转给协调器
         * @param parent Qt 父对象
         */
        TestRecordingCoordinator(GyanisScribe::Core::IScreenCaptureSource &captureSource, GyanisScribe::Platform::IPlatformIntegration &integration,
                                 GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Gui::IconProvider &icons, std::unique_ptr<RecordingService> service,
                                 QObject *parent = nullptr) : RecordingCoordinator(captureSource, integration, settings, icons, std::move(service), parent)
        {
        }

    protected:
        /**
         * @brief 录哪块屏
         * @return 固定的替身屏名
         * @details 重写 RecordingCoordinator::recordingScreenName()：离屏平台没有屏名，返回固定值让参数校验通过。
         */
        [[nodiscard]] QString recordingScreenName() const override
        {
            return QStringLiteral("FakeScreen");
        }
    };

    /**
     * @brief 直测装置：设置、抓帧、编码与平台全部换成用例自己的那一份
     * @details 协调器由注入构造接上替身编码器的真服务；控制条的显示与收起因此是可观测的。
     */
    class RecordingCoordinatorFixture : public ::testing::Test
    {
    protected:
        /**
         * @brief 起应用：协调器拼配置与摆控制条各要一块屏
         * @details 离屏平台给不出屏名，拼配置那一格由 TestRecordingCoordinator 覆写的 recordingScreenName() 补上。
         */
        static void SetUpTestSuite()
        {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            // 帧目录按应用名分开：并行跑别的测试进程时不共用缓存里那棵 Recording 树
            QCoreApplication::setApplicationName(QStringLiteral("TestRecordingCoordinator-%1").arg(QCoreApplication::applicationPid()));
            int argumentCount = 1;
            application       = std::make_unique<QApplication>(argumentCount, applicationArguments);
        }

        /// 收掉应用
        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
            m_root           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-RecordingCoordinator.{}_{}", info->test_suite_name(), info->name());
            std::error_code ignored;
            std::filesystem::remove_all(m_root, ignored);
            std::filesystem::create_directories(m_root);

            m_settings = std::make_unique<SettingsStore>(m_root / "settings.ini");
            ASSERT_TRUE(m_settings->setValue(GyanisScribe::Core::kPathRecording, GyanisScribe::Base::Paths::toDisplayText(m_root / "out")).has_value());
            ASSERT_TRUE(m_settings->setValue(GyanisScribe::Core::kRecordCountdownSeconds, 0).has_value());
            ASSERT_TRUE(m_settings->setValue(GyanisScribe::Core::kRecordMaximumDuration, 1).has_value());
            ASSERT_TRUE(m_settings->setValue(GyanisScribe::Core::kRecordDefaultFrameRate, 15).has_value());

            m_icons       = std::make_unique<IconProvider>();
            auto service  = std::make_unique<RecordingService>(m_source, m_encoder, m_platform);
            m_coordinator = std::make_unique<TestRecordingCoordinator>(m_source, m_platform, *m_settings, *m_icons, std::move(service));
        }

        void TearDown() override
        {
            // 协调器先走：它持有服务，服务析构会等手上的编码
            m_coordinator.reset();
            m_icons.reset();
            m_settings.reset();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

            std::error_code ignored;
            std::filesystem::remove_all(m_root, ignored);
            std::filesystem::remove_all(GyanisScribe::Base::Paths::cacheDirectory() / "Recording", ignored);
        }

        std::vector<QString>                      m_messages;                                     ///< 收到的全部提示正文，按发出顺序
        std::vector<QString>                      m_dangerMessages;                               ///< 收到的那几条危险提示
        int                                       m_announcements            = 0;                 ///< 「已到最长时长」那条说明的条数
        bool                                      m_hudVisibleAtAnnouncement = false;             ///< 说明发出那一刻控制条是否可见
        bool                                      m_finished                 = false;             ///< 成品信号到过没有
        bool                                      m_finishedAtAnnouncement   = false;             ///< 说明发出那一刻成品是否已经回来
        std::filesystem::path                     m_root;                                         ///< 这一格用例自己的临时目录
        StubCaptureSource                         m_source;                                       ///< 抓帧来源
        std::shared_ptr<InstantEncoder>           m_encoder = std::make_shared<InstantEncoder>(); ///< 立即返回成品的编码器
        StubPlatformIntegration                   m_platform;                                     ///< 平台集成
        std::unique_ptr<SettingsStore>            m_settings;                                     ///< 参数来源
        std::unique_ptr<IconProvider>             m_icons;                                        ///< 控制条图标出口
        std::unique_ptr<TestRecordingCoordinator> m_coordinator;                                  ///< 被测对象
    };
} // namespace

/**
 * @brief 钉住自动停止时先收控制条、再说明一句，而且只说明一次
 * @details 服务在停止成功那一刻发 autoStopStarted，不等下一次每秒节拍；这里在说明发出的那一刻读控制条，
 *          判的是用户先看到控制条收掉，再看到说明。
 */
TEST_F(RecordingCoordinatorFixture, AutoStopAnnouncesBeforeTheEncodeFinishes)
{
    QObject::connect(m_coordinator.get(), &RecordingCoordinator::messageForUser,
                     [this](const QString &message, const ToastKind)
                     {
                         m_messages.push_back(message);
                         if (message.contains(QStringLiteral("已到最长时长，正在编成品")))
                         {
                             ++m_announcements;
                             m_hudVisibleAtAnnouncement = m_coordinator->isHudVisible();
                             m_finishedAtAnnouncement   = m_finished;
                         }
                     });
    QObject::connect(m_coordinator.get(), &RecordingCoordinator::finished, [this](const QString &, const QSize &) { m_finished = true; });

    m_coordinator->toggle();
    ASSERT_TRUE(m_coordinator->isHudVisible()) << "起录之后控制条没有出现";

    // 上限 1 秒：等成品而不是等固定时长，机器慢一点也只多转几次事件循环
    ASSERT_TRUE(spinUntil([this] { return m_finished; })) << "十秒内没等到成品";

    EXPECT_EQ(m_announcements, 1) << "说明 " << m_announcements << " 条，应恰好 1 条";
    EXPECT_FALSE(m_finishedAtAnnouncement) << "说明在成品之后才发，自动停止的时序判据没有成立";
    EXPECT_FALSE(m_hudVisibleAtAnnouncement) << "说明发出时控制条还挂着：用户会先看到一句与画面对不上的提示";
    EXPECT_FALSE(m_coordinator->isHudVisible()) << "成品回来之后控制条还挂着";
}

/**
 * @brief 钉住停止失败只有一条危险提示
 * @details 首帧还没落下就停止，服务的 stop() 会自己发 failed；协调器若在 toggle() 的失败分支补一条，
 *          用户会同时看到两条同一件事的危险提示。
 */
TEST_F(RecordingCoordinatorFixture, StopBeforeTheFirstFrameReportsOneDangerToast)
{
    QObject::connect(m_coordinator.get(), &RecordingCoordinator::messageForUser,
                     [this](const QString &message, const ToastKind kind)
                     {
                         if (kind == ToastKind::Danger)
                         {
                             m_dangerMessages.push_back(message);
                         }
                     });

    // 两下之间不转事件循环：帧率 15 的首帧还没落，这一轮必然封不了盘
    m_coordinator->toggle();
    m_coordinator->toggle();

    spinFor(kStopSettleMs);

    ASSERT_EQ(m_dangerMessages.size(), 1U) << "危险提示 " << m_dangerMessages.size() << " 条，应恰好 1 条";
    EXPECT_NE(m_dangerMessages.front().toStdString().find("录屏中止"), std::string::npos) << m_dangerMessages.front().toStdString();
}
