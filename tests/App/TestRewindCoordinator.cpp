#include "App/RewindCoordinator.h"

#include "Base/Logger.h"
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
using GyanisScribe::Core::kRewindBufferSeconds;
using GyanisScribe::Core::kRewindEnabled;
using GyanisScribe::Core::kRewindExcludeProcesses;
using GyanisScribe::Core::kRewindFrameRate;
using GyanisScribe::Core::RewindStatus;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Gui::ToastKind;
using GyanisScribe::Platform::ForegroundWindow;
using GyanisScribe::Platform::IPlatformIntegration;
using GyanisScribe::Platform::PlatformCapabilities;
using GyanisScribe::Platform::ScreenWindow;

namespace
{
    CapturedImage makeFrame(const QColor &color, const QSize &size = QSize(64, 36))
    {
        CapturedImage frame;
        frame.image = QImage(size, QImage::Format_ARGB32_Premultiplied);
        frame.image.fill(color);
        frame.logicalSize      = size;
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
            return makeFrame(Qt::white, frameSize);
        }

        [[nodiscard]] Expected<CapturedImage> tryGrabScreen(const QString &) const override
        {
            ++calls;
            if (failing)
            {
                return std::unexpected(Error{ErrorCode::ScreenUnavailable, "测试替身：抓帧失败"});
            }
            if (nullImage)
            {
                // 抓帧成功但图是空的：专供编码失败那条路
                auto frame  = makeFrame(Qt::white, frameSize);
                frame.image = QImage{};
                return frame;
            }
            if (constantFrame)
            {
                return makeFrame(Qt::white, frameSize);
            }
            return makeFrame(calls % 2 == 0 ? Qt::white : Qt::black, frameSize);
        }

        [[nodiscard]] Expected<CapturedImage> grabWindow(quintptr) const override
        {
            return makeFrame(Qt::white, frameSize);
        }

        mutable int   calls         = 0;
        mutable bool  failing       = false;
        mutable bool  nullImage     = false;
        mutable bool  constantFrame = false;
        mutable QSize frameSize{64, 36};
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
            // 日志只留错误级并落到临时目录：用例判的是状态与信号，控制台上一行行 info 会把结果冲散
            const auto logDirectory = std::filesystem::temp_directory_path() / "GyanisScribe-RewindCoordinator-logs";
            static_cast<void>(GyanisScribe::Base::Logger::initialize(logDirectory, GyanisScribe::Base::Severity::Error));
        }

        static void TearDownTestSuite()
        {
            GyanisScribe::Base::Logger::shutdown();
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
    const auto sampleCapturedAt = coordinator.sampleCapturedAtForTesting(*id);
    ASSERT_TRUE(sampleCapturedAt.isValid()) << "样本自己没有抓帧时刻，后面的对照没有基准";
    coordinator.chooseFrameForTesting(*id);
    QTRY_VERIFY_WITH_TIMEOUT(chosen.count() == 1, 3000);
    const auto chosenFrame = chosen.first().at(0).value<CapturedImage>();
    EXPECT_FALSE(chosenFrame.image.isNull());
    EXPECT_EQ(chosenFrame.capturedAt, sampleCapturedAt) << "选帧交回的 capturedAt 不是样本那一格：横幅会显示解码时刻而不是抓帧时刻";

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

TEST_F(RewindCoordinatorFixture, DisableStopsEvenWhenExclusionListIsInvalid)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    coordinator.start();
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount > 0, 3000);

    // 名单先坏掉：重载会报错但不该停掉已在跑的缓冲
    ASSERT_TRUE(settings.setValue(kRewindExcludeProcesses, QStringLiteral("C:\\Tools\\keepass.exe")).has_value());
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);
    EXPECT_GT(coordinator.bufferStatsForTesting().sampleCount, 0);

    // 关开关必须压过解析失败：先读原始开关，缓冲清空、定时器停下
    const int callsBeforeDisable = source.calls;
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(false)).has_value());
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Disabled) << "名单坏掉时关开关没生效：缓冲还在抓";
    EXPECT_EQ(coordinator.bufferStatsForTesting().sampleCount, 0) << "关掉之后旧画面还留在缓冲里";
    coordinator.captureTickForTesting();
    EXPECT_EQ(source.calls, callsBeforeDisable) << "已关闭却仍抓到新帧";
}

