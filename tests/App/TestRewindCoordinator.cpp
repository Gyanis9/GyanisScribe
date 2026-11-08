#include "App/RewindCoordinator.h"

#include "Core/Capture/IScreenCaptureSource.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"
#include "Gui/Controls/Toast.h"
#include "Platform/IPlatformIntegration.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMetaType>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <vector>

Q_DECLARE_METATYPE(GyanisScribe::Base::CapturedImage)

using GyanisScribe::App::RewindCoordinator;
using GyanisScribe::Base::CapturedImage;
using GyanisScribe::Base::Error;
using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Base::Expected;
using GyanisScribe::Core::IScreenCaptureSource;
using GyanisScribe::Core::kRewindEnabled;
using GyanisScribe::Core::kRewindExcludeProcesses;
using GyanisScribe::Core::RewindStatus;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Gui::ToastKind;
using GyanisScribe::Platform::ForegroundWindow;
using GyanisScribe::Platform::IPlatformIntegration;
using GyanisScribe::Platform::PlatformCapabilities;
using GyanisScribe::Platform::ScreenWindow;

namespace
{
    CapturedImage makeFrame(const QColor &color)
    {
        CapturedImage frame;
        frame.image = QImage(64, 36, QImage::Format_ARGB32_Premultiplied);
        frame.image.fill(color);
        frame.logicalSize      = QSize(64, 36);
        frame.devicePixelRatio = 1.0;
        const auto *screen     = QGuiApplication::primaryScreen();
        frame.screenName       = screen != nullptr ? screen->name() : QStringLiteral("TestScreen");
        frame.logicalRegion    = screen != nullptr ? screen->geometry() : QRect(0, 0, 640, 360);
        return frame;
    }

    class FakeSource final : public IScreenCaptureSource
    {
    public:
        [[nodiscard]] Expected<CapturedImage> grabScreen(const QString &) const override
        {
            return makeFrame(Qt::white);
        }

        [[nodiscard]] Expected<CapturedImage> tryGrabScreen(const QString &) const override
        {
            ++calls;
            if (failing)
            {
                return std::unexpected(Error{ErrorCode::ScreenUnavailable, "测试替身：抓帧失败"});
            }
            return makeFrame(calls % 2 == 0 ? Qt::white : Qt::black);
        }

        [[nodiscard]] Expected<CapturedImage> grabWindow(quintptr) const override
        {
            return makeFrame(Qt::white);
        }

        mutable int  calls   = 0;
        mutable bool failing = false;
    };

    class FakePlatform final : public IPlatformIntegration
    {
    public:
        FakePlatform()
        {
            caps.sessionNotifications = true;
            caps.windowEnumeration    = true;
        }

        [[nodiscard]] PlatformCapabilities capabilities() const noexcept override
        {
            return caps;
        }
        [[nodiscard]] QString displayName() const override
        {
            return QStringLiteral("Fake");
        }
        [[nodiscard]] std::unique_ptr<GyanisScribe::Platform::IHotkeyBackend> createHotkeyBackend() override
        {
            return {};
        }
        [[nodiscard]] Expected<ForegroundWindow> foregroundWindow() const override
        {
            return ForegroundWindow{1, QStringLiteral("Editor")};
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
            return windows;
        }
        [[nodiscard]] Expected<std::vector<QRect>> elementChainAtPoint(const QPoint &, const std::vector<quintptr> &) const override
        {
            return std::vector<QRect>{};
        }

        PlatformCapabilities              caps;
        mutable std::vector<ScreenWindow> windows;
    };

    /// 托盘状态信号的一帧读数
    struct StatusSignal
    {
        bool    enabled = false;
        bool    paused  = false;
        QString reason;
    };

    std::optional<StatusSignal> lastStatus(const QSignalSpy &spy)
    {
        if (spy.isEmpty())
        {
            return std::nullopt;
        }
        const auto arguments = spy.last();
        return StatusSignal{arguments.at(0).toBool(), arguments.at(1).toBool(), arguments.at(2).toString()};
    }

