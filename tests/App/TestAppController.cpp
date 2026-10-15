// 「把各模块接起来」那一层的直测：延时截图的路由与成品进历史。
// 这一段此前一个实例都建不出来（构造函数自己造设置、抓帧、托盘这些真模块，没有注入口），
// 「截取当前窗口绕过延时」就是这么漏下来的；这一批给它一套与 TestModuleFlow 同形状的替身，
// 从 AppControllerParts 注进去，把「谁在什么时刻被叫到、成品记在哪一份清单上」钉住。

#include "App/AppController.h"

#include "App/CaptureIntent.h"
#include "Capture/IScreenCaptureSource.h"
#include "Base/Paths.h"
#include "Base/Types.h"
#include "History/HistoryRules.h"
#include "History/HistoryStore.h"
#include "History/HistoryTypes.h"
#include "Platform/IPlatformIntegration.h"
#include "Platform/PlatformCapabilities.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QSize>
#include <QString>
#include <QVector>

#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace GyanisScribe
{
    namespace
    {
        int                           argumentCount = 1;
        std::unique_ptr<QApplication> application;

        /// argv 缓冲要有静态存储期：QApplication 只存指针不拷贝
        inline static char argumentZero[] = "TestAppController";
        char              *arguments[]    = {argumentZero, nullptr};

        constexpr quintptr kForegroundWindowHandle = 0x51CE; ///< 替身每次交回的这一枚句柄
        constexpr int      kFrameWidth             = 64;
        constexpr int      kFrameHeight            = 48;

        /// 与任何真屏都不重名的屏名：交付会走到「屏不在」的兜底，不开标注窗也不记账
        constexpr char kGhostScreenName[] = "GhostScreen";

        /**
         * @brief 替掉真实抓屏：每一路各交回同一帧，并把「被叫了几次、叫的是什么」留住
         * @details 计数是这几条判据的落脚点：延时那一档要判「到点之前一次都没被叫」，
         *          换入口那一档要判「叫的是哪一路」。
         */
        class CountingScreenSource final : public GyanisScribe::Capture::IScreenCaptureSource
        {
        public:
            explicit CountingScreenSource(Base::CapturedImage frame) : m_frame(std::move(frame))
            {
            }

            [[nodiscard]] Base::Expected<Base::CapturedImage> grabScreen(const QString &screenName) const override
            {
                ++screenCalls;
                requestedScreen = screenName;
                return m_frame;
            }

            [[nodiscard]] Base::Expected<Base::CapturedImage> grabWindow(const quintptr handle) const override
            {
                ++windowCalls;
                requestedWindow = handle;
                return m_frame;
            }

            mutable int      screenCalls = 0;     ///< 整屏这一路被叫了几次
            mutable int      windowCalls = 0;     ///< 窗口这一路被叫了几次
            mutable QString  requestedScreen;     ///< 最近一次叫的屏名
            mutable quintptr requestedWindow = 0; ///< 最近一次叫的窗口句柄

        private:
            Base::CapturedImage m_frame; ///< 与生产同形状的那一帧假屏
        };

        /**
         * @brief 只演「前台窗口就是这一扇」的平台集成替身
         * @details 能力位全假：界面按「本平台给不出」筛菜单，与本组判据无关；
         *          前台窗口查询次数是「到点才取」那条判据的落脚点。
         */
        class ForegroundWindowStub final : public Platform::IPlatformIntegration
        {
        public:
            explicit ForegroundWindowStub(const quintptr handle) : m_handle(handle)
            {
            }

            [[nodiscard]] Platform::PlatformCapabilities capabilities() const noexcept override
            {
                return Platform::PlatformCapabilities{};
            }

            [[nodiscard]] QString displayName() const override
            {
                return QStringLiteral("直测替身");
            }

            [[nodiscard]] std::unique_ptr<Platform::IHotkeyBackend> createHotkeyBackend() override
            {
                return nullptr;
            }

            [[nodiscard]] Base::Expected<Platform::ForegroundWindow> foregroundWindow() const override
            {
                ++foregroundQueries;
                return Platform::ForegroundWindow{m_handle, QStringLiteral("替身窗口")};
            }

            [[nodiscard]] Base::Expected<bool> autostartEnabled() const override
            {
                return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported, "替身不演开机启动这一项"});
            }

            [[nodiscard]] Base::Expected<void> setAutostart(const bool enabled) override
            {
                static_cast<void>(enabled);
                return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported, "替身不演开机启动这一项"});
            }

            [[nodiscard]] Base::Expected<std::vector<Platform::ScreenWindow>> screenWindows() const override
            {
                return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported, "替身不演窗口清单这一项"});
            }

            [[nodiscard]] Base::Expected<std::vector<QRect>> elementChainAtPoint(const QPoint &globalLogicalPoint, const std::vector<quintptr> &excludeWindowHandles) const override
            {
                static_cast<void>(globalLogicalPoint);
                static_cast<void>(excludeWindowHandles);
                return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported, "替身不演界面元素这一项"});
            }

            mutable int foregroundQueries = 0; ///< 前台窗口被问了几次

        private:
            quintptr m_handle; ///< 每次交回同一枚句柄
        };

        /**
         * @brief 直测装置：替身把设置、平台、抓帧与历史清单全换成用例自己的那一份
         * @details 每一位成员字段就是一个开关：先摆好字段再 makeController()，
         *          和用例读起来更近（「这一格设了什么」一眼看得见）。
         */
        class AppControllerFixture : public ::testing::Test
        {
        protected:
            static void SetUpTestSuite()
            {
                // 控制器要建主窗口、托盘、气泡这些真模块：跑到离屏平台上，测试不在桌面上闪，
                // 与容器里那一条用同一个读数环境（env.sh 里同样 export QT_QPA_PLATFORM=offscreen）
                qputenv("QT_QPA_PLATFORM", "offscreen");
                application = std::make_unique<QApplication>(argumentCount, arguments);
                QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
                QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe-Tests"));
            }

            static void TearDownTestSuite()
            {
                application.reset();
            }

            void SetUp() override
            {
                const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
                m_root           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-AppController.{}_{}", info->test_suite_name(), info->name());
                std::filesystem::remove_all(m_root);
                std::filesystem::create_directories(m_root);

                // 贴图布局与未保存归档读的是真实的（按测试应用名分开的）缓存目录：
                // 进来先清，别把上一轮留下的摆进这一轮
                std::error_code ignored;
                std::filesystem::remove_all(Base::Paths::cacheDirectory() / "Pins", ignored);
                std::filesystem::remove_all(Base::Paths::unsavedCaptureDirectory(), ignored);

                ASSERT_NE(QGuiApplication::primaryScreen(), nullptr) << "离屏平台该有一块屏，帧的屏名要对上它";
                m_realScreenName = QGuiApplication::primaryScreen()->name();

                // 剪贴板是进程外的共享资源：上一格用例留下的图会让「没写进剪贴板」判成写进去了
                QGuiApplication::clipboard()->clear();
            }

            void TearDown() override
            {
                m_controller.reset();
                std::error_code ignored;
                std::filesystem::remove_all(m_root, ignored);
                std::filesystem::remove_all(Base::Paths::cacheDirectory() / "Pins", ignored);
                std::filesystem::remove_all(Base::Paths::unsavedCaptureDirectory(), ignored);
            }

            /// 造一帧与生产同形状的假屏：逻辑与物理尺寸一致（倍比 1.0），屏名由用例点名
            [[nodiscard]] static Base::CapturedImage makeFrame(const QString &screenName)
            {
                QImage image(kFrameWidth, kFrameHeight, QImage::Format_ARGB32_Premultiplied);
                image.fill(QColor(36, 87, 166));

                Base::CapturedImage frame;
                frame.image            = image;
                frame.logicalSize      = QSize(kFrameWidth, kFrameHeight);
                frame.devicePixelRatio = 1.0;
                frame.screenName       = screenName;
                return frame;
            }

            /// 用替身把控制器支起来：延时、去向与「未保存也进历史」都从上面那几格字段读
            void makeController()
            {
                auto settings = std::make_unique<Settings::SettingsStore>(m_root / "settings.ini");
                ASSERT_TRUE(settings->setValue(Settings::kCaptureDelaySeconds, m_delaySeconds).has_value());
                ASSERT_TRUE(settings->setValue(Settings::kPathScreenshot, Base::Paths::toDisplayText(m_root / "shots")).has_value());
                ASSERT_TRUE(settings->setValue(Settings::kHistoryKeepUnsaved, m_keepUnsaved).has_value());
                ASSERT_TRUE(settings->setValue(Settings::kCaptureAutoSave, m_autoSave).has_value());
                if (m_afterAction != std::nullopt)
                {
                    ASSERT_TRUE(settings->setValue(Settings::kCaptureAfterAction, QString::fromUtf8(std::string{App::captureIntentText(*m_afterAction)}.c_str())).has_value());
                }
                m_settings = settings.get();

                auto source      = std::make_unique<CountingScreenSource>(makeFrame(m_frameScreenName));
                auto integration = std::make_unique<ForegroundWindowStub>(kForegroundWindowHandle);
                m_source         = source.get();
                m_integration    = integration.get();

                App::AppControllerParts parts;
                parts.settings           = std::move(settings);
                parts.integration        = std::move(integration);
                parts.captureSource      = std::move(source);
                parts.historyStoragePath = m_root / "history.json";
                m_controller             = std::make_unique<App::AppController>(std::move(parts));
            }

            /// 读回注入的那份历史清单；条目里指向的成品文件由 load() 自己检查存在性
            [[nodiscard]] Base::Expected<QVector<History::HistoryEntry>> readHistory() const
            {
                const History::HistoryStore store(m_root / "history.json");
                return store.load(History::policyFromSettings(*m_settings), QDateTime::currentDateTime());
            }

            /// 钉住成品落在哪个目录：拿两边的等价文件系统状态判，不抄字符串比前缀
            static void expectUnderDirectory(const std::filesystem::path &file, const std::filesystem::path &directory)
            {
                EXPECT_TRUE(std::filesystem::exists(file)) << "记录指着的成品文件不在盘上：" << file.string();
                std::error_code same;
                const bool      equivalent = std::filesystem::equivalent(file.parent_path(), directory, same);
                EXPECT_FALSE(same) << same.message();
                EXPECT_TRUE(equivalent) << "成品没落在判据点名的目录里：" << file.string();
            }

            /// 等一个条件成立，最长等 budgetMs：延时那一档用的是真定时器，只能转着事件循环真等
            [[nodiscard]] static bool waitUntil(const std::function<bool()> &condition, const int budgetMs = 5000)
            {
                QElapsedTimer clock;
                clock.start();
                while (!condition() && clock.elapsed() < budgetMs)
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                return condition();
            }

            /// 把事件循环空转这段时间：用来判「这段时间里不该发生的事确实没发生」
            static void pumpFor(const int milliseconds)
            {
                QElapsedTimer clock;
                clock.start();
                while (clock.elapsed() < milliseconds)
                {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }

            std::filesystem::path               m_root;                                                    ///< 这一格用例自己的临时目录
            Settings::SettingsStore            *m_settings    = nullptr;                                   ///< 被控制器接管的配置存储（读回历史策略用）
            CountingScreenSource               *m_source      = nullptr;                                   ///< 被抓帧服务接管的替身
            ForegroundWindowStub               *m_integration = nullptr;                                   ///< 被平台那一侧接管的替身
            std::unique_ptr<App::AppController> m_controller;                                              ///< 被测对象
            QString                             m_realScreenName;                                          ///< 离屏平台那块真屏的名字
            int                                 m_delaySeconds    = 0;                                     ///< 延时截图那一格
            QString                             m_frameScreenName = QString::fromLatin1(kGhostScreenName); ///< 假帧带的屏名
            std::optional<App::CaptureIntent>   m_afterAction;                                             ///< 「截完之后」那一格；空 = 不写
            bool                                m_keepUnsaved = false;                                     ///< 「未保存也进历史」那一格
            bool                                m_autoSave    = false;                                     ///< 「自动保存」那一格
        };
    } // namespace

    /**
     * @brief 钉住「0 秒就是不延时」：按下那一刻帧已经抓完，不等任何一次事件循环
     * @details 「0 秒」若走成「定时器 0 毫秒」也还是异步的，调用返回时这条就不成立了；
     *          把「就地开抓」与「排个定时器」分开，是这一档存在的全部意义。
     */
    TEST_F(AppControllerFixture, ZeroDelayStartsTheCaptureRightAway)
    {
        makeController();
        m_controller->startFullScreenCapture(App::CaptureIntent::CopyToClipboard);

        EXPECT_EQ(m_source->screenCalls, 1) << "0 秒档该在按下那一刻抓完";
        EXPECT_EQ(m_source->windowCalls, 0);
    }

    /**
     * @brief 钉住「设了几秒就等几秒」：到点之前一次都没被抓，到点之后正好一次
     * @details 只判「最后抓到了」会把「立刻抓走」也放过去——而这一档存在的全部意义就是先等；
     *          到点那一半用有界轮询等，等的是真定时器，不是调度运气。
     */
    TEST_F(AppControllerFixture, DelayHoldsTheCaptureUntilTheTimerFires)
    {
        m_delaySeconds = 1;
        makeController();
        m_controller->startFullScreenCapture(App::CaptureIntent::CopyToClipboard);

        EXPECT_EQ(m_source->screenCalls, 0) << "设了 1 秒延时，按下那一刻不该已经抓完";
        ASSERT_TRUE(waitUntil([this] { return m_source->screenCalls >= 1; })) << "到点了还没抓";
        EXPECT_EQ(m_source->screenCalls, 1) << "一次按下只该抓一帧";
    }

    /**
     * @brief 钉住「截取当前窗口走同一条延时通道，且到点那一刻才取前台窗口」
     * @details 这一格漏过一次：前台窗口在按下那一刻就取走、截图也立刻开抓，延时被绕开；
     *          等的那几秒本是用来把菜单摆出来的，取早了取到的就是错的窗口。
     *          两条判据分开钉：句柄查询与抓帧，各自「按下为零、到点为一」。
     */
    TEST_F(AppControllerFixture, ForegroundWindowIsFetchedOnlyWhenTheTimerFires)
    {
        m_delaySeconds = 1;
        makeController();
        m_controller->captureForegroundWindow();

        EXPECT_EQ(m_integration->foregroundQueries, 0) << "按下那一刻就取前台窗口＝绕过延时";
        EXPECT_EQ(m_source->windowCalls, 0) << "延时没到就开抓窗口";

        ASSERT_TRUE(waitUntil([this] { return m_source->windowCalls >= 1; })) << "到点了还没抓窗口";
        EXPECT_EQ(m_integration->foregroundQueries, 1) << "到点该取一次前台窗口";
        EXPECT_EQ(m_source->windowCalls, 1) << "一次按下只该抓一帧";
        EXPECT_EQ(m_source->requestedWindow, kForegroundWindowHandle) << "抓的该是那一刻取到的那枚句柄";
    }

    /**
     * @brief 钉住「在等的时候再按一次」：不排第二个、也不把在等的那一次顶掉
     * @details 两种坏形状各挡一半：偷偷排进第二枚定时器会把两次都做出来；后按的顶掉先按的
     *          会让用户等到的动作不是自己第一次按下的那个。两次按的是不同入口，按「哪一路被抓了」分账。
     */
    TEST_F(AppControllerFixture, SecondPressWhileWaitingKeepsTheFirstAction)
    {
        m_delaySeconds = 1;
        makeController();
        m_controller->startFullScreenCapture(App::CaptureIntent::CopyToClipboard);
        m_controller->captureForegroundWindow(); // 顶上再按一次：该被回绝

        ASSERT_TRUE(waitUntil([this] { return m_source->screenCalls > 0 || m_source->windowCalls > 0; })) << "先按的那一次都没跑起来";
        pumpFor(700); // 若第二下真排了枚定时器，这一档够它到点
        EXPECT_EQ(m_source->screenCalls, 1) << "先按的那一次该照常跑";
        EXPECT_EQ(m_source->windowCalls, 0) << "后按的那一次既不该排进去、也不该把在等的顶掉";
        EXPECT_EQ(m_integration->foregroundQueries, 0);
    }

    /**
     * @brief 钉住「快速保存：落盘一份 + 历史清单上正好一条」
     * @details 清单必须落在注入的那份上：写进用户真实的清单是测试事故，读自己那份才判得出。
     *          条数是「正好一」而不是「至少一」：重复记账会让同一张图在历史里出现两次。
     */
    TEST_F(AppControllerFixture, QuickSaveLandsTheFileAndOneHistoryEntry)
    {
        m_frameScreenName = m_realScreenName;
        makeController();
        m_controller->startFullScreenCapture(App::CaptureIntent::QuickSave);

        ASSERT_EQ(m_source->screenCalls, 1) << "0 秒档该在按下时就抓完";

        const auto entries = readHistory();
        ASSERT_TRUE(entries.has_value()) << "历史清单读不回：" << entries.error().message;
        ASSERT_EQ(entries->size(), 1) << "快速保存该正好记一条";
        EXPECT_EQ(entries->front().kind, History::HistoryKind::Screenshot);
        EXPECT_EQ(entries->front().size, QSize(kFrameWidth, kFrameHeight)) << "记的该是落盘那份的像素尺寸";
        expectUnderDirectory(entries->front().filePath, m_root / "shots");
    }

    /**
     * @brief 钉住「开着『未保存也进历史』时，没落盘的截图归档进历史」
     * @details 归档落点是未保存目录（不是截图目录）：这一档写的就是「没存文件的那一张」，
     *          落错目录等于把它当成了存过的那一类。
     */
    TEST_F(AppControllerFixture, UnsavedCaptureEntersHistoryWhenTheSwitchIsOn)
    {
        m_frameScreenName = m_realScreenName;
        m_keepUnsaved     = true;
        makeController();
        m_controller->startFullScreenCapture(App::CaptureIntent::CopyToClipboard);

        const auto entries = readHistory();
        ASSERT_TRUE(entries.has_value()) << "历史清单读不回：" << entries.error().message;
        ASSERT_EQ(entries->size(), 1) << "开着「未保存也进历史」时该正好记一条";
        EXPECT_EQ(entries->front().kind, History::HistoryKind::Screenshot);
        expectUnderDirectory(entries->front().filePath, Base::Paths::unsavedCaptureDirectory());
    }

    /**
     * @brief 钉住「自动保存与归档两级都开着时，历史只记一条」
     * @details 自动保存已经把这帧写上盘了，归档那一支该认出「已在盘上」而跳过；
     *          两条都记会让同一张截图在历史里出现两次，用户删一条还剩一条。
     */
    TEST_F(AppControllerFixture, AutoSaveRecordsOnceEvenWithTheArchiveSwitchOn)
    {
        m_frameScreenName = m_realScreenName;
        m_autoSave        = true;
        m_keepUnsaved     = true;
        makeController();
        m_controller->startFullScreenCapture(App::CaptureIntent::CopyToClipboard);

        const auto entries = readHistory();
        ASSERT_TRUE(entries.has_value()) << "历史清单读不回：" << entries.error().message;
        ASSERT_EQ(entries->size(), 1) << "自动保存已经落盘，归档不该再记第二条";
        expectUnderDirectory(entries->front().filePath, m_root / "shots");
    }

    /**
     * @brief 钉住「截取当前窗口也按『截完之后』那一格交出去」
     * @details 这条入口此前不读那一格：交付时用的是上一次截图留下的值，冷启动时是默认的
     *          「进标注窗」。设置点名了快速保存，这里就该落盘并在清单上留一条。
     */
    TEST_F(AppControllerFixture, ForegroundWindowFollowsTheAfterCaptureSetting)
    {
        m_frameScreenName = m_realScreenName;
        m_afterAction     = App::CaptureIntent::QuickSave;
        makeController();
        m_controller->captureForegroundWindow();

        ASSERT_EQ(m_source->windowCalls, 1) << "0 秒档该在按下那一刻抓完";

        const auto entries = readHistory();
        ASSERT_TRUE(entries.has_value()) << "历史清单读不回：" << entries.error().message;
        ASSERT_EQ(entries->size(), 1) << "设置点的是快速保存，这一张却没按它交出去";
        expectUnderDirectory(entries->front().filePath, m_root / "shots");
    }

} // namespace GyanisScribe