TEST_F(RewindCoordinatorFixture, EncodeFailurePausesAndResetsPendingFrame)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    QStringList       messages;
    RewindCoordinator coordinator({source, platform, settings});
    QObject::connect(&coordinator, &RewindCoordinator::messageForUser, &coordinator, [&messages](const QString &message, ToastKind) { messages.append(message); });
    coordinator.start();
    // 抓帧成功但图是空的：编码器拒收空图，正好稳定复现编码失败
    source.nullImage = true;

    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.consecutiveEncodeFailuresForTesting() == 1, 3000);
    // 失败回接后必须丢掉在途帧，否则后面两次 tick 都提交不了新编码，也就到不了三连败
    EXPECT_FALSE(coordinator.encodePendingForTesting());
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);

    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.consecutiveEncodeFailuresForTesting() == 2, 3000);
    EXPECT_FALSE(coordinator.encodePendingForTesting());

    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.statusForTesting() == RewindStatus::PausedByError, 3000);
    EXPECT_FALSE(coordinator.encodePendingForTesting());
    EXPECT_EQ(coordinator.bufferStatsForTesting().sampleCount, 0);
    ASSERT_EQ(messages.size(), 1);
    EXPECT_TRUE(messages.front().contains(QStringLiteral("编码失败")));

    // 错误暂停后不再抓帧，直到用户重新开启
    const int callsWhenPaused = source.calls;
    coordinator.captureTickForTesting();
    EXPECT_EQ(source.calls, callsWhenPaused);
}

TEST_F(RewindCoordinatorFixture, RunningExclusionReloadPausesAndResumes)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    coordinator.start();
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount > 0, 3000);

    // 名单是运行中才写进设置的：重载后下一次 tick 就要按它暂停
    ASSERT_TRUE(settings.setValue(kRewindExcludeProcesses, QStringLiteral("keepass.exe")).has_value());
    platform.windows.push_back(ScreenWindow{1, QStringLiteral("Vault"), QStringLiteral("keepass.exe"), QRect(0, 0, 100, 100), QRect(0, 0, 100, 100)});
    coordinator.captureTickForTesting();
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::PausedByExclusion);

    const int callsWhilePaused = source.calls;
    coordinator.captureTickForTesting();
    EXPECT_EQ(source.calls, callsWhilePaused);

    // 程序消失、名单也清空之后，下一次 tick 自己恢复抓帧
    platform.windows.clear();
    ASSERT_TRUE(settings.setValue(kRewindExcludeProcesses, QVariant{}).has_value());
    coordinator.captureTickForTesting();
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);
    EXPECT_GT(source.calls, callsWhilePaused);
}

TEST_F(RewindCoordinatorFixture, HeartbeatReusesEncodedBytesAndResetsEncodeFailures)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    coordinator.start();
    // 画面一直不变：第二拍只该追加心跳样本
    source.constantFrame = true;
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount == 1, 3000);
    const auto afterFirst = coordinator.bufferStatsForTesting();
    ASSERT_GT(afterFirst.uniquePixelBytes, 0);

    // 中间夹一次编码失败，用来验证心跳会把失败计数清零
    source.nullImage = true;
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.consecutiveEncodeFailuresForTesting() == 1, 3000);

    source.nullImage = false;
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount == 2, 3000);
    const auto afterSecond = coordinator.bufferStatsForTesting();
    EXPECT_EQ(afterSecond.uniquePixelBytes, afterFirst.uniquePixelBytes);
    // 两次失败之间隔着一段静止画面，不该凑成连续失败
    EXPECT_EQ(coordinator.consecutiveEncodeFailuresForTesting(), 0);
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);
}

TEST_F(RewindCoordinatorFixture, PickerWaitsForEncoderThenShowsNewestFrame)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    coordinator.start();
    // 大帧让第二帧的编码确实还在跑
    source.frameSize = QSize(1920, 1080);
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount == 1, 3000);

    coordinator.captureTickForTesting();
    ASSERT_TRUE(coordinator.encodePendingForTesting());

    // 编码在飞时热键要等它追加完再开（上限 300ms），这样选择器里能看到刚发生的一帧
    coordinator.showPicker();
    QTRY_VERIFY_WITH_TIMEOUT(findPickerWindow() != nullptr && findPickerWindow()->isVisible(), 3000);
    EXPECT_FALSE(coordinator.encodePendingForTesting());
    EXPECT_EQ(coordinator.bufferStatsForTesting().sampleCount, 2);
}