    QWidget *findPickerWindow()
    {
        for (auto *widget: QApplication::topLevelWidgets())
        {
            if (widget->objectName() == QStringLiteral("RewindPickerWindow"))
            {
                return widget;
            }
        }
        return nullptr;
    }

    std::unique_ptr<QApplication> application;

    class RewindCoordinatorFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // 协调器会建选择器窗口，跑到离屏平台上，测试不在桌面上闪，也与并行用例互不抢前台
            qputenv("QT_QPA_PLATFORM", "offscreen");
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestRewindCoordinator";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
            QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
            QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe-Tests"));
            qRegisterMetaType<GyanisScribe::Base::CapturedImage>("GyanisScribe::Base::CapturedImage");
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            const auto caseName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            m_iniFile           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-RewindCoordinator-{}.ini", caseName);
            std::error_code ignored;
            std::filesystem::remove(m_iniFile, ignored);
        }

        std::filesystem::path m_iniFile;
    };
} // namespace

TEST_F(RewindCoordinatorFixture, DisabledDoesNotCapture)
{
    SettingsStore     settings(m_iniFile);
    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});

    coordinator.captureTickForTesting();
    EXPECT_EQ(source.calls, 0);
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Disabled);
}

TEST_F(RewindCoordinatorFixture, EnabledCapturesAndEncodes)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    coordinator.start();
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);
    coordinator.captureTickForTesting();

    QTRY_VERIFY_WITH_TIMEOUT(source.calls > 0, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount > 0, 3000);
}

TEST_F(RewindCoordinatorFixture, ExcludedWindowPausesAndResumes)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());
    ASSERT_TRUE(settings.setValue(kRewindExcludeProcesses, QStringLiteral("keepass.exe")).has_value());

    FakeSource   source;
    FakePlatform platform;
    platform.windows.push_back(ScreenWindow{1, QStringLiteral("Vault"), QStringLiteral("keepass.exe"), QRect(0, 0, 100, 100), QRect(0, 0, 100, 100)});
    RewindCoordinator coordinator({source, platform, settings});
    coordinator.start();
    coordinator.captureTickForTesting();
    EXPECT_EQ(source.calls, 0);
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::PausedByExclusion);

    platform.windows.clear();
    // 排除暂停期间定时器不停：程序消失后无需用户操作，下一次 tick 自己恢复抓帧
    QTRY_VERIFY_WITH_TIMEOUT(source.calls > 0, 3000);
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount > 0, 3000);
}

TEST_F(RewindCoordinatorFixture, LockClearsAndUnlockRestarts)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    coordinator.start();
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount > 0, 3000);

    emit platform.sessionLocked();
    EXPECT_EQ(coordinator.bufferStatsForTesting().sampleCount, 0);
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::PausedByLock);

    const int callsBeforeUnlock = source.calls;
    emit      platform.sessionUnlocked();
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);
    // 解锁后定时器重新起跑，不用再按一次热键
    QTRY_VERIFY_WITH_TIMEOUT(source.calls > callsBeforeUnlock, 3000);
}

