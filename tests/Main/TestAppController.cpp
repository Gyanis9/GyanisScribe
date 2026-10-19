// 「把各模块接起来」那一层的直测：延时截图的路由与成品进历史。
// 这一段此前一个实例都建不出来（构造函数自己造设置、抓帧、托盘这些真模块，没有注入口），
// 「截取当前窗口绕过延时」就是这么漏下来的；这一批给它一套与 TestModuleFlow 同形状的替身，
// 从 AppControllerParts 注进去，把「谁在什么时刻被叫到、成品记在哪一份清单上」钉住。

#include "App/CaptureCoordinator.h"
#include "Gui/Annotation/AnnotationWindow.h"
#include "App/TrayController.h"
#include "Gui/Pick/ColorPickerOverlay.h"
#include "Gui/Pin/PinWindow.h"
#include "Gui/Record/RecordingHud.h"
#include "Main/AppController.h"

#include "App/CaptureIntent.h"
#include "Base/Text.h"
#include "Core/Capture/IScreenCaptureSource.h"
#include "Base/Paths.h"
#include "Base/Types.h"
#include "Core/History/HistoryRules.h"
#include "Core/History/HistoryStore.h"
#include "Core/History/HistoryTypes.h"
#include "Platform/IPlatformIntegration.h"
#include "Platform/PlatformCapabilities.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"

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
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSize>
#include <QString>
#include <QVector>

