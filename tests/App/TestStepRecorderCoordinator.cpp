// 步骤记录协调器的直测：起停、防抖抓屏、窗口切换、暂停真卸钩、首次说明、上限与故障收场。
// 事件源、平台、抓屏与抓屏来源全部走替身，判据集中在信号次数、会话步骤与临时目录的去留。

#include "App/StepRecorderCoordinator.h"

#include "Base/Logger.h"
#include "Core/Capture/IScreenCaptureSource.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"
#include "Core/Steps/StepSession.h"
#include "Gui/Controls/Toast.h"
#include "Gui/Steps/StepRecorderHud.h"
#include "Gui/Theme/ThemeManager.h"
#include "Platform/IPlatformIntegration.h"
#include "Platform/IStepEventSource.h"

#include <QApplication>
#include <QByteArray>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

Q_DECLARE_METATYPE(GyanisScribe::Gui::ToastKind)

using GyanisScribe::App::StepRecorderCoordinator;
using GyanisScribe::App::StepRecorderDependencies;
using GyanisScribe::Base::CapturedImage;
using GyanisScribe::Base::Error;
using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Base::Expected;
using GyanisScribe::Core::IScreenCaptureSource;
using GyanisScribe::Core::kMaximumSteps;
using GyanisScribe::Core::kStepsConsentAccepted;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Gui::StepRecorderHud;
using GyanisScribe::Gui::ThemeManager;
using GyanisScribe::Gui::ToastKind;
using GyanisScribe::Platform::ForegroundWindow;
using GyanisScribe::Platform::IHotkeyBackend;
using GyanisScribe::Platform::IPlatformIntegration;
using GyanisScribe::Platform::IStepEventSource;
using GyanisScribe::Platform::PlatformCapabilities;
using GyanisScribe::Platform::ScreenWindow;
using GyanisScribe::Platform::StepInputEvent;
using GyanisScribe::Platform::UiElementInfo;

namespace
{
    /// 用例里点一下用的固定坐标：落在副屏，换算到替身抓回的 400x300 图内仍是 (100,100)
    constexpr QPoint kClickPoint{900, 100};

    /// 离屏平台的两块命名屏：用例靠名字分开「事件点所在屏」与「光标所在屏」，副屏另带一个缩放因子
    constexpr char kScreensConfigJson[] = R"({
  "synchronousWindowSystemEvents": false,
  "windowFrameMargins": true,
  "screens": [
    {"name": "screen-main", "x": 0, "y": 0, "width": 800, "height": 600, "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1.0},
    {"name": "screen-side", "x": 800, "y": 0, "width": 800, "height": 600, "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1.5}
  ]
})";

    /// 离屏配置文件名：SetUpTestSuite 写、TearDownTestSuite 删；每个用例进程各用一份，避免并行互踩
    QString screensConfigFileName;

    class FakeStepEventSource final : public IStepEventSource
    {
    public:
        explicit FakeStepEventSource(std::shared_ptr<int> stopCounter = {}) : m_stopCounter(std::move(stopCounter))
        {
        }

        [[nodiscard]] Expected<void> start() override
        {
            ++startCalls;
            if (failNextStart)
            {
                failNextStart = false;
                running       = false;
                return std::unexpected(Error{ErrorCode::InternalError, "测试替身：钩子安装失败"});
            }
            running = true;
            return {};
        }

        void stop() override
        {
            ++stopCalls;
            if (m_stopCounter)
            {
                ++*m_stopCounter;
            }
            running = false;
        }

        [[nodiscard]] bool isRunning() const noexcept override
        {
            return running;
        }

        void emitClick(const QPoint &point, const bool rightButton = false, const quintptr handle = 0)
        {
            StepInputEvent event;
            event.kind               = StepInputEvent::Kind::MouseClick;
            event.globalLogicalPoint = point;
            event.windowHandle       = handle;
            event.rightButton        = rightButton;
            event.at                 = QDateTime::currentDateTime();
            emit stepEvent(event);
        }

        void emitWindowChange(const quintptr handle)
        {
            StepInputEvent event;
            event.kind         = StepInputEvent::Kind::ForegroundWindowChanged;
            event.windowHandle = handle;
            event.at           = QDateTime::currentDateTime();
            emit stepEvent(event);
        }

        void emitFailure(const QString &reason)
        {
            emit failed(Error{ErrorCode::InternalError, reason.toStdString()});
        }

        bool running       = false;
        int  startCalls    = 0;
        int  stopCalls     = 0;
        bool failNextStart = false;

    private:
        std::shared_ptr<int> m_stopCounter; ///< 事件源销毁后仍能读到的停止计数
    };

    class FakePlatform final : public IPlatformIntegration
    {
    public:
        FakePlatform()
        {
            m_capabilities.stepRecorder    = true;
            m_capabilities.windowDiscovery = true;
            m_capabilities.uiElements      = true;
            m_windows.push_back(ScreenWindow{7, QStringLiteral("Demo"), QStringLiteral("demo.exe"), QRect(0, 0, 800, 600), QRect(0, 0, 800, 600)});
        }