TEST_F(RewindCoordinatorFixture, ChooseFrameEmitsCapturedImage)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    coordinator.start();
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount > 0, 3000);

    const QSignalSpy statusSpy(&coordinator, &RewindCoordinator::bufferStatusChanged);
    QSignalSpy       chosen(&coordinator, &RewindCoordinator::frameChosen);
    const auto       id = coordinator.firstFrameIdForTesting();
    ASSERT_TRUE(id.has_value());
    coordinator.chooseFrameForTesting(*id);
    QTRY_VERIFY_WITH_TIMEOUT(chosen.count() == 1, 3000);
    EXPECT_FALSE(chosen.first().at(0).value<CapturedImage>().image.isNull());

    const auto captureFlow = lastStatus(statusSpy);
    ASSERT_TRUE(captureFlow.has_value());
    EXPECT_TRUE(captureFlow->enabled);
    EXPECT_TRUE(captureFlow->paused);

    // 框选/标注进行中再按热键只提示，不打开第二层流程
    QStringList messages;
    QObject::connect(&coordinator, &RewindCoordinator::messageForUser, &coordinator, [&messages](const QString &message, ToastKind) { messages.append(message); });
    coordinator.showPicker();
    ASSERT_EQ(messages.size(), 1);
    EXPECT_TRUE(messages.front().contains(QStringLiteral("先完成当前截图或标注")));

    coordinator.handleCaptureFlowFinished();
    const auto resumed = lastStatus(statusSpy);
    ASSERT_TRUE(resumed.has_value());
    EXPECT_TRUE(resumed->enabled);
    EXPECT_FALSE(resumed->paused);

    // 不在框选流程里时重复回调不再改状态
    const int statusCount = statusSpy.count();
    coordinator.handleCaptureFlowFinished();
    EXPECT_EQ(statusSpy.count(), statusCount);
}

TEST_F(RewindCoordinatorFixture, StartWithoutSessionNotificationsStaysDisabled)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource   source;
    FakePlatform platform;
    platform.caps.sessionNotifications = false;
    QStringList       messages;
    RewindCoordinator coordinator({source, platform, settings});
    QObject::connect(&coordinator, &RewindCoordinator::messageForUser, &coordinator, [&messages](const QString &message, ToastKind) { messages.append(message); });

    coordinator.start();
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Disabled);
    coordinator.captureTickForTesting();
    EXPECT_EQ(source.calls, 0);
    ASSERT_EQ(messages.size(), 1);
    EXPECT_TRUE(messages.front().contains(QStringLiteral("锁屏通知")));
}

TEST_F(RewindCoordinatorFixture, InvalidExclusionKeepsDisabledAndExplains)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());
    ASSERT_TRUE(settings.setValue(kRewindExcludeProcesses, QStringLiteral("C:\\Tools\\keepass.exe")).has_value());

    FakeSource        source;
    FakePlatform      platform;
    QStringList       messages;
    RewindCoordinator coordinator({source, platform, settings});
    QObject::connect(&coordinator, &RewindCoordinator::messageForUser, &coordinator, [&messages](const QString &message, ToastKind) { messages.append(message); });

    coordinator.start();
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Disabled);
    coordinator.captureTickForTesting();
    EXPECT_EQ(source.calls, 0);
    ASSERT_FALSE(messages.isEmpty());
    EXPECT_TRUE(messages.front().contains(QStringLiteral("排除名单")));
}

TEST_F(RewindCoordinatorFixture, ConsecutiveGrabFailuresPauseWithError)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    QStringList       messages;
    RewindCoordinator coordinator({source, platform, settings});
    QObject::connect(&coordinator, &RewindCoordinator::messageForUser, &coordinator, [&messages](const QString &message, ToastKind) { messages.append(message); });
    coordinator.start();
    source.failing = true;

    coordinator.captureTickForTesting();
    coordinator.captureTickForTesting();
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);
    coordinator.captureTickForTesting();
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::PausedByError);
    EXPECT_EQ(source.calls, 3);
    ASSERT_EQ(messages.size(), 1);
    EXPECT_TRUE(messages.front().contains(QStringLiteral("抓帧失败")));

    // 暂停之后不再抓帧，直到用户重新开启
    coordinator.captureTickForTesting();
    EXPECT_EQ(source.calls, 3);
}

TEST_F(RewindCoordinatorFixture, ShowPickerExplainsDisabledAndEmptyBuffer)
{
    SettingsStore     settings(m_iniFile);
    FakeSource        source;
    FakePlatform      platform;
    QStringList       messages;
    RewindCoordinator coordinator({source, platform, settings});
    QObject::connect(&coordinator, &RewindCoordinator::messageForUser, &coordinator, [&messages](const QString &message, ToastKind) { messages.append(message); });

    coordinator.showPicker();
    ASSERT_EQ(messages.size(), 1);
    EXPECT_TRUE(messages.front().contains(QStringLiteral("回溯截图未开启")));

    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());
    coordinator.showPicker();
    ASSERT_EQ(messages.size(), 2);
    EXPECT_TRUE(messages.back().contains(QStringLiteral("还没有画面")));
    EXPECT_EQ(findPickerWindow(), nullptr);
}