#include <algorithm>
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
        class CountingScreenSource final : public GyanisScribe::Core::IScreenCaptureSource
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
                auto settings = std::make_unique<Core::SettingsStore>(m_root / "settings.ini");
                ASSERT_TRUE(settings->setValue(Core::kCaptureDelaySeconds, m_delaySeconds).has_value());
                ASSERT_TRUE(settings->setValue(Core::kPathScreenshot, Base::Paths::toDisplayText(m_root / "shots")).has_value());
                ASSERT_TRUE(settings->setValue(Core::kHistoryKeepUnsaved, m_keepUnsaved).has_value());
                ASSERT_TRUE(settings->setValue(Core::kCaptureAutoSave, m_autoSave).has_value());
                if (m_afterAction != std::nullopt)
                {
                    ASSERT_TRUE(settings->setValue(Core::kCaptureAfterAction, QString::fromUtf8(std::string{App::captureIntentText(*m_afterAction)}.c_str())).has_value());
                }
                if (m_recordingDirectory != std::nullopt)
                {
                    ASSERT_TRUE(settings->setValue(Core::kPathRecording, Base::Paths::toDisplayText(*m_recordingDirectory)).has_value());
                }
                if (m_recordingCountdown != std::nullopt)
                {
                    ASSERT_TRUE(settings->setValue(Core::kRecordCountdownSeconds, *m_recordingCountdown).has_value());
                }
                m_settings = settings.get();

                auto source      = std::make_unique<CountingScreenSource>(makeFrame(m_frameScreenName));
                auto integration = std::make_unique<ForegroundWindowStub>(kForegroundWindowHandle);
                m_source         = source.get();
                m_integration    = integration.get();

                Main::AppControllerParts parts;
                parts.settings           = std::move(settings);
                parts.integration        = std::move(integration);
                parts.captureSource      = std::move(source);
                parts.historyStoragePath = m_root / "history.json";
                m_controller             = std::make_unique<Main::AppController>(std::move(parts));
            }


            /// 现在桌上有几扇这种顶层窗：接线断了时这一格就是 0
            template<typename Widget>
            [[nodiscard]] static int countTopLevelWindows()
            {
                int count = 0;
                for (auto *widget: QApplication::topLevelWidgets())
                {
                    if (qobject_cast<Widget *>(widget) != nullptr)
                    {
                        ++count;
                    }
                }
                return count;
            }

            /// 屏幕上此刻可见的那些气泡窗：提示是顶层空窗 + 无障碍名「提示」+ ToolTip 窗型（Toast 给自动化留的名字）
            [[nodiscard]] static std::vector<QWidget *> visibleToastBubbles()
            {
                std::vector<QWidget *> bubbles;
                for (auto *widget: QApplication::topLevelWidgets())
                {
                    const bool isToast =
                            widget->isVisible() && widget->accessibleName() == GyanisScribe::Base::uiText("提示") && (widget->windowFlags() & Qt::WindowType_Mask) == Qt::ToolTip;
                    if (isToast)
                    {
                        bubbles.push_back(widget);
                    }
                }
                return bubbles;
            }

            /// 屏幕上新冒出来的那一条提示的正文；没有新气泡时为空
            [[nodiscard]] static QString newToastText(const std::vector<QWidget *> &before)
            {
                for (auto *bubble: visibleToastBubbles())
                {
                    if (std::ranges::find(before, bubble) != before.end())
                    {
                        continue;
                    }
                    for (auto *label: bubble->findChildren<QLabel *>())
                    {
                        if (label->accessibleName() != GyanisScribe::Base::uiText("提示图标"))
                        {
                            return label->text();
                        }
                    }
                }
                return {};
            }

            /// 控制器建出来的那扇主窗口：没有托盘的环境里它会自己现身（离屏平台上托盘装不上）
            [[nodiscard]] static GyanisScribe::Main::MainWindow *mainWindowOnDesk()
            {
                for (auto *widget: QApplication::topLevelWidgets())
                {
                    if (auto *window = qobject_cast<GyanisScribe::Main::MainWindow *>(widget))
                    {
                        return window;
                    }
                }
                return nullptr;
            }

            /// 动作条上文案是这一句的那个按钮：那些按钮与托盘菜单同一张表，文案从表里取
            [[nodiscard]] static QPushButton *actionBarButton(QWidget &window, const QString &label)
            {
                for (auto *button: window.findChildren<QPushButton *>())
                {
                    if (button->text() == label)
                    {
                        return button;
                    }
                }
                return nullptr;
            }

            /// 读回注入的那份历史清单；条目里指向的成品文件由 load() 自己检查存在性
            [[nodiscard]] Base::Expected<QVector<Core::HistoryEntry>> readHistory() const
            {
                const Core::HistoryStore store(m_root / "history.json");
                return store.load(Core::policyFromSettings(*m_settings), QDateTime::currentDateTime());
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

            std::filesystem::path                m_root;                                                    ///< 这一格用例自己的临时目录
            Core::SettingsStore                 *m_settings    = nullptr;                                   ///< 被控制器接管的配置存储（读回历史策略用）
            CountingScreenSource                *m_source      = nullptr;                                   ///< 被抓帧服务接管的替身
            ForegroundWindowStub                *m_integration = nullptr;                                   ///< 被平台那一侧接管的替身
            std::unique_ptr<Main::AppController> m_controller;                                              ///< 被测对象
            QString                              m_realScreenName;                                          ///< 离屏平台那块真屏的名字
            int                                  m_delaySeconds    = 0;                                     ///< 延时截图那一格
            QString                              m_frameScreenName = QString::fromLatin1(kGhostScreenName); ///< 假帧带的屏名
            std::optional<App::CaptureIntent>    m_afterAction;                                             ///< 「截完之后」那一格；空 = 不写
            bool                                 m_keepUnsaved = false;                                     ///< 「未保存也进历史」那一格
            bool                                 m_autoSave    = false;                                     ///< 「自动保存」那一格
            std::optional<std::filesystem::path> m_recordingDirectory;                                      ///< 录屏成品目录；非空就写 kPathRecording（只有录屏那一组设它）
            std::optional<int>                   m_recordingCountdown;                                      ///< 录屏倒计时秒数；不设就按生产默认（3 秒）走
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
        m_controller->capture().startFullScreen(App::CaptureIntent::CopyToClipboard);

        EXPECT_EQ(m_source->screenCalls, 1) << "0 秒档该在按下那一刻抓完";
        EXPECT_EQ(m_source->windowCalls, 0);
    }

    /**
     * @brief 钉住标注窗拖边框之后按新范围重抓
     * @details 窗口只报新几何；藏窗、重抓、换画布与更新选区都在协调器这一层。
     */
    TEST_F(AppControllerFixture, CroppingTheAnnotationWindowRegrabsTheNewRegion)
    {
        makeController();
        auto *screen = QGuiApplication::primaryScreen();
        ASSERT_NE(screen, nullptr);

        auto       &window = m_controller->capture().annotationWindow();
        const auto  before = m_source->screenCalls;
        const QRect region{5, 5, 40, 30};
        window.cropRequested(screen->name(), region);

        EXPECT_EQ(m_source->screenCalls, before + 1) << "拖完边框没有按新范围重抓";
        EXPECT_EQ(m_source->requestedScreen, screen->name()) << "重抓跑到别的屏去了";
        EXPECT_TRUE(window.isVisible()) << "重抓之后标注窗没有重新出现";
        EXPECT_EQ(window.geometry(), region) << "标注窗没有按新范围摆好";
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
        m_controller->capture().startFullScreen(App::CaptureIntent::CopyToClipboard);

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
        m_controller->capture().captureForegroundWindow();

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
        m_controller->capture().startFullScreen(App::CaptureIntent::CopyToClipboard);
        m_controller->capture().captureForegroundWindow(); // 顶上再按一次：该被回绝

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
        m_controller->capture().startFullScreen(App::CaptureIntent::QuickSave);

        ASSERT_EQ(m_source->screenCalls, 1) << "0 秒档该在按下时就抓完";

        const auto entries = readHistory();
        ASSERT_TRUE(entries.has_value()) << "历史清单读不回：" << entries.error().message;
        ASSERT_EQ(entries->size(), 1) << "快速保存该正好记一条";
        EXPECT_EQ(entries->front().kind, Core::HistoryKind::Screenshot);
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
        m_controller->capture().startFullScreen(App::CaptureIntent::CopyToClipboard);

        const auto entries = readHistory();
        ASSERT_TRUE(entries.has_value()) << "历史清单读不回：" << entries.error().message;
        ASSERT_EQ(entries->size(), 1) << "开着「未保存也进历史」时该正好记一条";
        EXPECT_EQ(entries->front().kind, Core::HistoryKind::Screenshot);
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
        m_controller->capture().startFullScreen(App::CaptureIntent::CopyToClipboard);

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
        m_controller->capture().captureForegroundWindow();

        ASSERT_EQ(m_source->windowCalls, 1) << "0 秒档该在按下那一刻抓完";

        const auto entries = readHistory();
        ASSERT_TRUE(entries.has_value()) << "历史清单读不回：" << entries.error().message;
        ASSERT_EQ(entries->size(), 1) << "设置点的是快速保存，这一张却没按它交出去";
        expectUnderDirectory(entries->front().filePath, m_root / "shots");
    }


    /**
     * @brief 钉住「命令行 `--pin`」那条入口在拆族之后照走：桌上要多一张贴图
     * @details 这条链现在是「`Application` → `AppController::pinImageFile`（转发壳）→ `PinCoordinator::pinFile`
     *          → `PinManager` → 一扇贴图窗」。拆族时换过接收者，而件级用例测的是 `PinManager` 自己——
     *          接错线它一声不响，只有从这里走一遍才看得见。
     */
    TEST_F(AppControllerFixture, PinningAFileThroughThePublicEntryPutsAPinOnTheDesk)
    {
        makeController();
        const auto imagePath = m_root / "to-pin.png";
        ASSERT_TRUE(makeFrame(m_realScreenName).image.save(Base::Paths::toDisplayText(imagePath), "PNG")) << "样本图都没写出来";

        const auto before = countTopLevelWindows<GyanisScribe::Gui::PinWindow>();
        const auto pinned = m_controller->pinImageFile(Base::Paths::toDisplayText(imagePath));
        ASSERT_TRUE(pinned.has_value()) << pinned.error().message;
        EXPECT_EQ(countTopLevelWindows<GyanisScribe::Gui::PinWindow>(), before + 1) << "命令行 --pin 那条入口没贴出东西：协调器那条线断了";
    }

    /**
     * @brief 钉住取色入口在拆族之后照走：命令行 `--pick` 与热键都走它
     * @details 这一格钉的是「入口 → 取色协调器 → 一扇遮罩」这条链，中间那次改动换掉了接收者。
     */
    TEST_F(AppControllerFixture, PickingAColorFromTheEntryPointOpensTheOverlay)
    {
        makeController();
        m_frameScreenName = m_realScreenName;
        ASSERT_EQ(countTopLevelWindows<GyanisScribe::Gui::ColorPickerOverlay>(), 0) << "开局就不该有取色遮罩";

        m_controller->startColorPick();
        EXPECT_EQ(countTopLevelWindows<GyanisScribe::Gui::ColorPickerOverlay>(), 1) << "取色入口没铺出遮罩：协调器那条线断了";
    }

    /**
     * @brief 钉住「识别意图截完图之后，图真的交到了识别那一族」
     * @details 这条缝是拆族时新焊的：截图协调器拿着识别协调器的引用，按意图把帧递过去（CaptureCoordinator → OcrCoordinator）。
     *          识别链收到图后必然会开口说话——带真后端的档说「正在识别这一张…」，没后端的档说「没有编入识别后端」，
     *          机器的语言包对不上时会说「没有 zh-Hans」那类失败原因——都落在同一件可观测的事上：
     *          屏幕上多出一扇新的气泡（整条缝断了的话，一扇都不会多）。
     *          结果窗开不开取决于这台机器装没装那门语言，不在这条接缝用例的判据里（那是识别族自己的事，
     *          CI 的 runner 就只装了 en——把它算进来等于让这条用例跟着机器语言包走）。
     */
    TEST_F(AppControllerFixture, CaptureWithRecognizeIntentReachesTheOcrChain)
    {
        m_frameScreenName = m_realScreenName;
        makeController();
        const auto before = visibleToastBubbles();

        m_controller->capture().startFullScreen(App::CaptureIntent::Recognize);
        ASSERT_EQ(m_source->screenCalls, 1) << "0 秒档该在按下时就抓完";

        // 各档文案里都带「识别」或「语言包」两个词之一；拿它把「别的谁弹的提示」挡在外面
        const auto said = [&]
        {
            const QString text = newToastText(before);
            return !text.isEmpty() && (text.contains(QStringLiteral("识别")) || text.contains(QStringLiteral("语言包")));
        };
        EXPECT_TRUE(waitUntil(said)) << "识别意图截完图后识别那一族一句话都没说：截图协调器到识别协调器那条缝断了";
    }

    /**
     * @brief 钉住「动作条上的录屏键真的叫到了录屏协调器」
     * @details 这条链是「动作条按钮 → 控制器分派 → 录屏协调器（拼配置、摆控制条、起服务）」，
     *          拆族时换过接收者，接错一格就是「按了没反应」。离屏平台给不出屏名
     *          （`validateRecorderConfig` 会拒录），所以这一格钉的是同一下点击里该发生的三件事：
     *          协调器被叫到、控制条一闪、不能开的原因当面说给用户——接错线时三样一样都不会出现。
     *          真录一轮 → 出成品 → 进历史那条链由 Core/Plugins 的端到端用例覆盖，这里环境上到不了那一步。
     */
    TEST_F(AppControllerFixture, TogglingRecordingFromTheActionBarReachesTheCoordinator)
    {
        m_recordingDirectory = m_root / "videos";
        m_recordingCountdown = 0;
        makeController();

        auto *window = mainWindowOnDesk();
        ASSERT_NE(window, nullptr) << "没有托盘的环境里主窗口该自己现身，这条用例就指着它的动作条";
        const auto recordLabel = [&]
        {
            for (const auto &entry: GyanisScribe::App::visibleTrayMenuEntries(Platform::PlatformCapabilities{}))
            {
                if (entry.action == GyanisScribe::App::TrayAction::ToggleRecord)
                {
                    return entry.label;
                }
            }
            return QString{};
        }();
        ASSERT_FALSE(recordLabel.isEmpty()) << "托盘表里没有录屏这一项，用例的前提就没了";
        auto *recordButton = actionBarButton(*window, recordLabel);
        ASSERT_NE(recordButton, nullptr) << "动作条上没找到「" << recordLabel.toStdString() << "」那颗按钮";

        const auto before = visibleToastBubbles();
        recordButton->click();

        // 协调器起步前先把控制条摆出来，随后校验不过（离屏没有屏名）又把它收起——收起的是「藏」，
        // 销毁要等事件循环，所以这一刻它还数得到；这条同时挡掉「叫到了别的族」：别处不会摆这扇控制条
        EXPECT_EQ(countTopLevelWindows<GyanisScribe::Gui::RecordingHud>(), 1) << "按了录屏，控制条那一闪都没出现：动作条到录屏协调器那条线断了";
        const auto said = [&] { return newToastText(before).contains(QStringLiteral("录屏没能开始")); };
        EXPECT_TRUE(waitUntil(said)) << "录屏没开成，也没把原因说给用户听：接线断在协调器前面";

        // 「没能开始」不许在历史里留下东西：没开始就没成品
        pumpFor(300);
        const auto entries = readHistory();
        ASSERT_TRUE(entries.has_value());
        EXPECT_TRUE(entries->empty()) << "录屏没开始，历史里却多出了记录";
    }

} // namespace GyanisScribe