        [[nodiscard]] PlatformCapabilities capabilities() const noexcept override
        {
            return m_capabilities;
        }

        [[nodiscard]] QString displayName() const override
        {
            return QStringLiteral("Fake");
        }

        [[nodiscard]] std::unique_ptr<IHotkeyBackend> createHotkeyBackend() override
        {
            return {};
        }

        [[nodiscard]] Expected<ForegroundWindow> foregroundWindow() const override
        {
            if (foregroundFails)
            {
                return std::unexpected(Error{ErrorCode::PlatformUnsupported, "测试替身：取不到前台窗口"});
            }
            return m_foreground;
        }

        [[nodiscard]] Expected<bool> autostartEnabled() const override
        {
            return false;
        }

        [[nodiscard]] Expected<void> setAutostart(bool) override
        {
            return {};
        }

        [[nodiscard]] Expected<std::vector<ScreenWindow>> screenWindows() const override
        {
            return m_windows;
        }

        [[nodiscard]] Expected<std::vector<QRect>> elementChainAtPoint(const QPoint &, const std::vector<quintptr> &) const override
        {
            return std::vector<QRect>{};
        }

        [[nodiscard]] Expected<std::optional<UiElementInfo>> uiElementAtPoint(const QPoint &point, const std::vector<quintptr> &exclude) const override
        {
            ++uiElementCalls;
            lastPoint   = point;
            lastExclude = exclude;
            if (uiElementFails)
            {
                return std::unexpected(Error{ErrorCode::PlatformUnsupported, "测试替身：读不到界面元素"});
            }
            return std::optional<UiElementInfo>{m_element};
        }

        PlatformCapabilities          m_capabilities;
        ForegroundWindow              m_foreground{7, QStringLiteral("Demo"), QStringLiteral("demo.exe")};
        std::vector<ScreenWindow>     m_windows;
        UiElementInfo                 m_element{QStringLiteral("保存"), QStringLiteral("按钮"), QStringLiteral("Demo"), QStringLiteral("demo.exe"), QRect(90, 90, 40, 30)};
        bool                          uiElementFails  = false;
        mutable bool                  foregroundFails = false;
        mutable int                   uiElementCalls  = 0;
        mutable QPoint                lastPoint;
        mutable std::vector<quintptr> lastExclude;
    };

    class FakeCapture final : public IScreenCaptureSource
    {
    public:
        [[nodiscard]] Expected<CapturedImage> grabScreen(const QString &screenName) const override
        {
            ++calls;
            lastScreenName = screenName;
            if (failAlways || failNext)
            {
                failNext = false;
                return std::unexpected(Error{ErrorCode::ScreenUnavailable, "测试替身：抓屏失败"});
            }

            CapturedImage captured;
            captured.image = QImage{QSize(400, 300), QImage::Format_ARGB32_Premultiplied};
            captured.image.fill(backgroundColor);
            captured.logicalSize      = QSize(400, 300);
            captured.devicePixelRatio = 1.0;
            captured.screenName       = QStringLiteral("Fake");
            captured.logicalRegion    = QRect(0, 0, 400, 300);
            captured.capturedAt       = QDateTime::currentDateTime();
            return captured;
        }

        [[nodiscard]] Expected<CapturedImage> grabWindow(quintptr) const override
        {
            return std::unexpected(Error{ErrorCode::PlatformUnsupported, "测试替身：不抓窗口"});
        }

        QColor          backgroundColor{12, 200, 34};
        mutable int     calls = 0;
        mutable QString lastScreenName;
        mutable bool    failNext   = false;
        mutable bool    failAlways = false;
    };

    std::unique_ptr<QApplication> application;

    class StepRecorderCoordinatorFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // 控制条要真的上屏，离屏平台让用例不在桌面上闪；配置里摆两块命名屏，
            // 点击落在副屏、光标留在主屏，才能判出抓的是事件点所在屏而不是光标屏
            screensConfigFileName = QStringLiteral("step-recorder-screens-%1.json").arg(QString::number(QRandomGenerator::global()->generate64(), 16));
            QFile screensConfig{screensConfigFileName};
            ASSERT_TRUE(screensConfig.open(QIODevice::WriteOnly | QIODevice::Truncate)) << "离屏配置写不进当前目录";
            ASSERT_GT(screensConfig.write(kScreensConfigJson), 0);
            screensConfig.close();
            qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen:configfile=") + screensConfigFileName.toUtf8());

            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestAppStepRecorder";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
            QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
            QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe-Tests"));
            // 日志只留错误级：用例判的是状态与信号，info 会把结果冲散
            const auto logDirectory = std::filesystem::temp_directory_path() / "GyanisScribe-StepRecorder-logs";
            static_cast<void>(GyanisScribe::Base::Logger::initialize(logDirectory, GyanisScribe::Base::Severity::Error));
        }

        static void TearDownTestSuite()
        {
            GyanisScribe::Base::Logger::shutdown();
            application.reset();
            QFile::remove(screensConfigFileName);
        }

        void SetUp() override
        {
            const auto caseName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            m_ini               = std::filesystem::temp_directory_path() / std::format("GyanisScribe-StepRecorder-{}.ini", caseName);
            std::error_code ignored;
            std::filesystem::remove(m_ini, ignored);

            m_settings = std::make_unique<SettingsStore>(m_ini);
            static_cast<void>(m_settings->setValue(kStepsConsentAccepted, true));
            m_theme    = std::make_unique<ThemeManager>(*m_settings);
            m_tempRoot = std::make_unique<QTemporaryDir>();
            ASSERT_TRUE(m_tempRoot->isValid());
            m_platform = std::make_unique<FakePlatform>();
            m_capture  = std::make_unique<FakeCapture>();
        }

        void TearDown() override
        {
            m_capture.reset();
            m_platform.reset();
            m_theme.reset();
            m_settings.reset();
            m_tempRoot.reset();
            std::error_code ignored;
            std::filesystem::remove(m_ini, ignored);
        }

        StepRecorderDependencies dependencies()
        {
            return StepRecorderDependencies{*m_settings,
                                            *m_platform,
                                            *m_capture,
                                            *m_theme,
                                            [this]
                                            {
                                                auto source           = std::make_unique<FakeStepEventSource>(m_stopSignals);
                                                source->failNextStart = m_failFirstStart;
                                                m_source              = source.get();
                                                return source;
                                            },
                                            [this](QWidget *)
                                            {
                                                ++m_consentCalls;
                                                return m_consentAnswer;
                                            },
                                            std::filesystem::path{m_tempRoot->path().toStdWString()}};
        }

        std::unique_ptr<StepRecorderCoordinator> makeCoordinator()
        {
            return std::make_unique<StepRecorderCoordinator>(dependencies());
        }

        std::filesystem::path          m_ini;
        std::unique_ptr<SettingsStore> m_settings;
        std::unique_ptr<ThemeManager>  m_theme;
        std::unique_ptr<QTemporaryDir> m_tempRoot;
        std::unique_ptr<FakePlatform>  m_platform;
        std::unique_ptr<FakeCapture>   m_capture;
        FakeStepEventSource           *m_source         = nullptr;
        std::shared_ptr<int>           m_stopSignals    = std::make_shared<int>(0);
        bool                           m_failFirstStart = false;
        bool                           m_consentAnswer  = true;
        int                            m_consentCalls   = 0;
    };

    /// 取事件里给的坐标所在屏，取不到时按主屏算，与协调器的选屏规则一致
    QScreen *screenAtOrPrimary(const QPoint &point)
    {
        if (auto *screen = QGuiApplication::screenAt(point))
        {
            return screen;
        }
        return QGuiApplication::primaryScreen();
    }
} // namespace

/// 开始录制装钩子并显示控制条，再按一次全部收掉
TEST_F(StepRecorderCoordinatorFixture, StartsStopsAndShowsHud)
{
    auto       coordinator = makeCoordinator();
    QSignalSpy stateSpy{coordinator.get(), &StepRecorderCoordinator::stateChanged};
    QSignalSpy stoppedSpy{coordinator.get(), &StepRecorderCoordinator::stopped};

    coordinator->toggleRecording();
    ASSERT_TRUE(coordinator->isRecording());
    EXPECT_FALSE(coordinator->isPaused());
    ASSERT_NE(m_source, nullptr);
    EXPECT_TRUE(m_source->isRunning());
    EXPECT_EQ(m_source->startCalls, 1);
    ASSERT_NE(coordinator->hudForTesting(), nullptr);
    EXPECT_TRUE(coordinator->hudForTesting()->isVisible());
    ASSERT_EQ(stateSpy.count(), 1);
    EXPECT_TRUE(stateSpy.at(0).at(0).toBool());
    EXPECT_FALSE(stateSpy.at(0).at(1).toBool());
    EXPECT_EQ(stateSpy.at(0).at(2).toInt(), 0);

    coordinator->toggleRecording();
    EXPECT_FALSE(coordinator->isRecording());
    EXPECT_FALSE(m_source->isRunning());
    EXPECT_FALSE(coordinator->hudForTesting()->isVisible());
    ASSERT_EQ(stateSpy.count(), 2);
    EXPECT_FALSE(stateSpy.at(1).at(0).toBool());
    EXPECT_FALSE(stateSpy.at(1).at(1).toBool());
    ASSERT_EQ(stoppedSpy.count(), 1);
    EXPECT_EQ(stoppedSpy.at(0).at(0).toInt(), 0);
}