TEST_F(RewindCoordinatorFixture, PickerWaitTimeoutOpensWithExistingFrames)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    // 把 300ms 上限压到 0：不必真的等一次慢编码也能走到超时分支
    coordinator.setPickerWaitForTesting(0);
    coordinator.start();
    // 大帧让第二帧的编码确实还在跑
    source.frameSize = QSize(1920, 1080);
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount == 1, 3000);

    coordinator.captureTickForTesting();
    ASSERT_TRUE(coordinator.encodePendingForTesting());

    coordinator.showPicker();
    QTRY_VERIFY_WITH_TIMEOUT(findPickerWindow() != nullptr && findPickerWindow()->isVisible(), 3000);
    // 超时先开：窗口出现时那一帧还在编码，选择器用的是已有样本
    EXPECT_TRUE(coordinator.encodePendingForTesting());
    EXPECT_GE(coordinator.bufferStatsForTesting().sampleCount, 1);
}

TEST_F(RewindCoordinatorFixture, TickWhileEncodePendingIsDropped)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    // 2560×1440 的 PNG 编码耗时不短，第二拍一定落在编码还没回接的窗口里；
    // 再大的帧只会拖慢用例，更小的帧则要靠 finished 事件尚未投递这一点，不如大帧直白
    source.frameSize = QSize(2560, 1440);
    coordinator.start();
    ASSERT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);

    coordinator.captureTickForTesting();
    ASSERT_EQ(source.calls, 1);
    ASSERT_TRUE(coordinator.encodePendingForTesting());

    // 两拍之间不跑事件循环：编码的 finished 事件不可能回接，第二拍必须被在途守卫挡在抓帧之前
    coordinator.captureTickForTesting();
    EXPECT_EQ(source.calls, 1);
    EXPECT_TRUE(coordinator.encodePendingForTesting());

    // 收尾：把这一帧等进缓冲，不把在途 future 留给下一条用例
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount >= 1, 3000);
}

TEST_F(RewindCoordinatorFixture, HotkeyWhileDecodingExplainsInsteadOfRaisingHiddenPicker)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    coordinator.start();
    // 大帧让解码确实要花几毫秒，热键才落在解码进行中
    source.frameSize = QSize(1920, 1080);
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount == 1, 3000);

    coordinator.showPicker();
    QTRY_VERIFY_WITH_TIMEOUT(findPickerWindow() != nullptr && findPickerWindow()->isVisible(), 3000);

    QStringList messages;
    QObject::connect(&coordinator, &RewindCoordinator::messageForUser, &coordinator, [&messages](const QString &message, ToastKind) { messages.append(message); });
    QSignalSpy chosen(&coordinator, &RewindCoordinator::frameChosen);
    const auto id = coordinator.firstFrameIdForTesting();
    ASSERT_TRUE(id.has_value());
    coordinator.chooseFrameForTesting(*id);

    // 解码期间选择器是收起的：此时热键不能去抬一个看不见的窗口
    coordinator.showPicker();
    ASSERT_EQ(messages.size(), 1);
    EXPECT_TRUE(messages.front().contains(QStringLiteral("正在解码")));

    QTRY_VERIFY_WITH_TIMEOUT(chosen.count() == 1, 3000);
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);
}

TEST_F(RewindCoordinatorFixture, DecodeFailureReopensPickerAndExplains)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    coordinator.start();
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount == 1, 3000);

    const auto corruptId = coordinator.injectCorruptFrameForTesting();
    ASSERT_NE(corruptId, 0U);

    coordinator.showPicker();
    QTRY_VERIFY_WITH_TIMEOUT(findPickerWindow() != nullptr && findPickerWindow()->isVisible(), 3000);

    const QSignalSpy statusSpy(&coordinator, &RewindCoordinator::bufferStatusChanged);
    QStringList      messages;
    QObject::connect(&coordinator, &RewindCoordinator::messageForUser, &coordinator, [&messages](const QString &message, ToastKind) { messages.append(message); });
    coordinator.chooseFrameForTesting(corruptId);

    QTRY_VERIFY_WITH_TIMEOUT(messages.size() == 1, 3000);
    EXPECT_TRUE(messages.front().contains(QStringLiteral("解码失败")));
    // 失败不能静默收起：选择器要摆回来，缓冲仍是暂停态
    auto *picker = findPickerWindow();
    ASSERT_NE(picker, nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(picker->isVisible(), 3000);
    const auto reopened = lastStatus(statusSpy);
    ASSERT_TRUE(reopened.has_value());
    EXPECT_TRUE(reopened->enabled);
    EXPECT_TRUE(reopened->paused);
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);
}