TEST_F(RewindCoordinatorFixture, PickerOpenPausesAndDismissResumes)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    coordinator.start();
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount > 0, 3000);

    const QSignalSpy statusSpy(&coordinator, &RewindCoordinator::bufferStatusChanged);
    coordinator.showPicker();
    // 编码在跑时选择器会等它完成（最多 300ms），所以这里等窗口真出现
    QTRY_VERIFY_WITH_TIMEOUT(findPickerWindow() != nullptr, 3000);
    const auto opened = lastStatus(statusSpy);
    ASSERT_TRUE(opened.has_value());
    EXPECT_TRUE(opened->paused);

    auto *picker = findPickerWindow();
    ASSERT_NE(picker, nullptr);
    EXPECT_TRUE(picker->isVisible());

    // 选择器开着时再按热键只是抬到最前，不重复建窗
    coordinator.showPicker();
    EXPECT_EQ(findPickerWindow(), picker);

    // 选择器开着不抓帧
    const int callsWhileOpen = source.calls;
    coordinator.captureTickForTesting();
    EXPECT_EQ(source.calls, callsWhileOpen);

    picker->close();
    QTRY_VERIFY_WITH_TIMEOUT(statusSpy.count() > 0 && !lastStatus(statusSpy)->paused, 3000);
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);
}

TEST_F(RewindCoordinatorFixture, ClearBufferKeepsBufferingAndPauseState)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    coordinator.start();
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount > 0, 3000);

    const QSignalSpy statusSpy(&coordinator, &RewindCoordinator::bufferStatusChanged);
    coordinator.clearBuffer();
    EXPECT_EQ(coordinator.bufferStatsForTesting().sampleCount, 0);
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);
    const auto cleared = lastStatus(statusSpy);
    ASSERT_TRUE(cleared.has_value());
    EXPECT_TRUE(cleared->enabled);
    EXPECT_FALSE(cleared->paused);

    // 开启状态下清空不改变开关：新帧继续进来
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount > 0, 3000);

    emit platform.sessionLocked();
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::PausedByLock);
    coordinator.clearBuffer();
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::PausedByLock);

    coordinator.stop();
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Disabled);
    coordinator.clearBuffer();
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Disabled);
    EXPECT_EQ(coordinator.bufferStatsForTesting().sampleCount, 0);
}

TEST_F(RewindCoordinatorFixture, SettingsDisableStopsAndInvalidReloadDoesNotStart)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    QStringList       messages;
    RewindCoordinator coordinator({source, platform, settings});
    QObject::connect(&coordinator, &RewindCoordinator::messageForUser, &coordinator, [&messages](const QString &message, ToastKind) { messages.append(message); });
    coordinator.start();
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount > 0, 3000);

    // 设置里关掉即时生效：停止抓帧、清空缓冲
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(false)).has_value());
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Disabled);
    EXPECT_EQ(coordinator.bufferStatsForTesting().sampleCount, 0);
    const int callsAfterDisable = source.calls;
    coordinator.captureTickForTesting();
    EXPECT_EQ(source.calls, callsAfterDisable);

    // 非法排除名单：给出原因，不启动
    ASSERT_TRUE(settings.setValue(kRewindExcludeProcesses, QStringLiteral("C:\\Tools\\keepass.exe")).has_value());
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Disabled);
    ASSERT_FALSE(messages.isEmpty());
    EXPECT_TRUE(messages.back().contains(QStringLiteral("排除名单")));
    coordinator.captureTickForTesting();
    EXPECT_EQ(source.calls, callsAfterDisable);
}