/// 点击先攒到延迟之后才成步：说明取 UIA，截图按原始画面落盘，点击点按屏内逻辑坐标记
TEST_F(StepRecorderCoordinatorFixture, ClickBecomesAStepAfterDelay)
{
    EXPECT_EQ(GyanisScribe::App::kCaptureDelayMs, 250);
    EXPECT_EQ(GyanisScribe::App::kClickMergeWindowMs, GyanisScribe::Core::kClickMergeWindowMs);

    auto       coordinator = makeCoordinator();
    QSignalSpy stateSpy{coordinator.get(), &StepRecorderCoordinator::stateChanged};
    coordinator->toggleRecording();

    // 光标留在主屏：点击落在副屏，抓的必须是事件点所在屏而不是光标屏
    QCursor::setPos(QPoint(50, 50));
    auto *cursorScreen = QGuiApplication::screenAt(QCursor::pos());
    ASSERT_NE(cursorScreen, nullptr);
    EXPECT_EQ(cursorScreen->name(), QStringLiteral("screen-main"));

    m_source->emitClick(kClickPoint);
    EXPECT_EQ(coordinator->stepCount(), 0) << "延迟未到就先落了一步";

    coordinator->flushPendingForTesting();
    ASSERT_EQ(coordinator->stepCount(), 1);
    const auto &steps = coordinator->sessionForTesting()->steps();
    EXPECT_TRUE(steps.front().description.contains(QStringLiteral("保存")));
    EXPECT_EQ(steps.front().kind, GyanisScribe::Core::StepKind::Click);
    EXPECT_TRUE(QFileInfo::exists(steps.front().imagePath));
    EXPECT_TRUE(steps.front().captureError.isEmpty());
    EXPECT_EQ(m_platform->uiElementCalls, 1);
    EXPECT_EQ(m_platform->lastPoint, kClickPoint);

    ASSERT_EQ(stateSpy.count(), 2) << "开始一次、落一步一次";
    EXPECT_TRUE(stateSpy.at(1).at(0).toBool());
    EXPECT_FALSE(stateSpy.at(1).at(1).toBool());
    EXPECT_EQ(stateSpy.at(1).at(2).toInt(), 1);

    auto *screen = screenAtOrPrimary(kClickPoint);
    ASSERT_NE(screen, nullptr);
    EXPECT_EQ(screen->name(), QStringLiteral("screen-side")) << "固定点击点应落在副屏";
    EXPECT_EQ(m_capture->lastScreenName, screen->name());
    EXPECT_NE(m_capture->lastScreenName, cursorScreen->name());
    EXPECT_EQ(steps.front().clickPointInImage, kClickPoint - screen->geometry().topLeft());
    EXPECT_DOUBLE_EQ(steps.front().imageDevicePixelRatio, screen->devicePixelRatio());

    const QImage raw{steps.front().imagePath};
    ASSERT_FALSE(raw.isNull());
    EXPECT_EQ(raw.size(), QSize(400, 300));
    EXPECT_EQ(raw.pixelColor(steps.front().clickPointInImage), m_capture->backgroundColor) << "截图里不应预先画上点击标记";

    auto *count = coordinator->hudForTesting()->findChild<QLabel *>(QStringLiteral("stepsHudCount"));
    ASSERT_NE(count, nullptr);
    EXPECT_TRUE(count->text().contains(QStringLiteral("1")));
}

/// 前台窗口切换成一步：标题与进程取当时的前台窗口，抓的是该窗口所在屏
TEST_F(StepRecorderCoordinatorFixture, WindowChangeBecomesAStep)
{
    auto coordinator = makeCoordinator();
    coordinator->toggleRecording();
    m_source->emitWindowChange(7);
    coordinator->flushPendingForTesting();

    ASSERT_EQ(coordinator->stepCount(), 1);
    const auto &step = coordinator->sessionForTesting()->steps().front();
    EXPECT_EQ(step.kind, GyanisScribe::Core::StepKind::WindowChange);
    EXPECT_EQ(step.windowTitle, QStringLiteral("Demo"));
    EXPECT_EQ(step.processName, QStringLiteral("demo.exe"));
    EXPECT_TRUE(step.description.contains(QStringLiteral("切换到")));

    auto *screen = screenAtOrPrimary(m_platform->m_windows.front().logicalGlobal.center());
    ASSERT_NE(screen, nullptr);
    EXPECT_EQ(m_capture->lastScreenName, screen->name());
    EXPECT_EQ(m_capture->calls, 1);
}

/// 暂停真的卸掉钩子并丢掉待处理事件；继续后重新装上
TEST_F(StepRecorderCoordinatorFixture, PauseUninstallsHooksAndDropsEvents)
{
    auto       coordinator = makeCoordinator();
    QSignalSpy stateSpy{coordinator.get(), &StepRecorderCoordinator::stateChanged};
    coordinator->toggleRecording();
    m_source->emitClick(kClickPoint);
    ASSERT_EQ(stateSpy.count(), 1);

    const int stopCallsBeforePause = m_source->stopCalls;
    coordinator->togglePauseForTesting();
    EXPECT_TRUE(coordinator->isPaused());
    EXPECT_FALSE(m_source->isRunning());
    EXPECT_EQ(m_source->stopCalls, stopCallsBeforePause + 1);
    coordinator->flushPendingForTesting();
    EXPECT_EQ(coordinator->stepCount(), 0) << "暂停前攒下的点击不该在暂停后落成一步";

    m_source->emitClick(kClickPoint);
    coordinator->flushPendingForTesting();
    EXPECT_EQ(coordinator->stepCount(), 0) << "暂停期间的事件要丢掉";

    coordinator->togglePauseForTesting();
    EXPECT_FALSE(coordinator->isPaused());
    EXPECT_TRUE(m_source->isRunning());
    EXPECT_EQ(coordinator->stepCount(), 0);
    ASSERT_GE(stateSpy.count(), 3);
    EXPECT_TRUE(stateSpy.at(1).at(1).toBool());
    EXPECT_FALSE(stateSpy.last().at(1).toBool());
}