TEST_F(RewindCoordinatorFixture, TopologyChangeDropsBufferedAndInFlightFrames)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    coordinator.start();
    coordinator.captureTickForTesting();
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount == 1, 3000);

    // 布局变化时还有一帧在编码：它属于旧屏，必须跟着旧缓冲一起作废
    source.frameSize = QSize(1920, 1080);
    coordinator.captureTickForTesting();
    ASSERT_TRUE(coordinator.encodePendingForTesting());

    emit qGuiApp->screenAdded(QGuiApplication::primaryScreen());
    EXPECT_EQ(coordinator.bufferStatsForTesting().sampleCount, 0);
    EXPECT_FALSE(coordinator.encodePendingForTesting());
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);

    // 锁屏把定时器停掉，之后只看那一帧的结果会不会把样本带回来
    emit platform.sessionLocked();
    QTest::qWait(400);
    EXPECT_EQ(coordinator.bufferStatsForTesting().sampleCount, 0);
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::PausedByLock);
}

TEST_F(RewindCoordinatorFixture, LiveDurationAndFrameRateReload)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(kRewindEnabled, QVariant(true)).has_value());

    FakeSource        source;
    FakePlatform      platform;
    RewindCoordinator coordinator({source, platform, settings});
    coordinator.start();
    EXPECT_EQ(coordinator.configForTesting().bufferSeconds, 10);
    EXPECT_EQ(coordinator.configForTesting().frameRate, 5);
    EXPECT_TRUE(coordinator.configForTesting().enabled);

    const QSignalSpy statusSpy(&coordinator, &RewindCoordinator::bufferStatusChanged);
    ASSERT_TRUE(settings.setValue(kRewindBufferSeconds, QVariant(20)).has_value());
    EXPECT_EQ(coordinator.configForTesting().bufferSeconds, 20);
    EXPECT_EQ(coordinator.configForTesting().frameRate, 5);

    ASSERT_TRUE(settings.setValue(kRewindFrameRate, QVariant(8)).has_value());
    EXPECT_EQ(coordinator.configForTesting().frameRate, 8);
    EXPECT_EQ(coordinator.configForTesting().bufferSeconds, 20);

    // 运行中重载只换容量与节拍，不该顺手暂停或重启
    EXPECT_EQ(coordinator.statusForTesting(), RewindStatus::Buffering);
    const auto reloaded = lastStatus(statusSpy);
    ASSERT_TRUE(reloaded.has_value());
    EXPECT_TRUE(reloaded->enabled);
    EXPECT_FALSE(reloaded->paused);
}

TEST_F(RewindCoordinatorFixture, DismissFromErrorPauseKeepsPauseWithoutRepeatingMessage)
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
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.bufferStatsForTesting().sampleCount == 1, 3000);

    // 连续抓帧失败进错误暂停，缓冲里还留着一帧，热键仍能打开选择器
    source.failing = true;
    coordinator.captureTickForTesting();
    coordinator.captureTickForTesting();
    coordinator.captureTickForTesting();
    ASSERT_EQ(coordinator.statusForTesting(), RewindStatus::PausedByError);
    ASSERT_EQ(messages.size(), 1);

    coordinator.showPicker();
    QTRY_VERIFY_WITH_TIMEOUT(findPickerWindow() != nullptr && findPickerWindow()->isVisible(), 3000);
    findPickerWindow()->close();

    // 取消选择器不该顺手解除错误暂停，也不该把同一条原因再弹一遍
    QTRY_VERIFY_WITH_TIMEOUT(coordinator.statusForTesting() == RewindStatus::PausedByError, 3000);
    EXPECT_EQ(messages.size(), 1);
}