/// 控制条上的暂停与停止按钮分别接到暂停与停止入口
TEST_F(StepRecorderCoordinatorFixture, HudButtonsTogglePauseAndStop)
{
    auto       coordinator = makeCoordinator();
    QSignalSpy stateSpy{coordinator.get(), &StepRecorderCoordinator::stateChanged};
    QSignalSpy stoppedSpy{coordinator.get(), &StepRecorderCoordinator::stopped};
    coordinator->toggleRecording();
    auto *hud = coordinator->hudForTesting();
    ASSERT_NE(hud, nullptr);
    auto *pauseButton = hud->findChild<QPushButton *>(QStringLiteral("stepsHudPause"));
    auto *stopButton  = hud->findChild<QPushButton *>(QStringLiteral("stepsHudStop"));
    ASSERT_NE(pauseButton, nullptr);
    ASSERT_NE(stopButton, nullptr);

    pauseButton->click();
    EXPECT_TRUE(coordinator->isPaused());
    EXPECT_FALSE(m_source->isRunning());
    EXPECT_EQ(pauseButton->text(), QStringLiteral("继续"));
    ASSERT_GE(stateSpy.count(), 2);
    EXPECT_TRUE(stateSpy.at(1).at(1).toBool());

    pauseButton->click();
    EXPECT_FALSE(coordinator->isPaused());
    EXPECT_TRUE(m_source->isRunning());
    EXPECT_EQ(pauseButton->text(), QStringLiteral("暂停"));

    stopButton->click();
    EXPECT_FALSE(coordinator->isRecording());
    EXPECT_FALSE(coordinator->isPaused());
    EXPECT_FALSE(m_source->isRunning());
    EXPECT_FALSE(hud->isVisible());
    ASSERT_EQ(stoppedSpy.count(), 1);
    EXPECT_EQ(stoppedSpy.at(0).at(0).toInt(), 0);
}

/// 首次说明被拒就不开录；同意后写配置，之后不再问
TEST_F(StepRecorderCoordinatorFixture, ConsentDeclinedDoesNotStart)
{
    static_cast<void>(m_settings->setValue(kStepsConsentAccepted, false));
    m_consentAnswer = false;

    auto coordinator = makeCoordinator();
    coordinator->toggleRecording();
    EXPECT_FALSE(coordinator->isRecording());
    EXPECT_EQ(m_consentCalls, 1);
    EXPECT_EQ(m_source, nullptr) << "没同意就不该建事件源";
    EXPECT_FALSE(m_settings->boolValue(kStepsConsentAccepted, true));

    m_consentAnswer = true;
    coordinator->toggleRecording();
    EXPECT_TRUE(coordinator->isRecording());
    EXPECT_EQ(m_consentCalls, 2);
    EXPECT_TRUE(m_settings->boolValue(kStepsConsentAccepted, false));

    coordinator->toggleRecording();
    coordinator->toggleRecording();
    EXPECT_TRUE(coordinator->isRecording());
    EXPECT_EQ(m_consentCalls, 2) << "同意过就不该再问";
}

/// 停止后再开始是全新会话：步数清零、旧临时目录收掉、换一个新事件源
TEST_F(StepRecorderCoordinatorFixture, RestartAfterStopStartsFreshSession)
{
    auto coordinator = makeCoordinator();
    coordinator->toggleRecording();
    FakeStepEventSource *firstSource = m_source;
    m_source->emitClick(kClickPoint);
    coordinator->flushPendingForTesting();
    ASSERT_EQ(coordinator->stepCount(), 1);
    const auto firstDirectory = coordinator->sessionForTesting()->tempDirectory();
    ASSERT_TRUE(std::filesystem::exists(firstDirectory));

    coordinator->toggleRecording();
    ASSERT_FALSE(coordinator->isRecording());

    QSignalSpy stateSpy{coordinator.get(), &StepRecorderCoordinator::stateChanged};
    coordinator->toggleRecording();
    EXPECT_TRUE(coordinator->isRecording());
    EXPECT_EQ(coordinator->stepCount(), 0) << "新会话不能带着上一轮的步骤";
    ASSERT_NE(m_source, nullptr);
    EXPECT_NE(m_source, firstSource);
    EXPECT_EQ(m_source->startCalls, 1);
    EXPECT_TRUE(m_source->isRunning());
    EXPECT_FALSE(std::filesystem::exists(firstDirectory)) << "重开前先收掉上一轮的临时目录";
    EXPECT_NE(coordinator->sessionForTesting()->tempDirectory(), firstDirectory);
    ASSERT_EQ(stateSpy.count(), 1);
    EXPECT_TRUE(stateSpy.at(0).at(0).toBool());
    EXPECT_EQ(stateSpy.at(0).at(2).toInt(), 0);
}

/// 能力表没给步骤记录时不开录，也不弹首次说明
TEST_F(StepRecorderCoordinatorFixture, UnsupportedPlatformDoesNotStart)
{
    m_platform->m_capabilities.stepRecorder = false;
    auto       coordinator                  = makeCoordinator();
    QSignalSpy messageSpy{coordinator.get(), &StepRecorderCoordinator::messageForUser};

    coordinator->toggleRecording();
    EXPECT_FALSE(coordinator->isRecording());
    EXPECT_EQ(m_consentCalls, 0);
    EXPECT_EQ(m_source, nullptr);
    EXPECT_EQ(messageSpy.count(), 1);
}

/// 事件源装不上时回到未录制，临时目录不留下半截会话
TEST_F(StepRecorderCoordinatorFixture, StartFailureCleansTempAndDoesNotRecord)
{
    m_failFirstStart       = true;
    auto       coordinator = makeCoordinator();
    QSignalSpy messageSpy{coordinator.get(), &StepRecorderCoordinator::messageForUser};

    coordinator->toggleRecording();
    EXPECT_FALSE(coordinator->isRecording());
    EXPECT_EQ(coordinator->hudForTesting(), nullptr) << "没开成就没有控制条";
    EXPECT_TRUE(coordinator->sessionForTesting()->tempDirectory().empty());
    EXPECT_EQ(QDir{m_tempRoot->path()}.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
    ASSERT_EQ(messageSpy.count(), 1);
    EXPECT_TRUE(messageSpy.last().at(0).toString().contains(QStringLiteral("钩子安装失败")));
}

/// 点在自己的控制条上不算步骤
TEST_F(StepRecorderCoordinatorFixture, IgnoresOwnWindowClicks)
{
    auto coordinator = makeCoordinator();
    coordinator->toggleRecording();
    const quintptr ownHandle = coordinator->hudForTesting()->winId();
    ASSERT_NE(ownHandle, 0U);

    m_source->emitClick(kClickPoint, false, ownHandle);
    coordinator->flushPendingForTesting();
    EXPECT_EQ(coordinator->stepCount(), 0);
    EXPECT_EQ(m_platform->uiElementCalls, 0) << "自家窗口上的点击连 UIA 都不该问";

    m_source->emitClick(kClickPoint, false, 7);
    coordinator->flushPendingForTesting();
    EXPECT_EQ(coordinator->stepCount(), 1);
}

/// 抓屏失败仍落一步，只留占位原因，不中断录制
TEST_F(StepRecorderCoordinatorFixture, CaptureFailureKeepsPlaceholder)
{
    auto coordinator = makeCoordinator();
    coordinator->toggleRecording();
    m_capture->failNext = true;

    m_source->emitClick(kClickPoint);
    coordinator->flushPendingForTesting();
    ASSERT_EQ(coordinator->stepCount(), 1);
    const auto &step = coordinator->sessionForTesting()->steps().front();
    EXPECT_FALSE(step.captureError.isEmpty());
    EXPECT_TRUE(step.imagePath.isEmpty());
    EXPECT_TRUE(step.description.contains(QStringLiteral("保存")));
    EXPECT_TRUE(coordinator->isRecording());
}

/// 临时图写不进盘就停止：已记的步骤保留，失败的那一步带占位原因
TEST_F(StepRecorderCoordinatorFixture, AttachFailureStopsRecordingAndKeepsSteps)
{
    auto       coordinator = makeCoordinator();
    QSignalSpy messageSpy{coordinator.get(), &StepRecorderCoordinator::messageForUser};
    QSignalSpy stoppedSpy{coordinator.get(), &StepRecorderCoordinator::stopped};
    coordinator->toggleRecording();
    m_source->emitClick(kClickPoint);
    coordinator->flushPendingForTesting();
    ASSERT_EQ(coordinator->stepCount(), 1);

    // 删掉会话临时目录：下一张整图没有落点
    std::error_code ignored;
    std::filesystem::remove_all(coordinator->sessionForTesting()->tempDirectory(), ignored);

    m_source->emitClick(kClickPoint + QPoint(40, 40));
    coordinator->flushPendingForTesting();

    EXPECT_FALSE(coordinator->isRecording());
    EXPECT_FALSE(m_source->isRunning());
    EXPECT_FALSE(coordinator->hudForTesting()->isVisible());
    ASSERT_EQ(coordinator->stepCount(), 2) << "写图失败的那一步作为占位留着，之前的一步不动";
    const auto &steps = coordinator->sessionForTesting()->steps();
    EXPECT_TRUE(steps.front().description.contains(QStringLiteral("保存")));
    EXPECT_FALSE(steps.back().captureError.isEmpty());
    EXPECT_TRUE(steps.back().imagePath.isEmpty());
    ASSERT_GE(messageSpy.count(), 1);
    EXPECT_TRUE(messageSpy.last().at(0).toString().contains(QStringLiteral("写不进临时目录")));
    EXPECT_EQ(messageSpy.last().at(1).value<ToastKind>(), ToastKind::Danger);
    ASSERT_EQ(stoppedSpy.count(), 1) << "落盘失败也按正常停止收场";
    EXPECT_EQ(stoppedSpy.at(0).at(0).toInt(), 2);
}

/// 同一元素短时间内的重复点击并成一步
TEST_F(StepRecorderCoordinatorFixture, RepeatedClickMergesIntoOneStep)
{
    auto coordinator = makeCoordinator();
    coordinator->toggleRecording();
    m_source->emitClick(kClickPoint);
    coordinator->flushPendingForTesting();
    m_source->emitClick(kClickPoint + QPoint(1, 1));
    coordinator->flushPendingForTesting();

    EXPECT_EQ(coordinator->stepCount(), 1);
    EXPECT_EQ(m_capture->calls, 1) << "合并掉的点击不该再抓一次屏";
}

/// 延迟期间来的新事件替换旧的：最后一下决定这一步，也只抓一次屏
TEST_F(StepRecorderCoordinatorFixture, NewEventReplacesPendingOne)
{
    auto coordinator = makeCoordinator();
    coordinator->toggleRecording();
    m_source->emitClick(QPoint(100, 100));
    m_source->emitClick(QPoint(300, 200));
    coordinator->flushPendingForTesting();

    ASSERT_EQ(coordinator->stepCount(), 1);
    EXPECT_EQ(coordinator->sessionForTesting()->steps().front().globalLogicalPoint, QPoint(300, 200));
    EXPECT_EQ(m_capture->calls, 1);
}

/// 右键点击保留按键标记与前缀
TEST_F(StepRecorderCoordinatorFixture, RightClickKeepsButtonFlag)
{
    auto coordinator = makeCoordinator();
    coordinator->toggleRecording();
    m_source->emitClick(kClickPoint, true);
    coordinator->flushPendingForTesting();

    ASSERT_EQ(coordinator->stepCount(), 1);
    const auto &step = coordinator->sessionForTesting()->steps().front();
    EXPECT_TRUE(step.rightButton);
    EXPECT_TRUE(step.description.startsWith(QStringLiteral("右键")));
}

/// 界面元素读不出来时照样记录，说明回落到坐标
TEST_F(StepRecorderCoordinatorFixture, ElementLookupFailureStillRecords)
{
    m_platform->uiElementFails = true;
    auto coordinator           = makeCoordinator();
    coordinator->toggleRecording();
    m_source->emitClick(kClickPoint);
    coordinator->flushPendingForTesting();

    ASSERT_EQ(coordinator->stepCount(), 1);
    const auto &step = coordinator->sessionForTesting()->steps().front();
    EXPECT_TRUE(step.elementName.isEmpty());
    EXPECT_TRUE(step.description.contains(QStringLiteral("点击（")));
    EXPECT_EQ(step.windowTitle, QStringLiteral("Demo"));
}

/// 句柄为 0 的点击不在提交时补前台窗口：延迟里刚切过去的窗口不算这一步
TEST_F(StepRecorderCoordinatorFixture, HandlelessClickKeepsArrivalWindowInfo)
{
    m_platform->uiElementFails  = true;
    m_platform->foregroundFails = true;
    auto coordinator            = makeCoordinator();
    coordinator->toggleRecording();

    m_source->emitClick(kClickPoint);
    // 到达时前台查不到；延迟窗口内换成另一个窗口，提交时再问就会张冠李戴
    m_platform->foregroundFails = false;
    m_platform->m_foreground    = ForegroundWindow{8, QStringLiteral("Other"), QStringLiteral("other.exe")};
    coordinator->flushPendingForTesting();

    ASSERT_EQ(coordinator->stepCount(), 1);
    const auto &step = coordinator->sessionForTesting()->steps().front();
    EXPECT_TRUE(step.windowTitle.isEmpty()) << "句柄为 0 的点击不该用提交时的前台窗口补标题";
    EXPECT_TRUE(step.processName.isEmpty());
    EXPECT_FALSE(step.description.contains(QStringLiteral("Other")));
}

/// 析构摘钩子并删掉会话临时目录
TEST_F(StepRecorderCoordinatorFixture, DestructorStopsSourceAndCleansTemp)
{
    std::filesystem::path sessionDirectory;
    {
        auto coordinator = makeCoordinator();
        coordinator->toggleRecording();
        m_source->emitClick(kClickPoint);
        coordinator->flushPendingForTesting();
        ASSERT_EQ(coordinator->stepCount(), 1);
        sessionDirectory = coordinator->sessionForTesting()->tempDirectory();
        ASSERT_TRUE(std::filesystem::exists(sessionDirectory));
        EXPECT_EQ(*m_stopSignals, 0);
    }

    EXPECT_GE(*m_stopSignals, 1) << "析构要先停事件源再释放它";
    EXPECT_FALSE(std::filesystem::exists(sessionDirectory));
}

/// 记满上限自动停止并提示
TEST_F(StepRecorderCoordinatorFixture, MaximumStepsStopsRecording)
{
    auto       coordinator = makeCoordinator();
    QSignalSpy messageSpy{coordinator.get(), &StepRecorderCoordinator::messageForUser};
    QSignalSpy stateSpy{coordinator.get(), &StepRecorderCoordinator::stateChanged};
    QSignalSpy stoppedSpy{coordinator.get(), &StepRecorderCoordinator::stopped};
    coordinator->toggleRecording();
    // 抓屏一律失败：这里只判步数上限，不把两百张 PNG 写进临时目录
    m_capture->failAlways = true;

    for (int index = 0; index < kMaximumSteps; ++index)
    {
        // 相邻两下至少错开 8 像素，避开合并
        m_source->emitClick(QPoint(10 + (index % 80) * 8, 50 + (index / 80) * 40));
        coordinator->flushPendingForTesting();
        if (!coordinator->isRecording())
        {
            break;
        }
    }

    EXPECT_EQ(coordinator->stepCount(), kMaximumSteps);
    EXPECT_FALSE(coordinator->isRecording());
    EXPECT_FALSE(m_source->isRunning());
    EXPECT_FALSE(coordinator->hudForTesting()->isVisible());
    ASSERT_GE(messageSpy.count(), 1);
    EXPECT_TRUE(messageSpy.last().at(0).toString().contains(QStringLiteral("200")));

    ASSERT_EQ(stoppedSpy.count(), 1) << "记满上限也走正常停止";
    EXPECT_EQ(stoppedSpy.at(0).at(0).toInt(), kMaximumSteps);
    EXPECT_FALSE(stateSpy.last().at(0).toBool());
    EXPECT_EQ(stateSpy.last().at(2).toInt(), kMaximumSteps);
}

/// 继续录音时钩子装不回来就结束本次记录并提示，已记的步骤保留
TEST_F(StepRecorderCoordinatorFixture, PauseResumeFailureEndsRecording)
{
    auto       coordinator = makeCoordinator();
    QSignalSpy messageSpy{coordinator.get(), &StepRecorderCoordinator::messageForUser};
    QSignalSpy stoppedSpy{coordinator.get(), &StepRecorderCoordinator::stopped};
    coordinator->toggleRecording();
    m_source->emitClick(kClickPoint);
    coordinator->flushPendingForTesting();
    ASSERT_EQ(coordinator->stepCount(), 1);

    coordinator->togglePauseForTesting();
    m_source->failNextStart = true;
    coordinator->togglePauseForTesting();

    EXPECT_FALSE(coordinator->isRecording());
    EXPECT_FALSE(coordinator->isPaused());
    EXPECT_FALSE(m_source->isRunning());
    EXPECT_FALSE(coordinator->hudForTesting()->isVisible());
    EXPECT_EQ(coordinator->stepCount(), 1);
    ASSERT_GE(messageSpy.count(), 1);
    EXPECT_TRUE(messageSpy.last().at(0).toString().contains(QStringLiteral("钩子安装失败")));
    ASSERT_EQ(stoppedSpy.count(), 1) << "继续失败也按正常停止收场";
    EXPECT_EQ(stoppedSpy.at(0).at(0).toInt(), 1);
}

/// 运行期故障：停止、清掉临时目录、收起控制条并告诉用户
TEST_F(StepRecorderCoordinatorFixture, SourceFailureCleansSessionAndHud)
{
    auto       coordinator = makeCoordinator();
    QSignalSpy messageSpy{coordinator.get(), &StepRecorderCoordinator::messageForUser};
    QSignalSpy stoppedSpy{coordinator.get(), &StepRecorderCoordinator::stopped};
    coordinator->toggleRecording();
    m_source->emitClick(kClickPoint);
    coordinator->flushPendingForTesting();
    ASSERT_EQ(coordinator->stepCount(), 1);

    const auto sessionDirectory = coordinator->sessionForTesting()->tempDirectory();
    ASSERT_FALSE(sessionDirectory.empty());
    ASSERT_TRUE(std::filesystem::exists(sessionDirectory));

    m_source->emitFailure(QStringLiteral("全局钩子被系统摘除"));

    EXPECT_FALSE(coordinator->isRecording());
    EXPECT_FALSE(m_source->isRunning());
    EXPECT_FALSE(coordinator->hudForTesting()->isVisible());
    EXPECT_EQ(coordinator->stepCount(), 0);
    EXPECT_FALSE(std::filesystem::exists(sessionDirectory));
    ASSERT_GE(messageSpy.count(), 1);
    EXPECT_TRUE(messageSpy.last().at(0).toString().contains(QStringLiteral("全局钩子被系统摘除")));
    EXPECT_EQ(stoppedSpy.count(), 0) << "故障不等同正常停止：会话已清，不能进审核";
}
