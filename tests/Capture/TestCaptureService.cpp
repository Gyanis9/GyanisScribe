#include "Capture/CaptureService.h"

#include "Capture/CaptureOverlay.h"
#include "Capture/IScreenCaptureSource.h"
#include "Base/Logger.h"
#include "Platform/IPlatformIntegration.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QString>
#include <QVariant>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using GyanisScribe::Capture::CaptureOverlay;
using GyanisScribe::Capture::CaptureService;
using GyanisScribe::Capture::ICaptureService;
using GyanisScribe::Capture::IScreenCaptureSource;
using GyanisScribe::Base::CapturedImage;
using GyanisScribe::Base::Error;
using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Base::Expected;
using GyanisScribe::Settings::SettingsStore;

namespace
{
    // 一帧 200×100 的假图，逻辑 100×50、dpr 2：与 ScreenUtils 那批用例同一套取值
    CapturedImage makeFrame()
    {
        CapturedImage frame;
        frame.image = QImage(200, 100, QImage::Format_ARGB32_Premultiplied);
        frame.image.fill(QColor(255, 0, 0));
        frame.image.setDevicePixelRatio(1.0);
        frame.devicePixelRatio = 2.0;
        frame.logicalSize      = QSize(100, 50);
        frame.screenName       = QStringLiteral("FakeScreen-1");
        return frame;
    }

    /// 替身照抄生产端的入参形状：抓屏要屏幕名、抓窗口要句柄
    class FakeCaptureSource : public IScreenCaptureSource
    {
    public:
        [[nodiscard]] Expected<CapturedImage> grabScreen(const QString &screenName) const override
        {
            requestedScreens.push_back(screenName);
            // 队列非空就按帧交：滚动长截图要的是「同一块区域连着几张不同的」，
            // 队列空了退回那份固定帧，其余用例的行为一个字都不变
            if (!screenQueue.empty())
            {
                const auto next = screenQueue.front();
                screenQueue.erase(screenQueue.begin());
                return next;
            }
            return screenResult;
        }

        [[nodiscard]] Expected<CapturedImage> grabWindow(quintptr windowHandle) const override
        {
            requestedWindows.push_back(windowHandle);
            return windowResult;
        }

        /// 替身照抄生产端的入参形状：取指针要同一份屏名，落点才算在同一块屏上
        [[nodiscard]] std::optional<GyanisScribe::Capture::CursorSprite> grabCursor(const QString &screenName) const override
        {
            requestedCursorScreens.push_back(screenName);
            return cursorSprite;
        }

        mutable std::vector<CapturedImage>                 screenQueue;
        mutable std::vector<QString>                       requestedScreens;
        mutable std::vector<QString>                       requestedCursorScreens;
        mutable std::vector<quintptr>                      requestedWindows;
        Expected<CapturedImage>                            screenResult{makeFrame()};
        Expected<CapturedImage>                            windowResult{makeFrame()};
        std::optional<GyanisScribe::Capture::CursorSprite> cursorSprite;
    };

    std::unique_ptr<QApplication> application;

    /// 服务现在要问平台「屏上有哪些窗口」；这几条用例不碰框选，用真的平台对象就够
    std::unique_ptr<GyanisScribe::Platform::IPlatformIntegration> platform;

    class CaptureServiceFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestCaptureService";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
            QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
            QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe-Tests"));
            platform = GyanisScribe::Platform::createPlatformIntegration();
        }

        static void TearDownTestSuite()
        {
            platform.reset();
            application.reset();
        }

        void SetUp() override
        {
            const auto caseName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            m_iniFile           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Capture-{}.ini", caseName);
            std::error_code ignored;
            std::filesystem::remove(m_iniFile, ignored);
        }

        /// 收集一次服务调用产出的信号
        struct Outcome
        {
            std::optional<CapturedImage> image;
            std::optional<Error>         error;
            int                          cancellations = 0;
        };

        void wire(CaptureService &service, Outcome &outcome) const
        {
            // 先断开上一轮的连接：同一条测试里会分几个块各 wire 一次，而 outcome 是块级对象。
            // 留着旧连接就是让信号往已析构的栈对象里写——ASan 在容器里报的
            // stack-use-after-scope 正是这个（第三条块的 emit 会踩前两块的 Outcome）
            QObject::disconnect(&service, &ICaptureService::captured, nullptr, nullptr);
            QObject::disconnect(&service, &ICaptureService::captureFailed, nullptr, nullptr);
            QObject::disconnect(&service, &ICaptureService::captureCancelled, nullptr, nullptr);

            // 上下文给 service：连接随它一起析构，不会留下比测试活得更久的挂接
            QObject::connect(&service, &ICaptureService::captured, &service, [&outcome](const CapturedImage &image) { outcome.image = image; });
            QObject::connect(&service, &ICaptureService::captureFailed, &service, [&outcome](const Error &error) { outcome.error = error; });
            QObject::connect(&service, &ICaptureService::captureCancelled, &service, [&outcome] { ++outcome.cancellations; });
        }

        std::filesystem::path m_iniFile;
    };
} // namespace

/**
 * @brief 钉住整屏抓取：屏幕名原样交下去，抓到的帧从 captured 信号出来
 */
TEST_F(CaptureServiceFixture, FullScreenCaptureEmitsTheFrame)
{
    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    CaptureService    service(source, settings, *platform);
    Outcome           outcome;
    wire(service, outcome);

    service.captureFullScreen();

    ASSERT_TRUE(outcome.image.has_value()) << "整屏抓取没产出图像，错误：" << (outcome.error ? outcome.error->message : std::string{});
    EXPECT_EQ(outcome.image->image.width(), 200);
    EXPECT_EQ(outcome.image->screenName, QStringLiteral("FakeScreen-1"));
    ASSERT_EQ(source.requestedScreens.size(), 1U);
    EXPECT_TRUE(source.requestedScreens[0].isEmpty()) << "不填屏名时必须交空串下去，让来源自己按光标所在屏走";
}

/**
 * @brief 钉住区域抓取成功后的两件事：裁出的帧与「上次区域」记账
 */
TEST_F(CaptureServiceFixture, RegionCaptureRemembersTheRegion)
{
    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    CaptureService    service(source, settings, *platform);
    Outcome           outcome;
    wire(service, outcome);

    service.captureRegion(QRect(10, 10, 40, 15), QStringLiteral("FakeScreen-1"));

    ASSERT_TRUE(outcome.image.has_value()) << "选区抓取没产出图像，错误：" << (outcome.error ? outcome.error->message : std::string{});
    EXPECT_EQ(outcome.image->image.width(), 80);
    EXPECT_EQ(outcome.image->image.height(), 30);
    EXPECT_EQ(outcome.image->logicalSize, (QSize(40, 15)));
    EXPECT_EQ(source.requestedScreens[0], QStringLiteral("FakeScreen-1")) << "指定的屏名要原样传下去";

    settings.sync();
    SettingsStore reopened(m_iniFile);
    EXPECT_EQ(reopened.stringValue(GyanisScribe::Settings::kCaptureLastRegion, QString()), QStringLiteral("10,10,40,15")) << "成功抓过的选区没记下，「上次区域」就永远复用不了";
    EXPECT_EQ(reopened.stringValue(GyanisScribe::Settings::kCaptureLastScreen, QString()), QStringLiteral("FakeScreen-1")) << "所在屏没记下，多显示器下上次区域会回到错的那一块屏";
    EXPECT_EQ(reopened.stringValue(GyanisScribe::Settings::kCaptureRegionHistory, QString()), QStringLiteral("10,10,40,15,FakeScreen-1"))
            << "选区历史没记下，框选时的 R/Shift+R 就没有可挑的";
}

/**
 * @brief 钉住就地重抓：裁好的帧带着区域交回，但不出 captured、不记选区
 * @details 刷新截图（标注窗 F5）走这条：它不是一次新截图，不该触发「截图完成」那一串反应
 *          （音效、自动保存、意图分派），也不该往最近选区里再记一遍——同一块区域记两次，
 *          R/Shift+R 的历史里就会冒出重复项。区域字段随裁剪一起写进帧：重抓的人靠它认识这块地方。
 */
TEST_F(CaptureServiceFixture, GrabRegionNowReturnsTheFrameWithoutEmittingOrRemembering)
{
    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    CaptureService    service(source, settings, *platform);

    // 先用一次正常抓取把「上次区域」写成一条已知值，之后就看重抓有没有去动它
    service.captureRegion(QRect(5, 5, 20, 10), QStringLiteral("FakeScreen-1"));

    Outcome outcome;
    wire(service, outcome);
    const auto frame = service.grabRegionNow(QRect(10, 10, 40, 15), QStringLiteral("FakeScreen-1"));

    ASSERT_TRUE(frame.has_value()) << (frame ? std::string{} : frame.error().message);
    EXPECT_EQ(frame->image.width(), 80);
    EXPECT_EQ(frame->logicalSize, (QSize(40, 15)));
    EXPECT_EQ(frame->screenName, QStringLiteral("FakeScreen-1"));
    EXPECT_EQ(frame->logicalRegion, (QRect(10, 10, 40, 15))) << "重抓的帧没带上区域，下一次刷新就不知道重抓哪";
    EXPECT_FALSE(outcome.image.has_value()) << "重抓不该出 captured：那会把刷新当新截图，音效与自动保存各来一遍";

    settings.sync();
    SettingsStore reopened(m_iniFile);
    EXPECT_EQ(reopened.stringValue(GyanisScribe::Settings::kCaptureLastRegion, QString()), QStringLiteral("5,5,20,10")) << "重抓把最近选区顶掉了";
    EXPECT_EQ(reopened.stringValue(GyanisScribe::Settings::kCaptureRegionHistory, QString()), QStringLiteral("5,5,20,10,FakeScreen-1")) << "重抓往选区历史里记了账";
}

/**
 * @brief 钉住历史按最近在前排，且「重复上次选区」取的是历史首条
 * @details 两本账（历史与单值键）由同一个写点保成同一条：这里两处各验一次，
 *          分开写就会出现「重复上次选区」与 R/Shift+R 说的不是同一块的局面。
 */
TEST_F(CaptureServiceFixture, RegionHistoryKeepsNewestFirstAndLastRegionReusesTheHead)
{
    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    CaptureService    service(source, settings, *platform);

    service.captureRegion(QRect(10, 10, 40, 15), QStringLiteral("FakeScreen-1"));
    service.captureRegion(QRect(50, 5, 30, 20), QStringLiteral("FakeScreen-1"));

    settings.sync();
    SettingsStore reopened(m_iniFile);
    EXPECT_EQ(reopened.stringValue(GyanisScribe::Settings::kCaptureRegionHistory, QString()), QStringLiteral("50,5,30,20,FakeScreen-1;10,10,40,15,FakeScreen-1"))
            << "历史该按最近在前排，两条都在";

    Outcome outcome;
    wire(service, outcome);
    service.captureLastRegion();
    ASSERT_TRUE(outcome.image.has_value()) << "复用上次区域应当抓出一帧，错误：" << (outcome.error ? outcome.error->message : std::string{});
    EXPECT_EQ(outcome.image->logicalSize, (QSize(30, 20))) << "复用的不是最近那一条";
}

/**
 * @brief 钉住失败不顶掉记账：坏选区不能把用户上次的好选区擦掉
 */
TEST_F(CaptureServiceFixture, FailedRegionKeepsPreviouslyRememberedRegion)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureLastRegion, QVariant(QStringLiteral("1,2,3,4"))).has_value());
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureLastScreen, QVariant(QStringLiteral("FakeScreen-1"))).has_value());
    settings.sync();

    FakeCaptureSource source;
    CaptureService    service(source, settings, *platform);
    Outcome           outcome;
    wire(service, outcome);

    // 逻辑 (60,10,80,20) 在 dpr 2 上 → 物理 (120,20,160,40)，超出 200 宽
    service.captureRegion(QRect(60, 10, 80, 20), QStringLiteral("FakeScreen-1"));

    ASSERT_TRUE(outcome.error.has_value()) << "越界选区必须失败，而不是裁出一张窄图";
    EXPECT_EQ(outcome.error->code, ErrorCode::ScreenUnavailable);
    EXPECT_FALSE(outcome.image.has_value()) << "失败的同时又发成功信号，控制器会拿到两条互相矛盾的收尾";

    settings.sync();
    SettingsStore reopened(m_iniFile);
    EXPECT_EQ(reopened.stringValue(GyanisScribe::Settings::kCaptureLastRegion, QString()), QStringLiteral("1,2,3,4")) << "一次失败的抓取把上次的好选区顶掉了";
}

/**
 * @brief 钉住「上次区域」复用的完整回路与两个拒绝面
 */
TEST_F(CaptureServiceFixture, LastRegionReusesStoredRegionOrFails)
{
    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    CaptureService    service(source, settings, *platform);

    // 没记过时：拒绝并给替代做法
    {
        Outcome outcome;
        wire(service, outcome);
        service.captureLastRegion();
        ASSERT_TRUE(outcome.error.has_value()) << "没有上次区域时必须失败";
        EXPECT_EQ(outcome.error->code, ErrorCode::InvalidArgument);
        EXPECT_NE(outcome.error->message.find("先框选截图一次"), std::string::npos) << "文案要给出下一步做什么";
    }

    // 记下过之后：按记下的选区与所在屏重抓
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureLastRegion, QVariant(QStringLiteral("10,10,40,15"))).has_value());
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureLastScreen, QVariant(QStringLiteral("FakeScreen-1"))).has_value());
    {
        Outcome outcome;
        wire(service, outcome);
        service.captureLastRegion();
        ASSERT_TRUE(outcome.image.has_value()) << "复用上次区域应当抓出一帧";
        EXPECT_EQ(outcome.image->image.width(), 80);
        EXPECT_EQ(source.requestedScreens.back(), QStringLiteral("FakeScreen-1")) << "没有回到记下的那块屏";
    }

    // 记的文本坏了：拒绝，且不猜成任意一块屏。两本账各验一次——
    // 历史键是现在的真源；升级前留下的单值键只在历史为空时才被回退读，坏文本同样不许补全
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureRegionHistory, QVariant(QStringLiteral("10,20,30"))).has_value());
    {
        Outcome outcome;
        wire(service, outcome);
        service.captureLastRegion();
        ASSERT_TRUE(outcome.error.has_value()) << "三段文本不该被补成四段";
        EXPECT_EQ(outcome.error->code, ErrorCode::InvalidArgument);
    }

    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureRegionHistory, QVariant(QString())).has_value());
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureLastRegion, QVariant(QStringLiteral("10,20,30"))).has_value());
    {
        Outcome outcome;
        wire(service, outcome);
        service.captureLastRegion();
        ASSERT_TRUE(outcome.error.has_value()) << "只剩旧键时，坏文本仍不该被补成四段";
        EXPECT_EQ(outcome.error->code, ErrorCode::InvalidArgument);
    }
}

/**
 * @brief 钉住窗口抓取：句柄原样交下去，来源的失败原因照搬给调用方
 */
TEST_F(CaptureServiceFixture, WindowCaptureForwardsHandleAndSourceFailure)
{
    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    source.windowResult = std::unexpected(Error{ErrorCode::PlatformUnsupported, "本平台抓不出窗口像素。请改用区域截图框住那个窗口"});
    CaptureService service(source, settings, *platform);
    Outcome        outcome;
    wire(service, outcome);

    service.captureWindow(0x1234);

    ASSERT_EQ(source.requestedWindows.size(), 1U);
    EXPECT_EQ(source.requestedWindows[0], static_cast<quintptr>(0x1234)) << "句柄没原样传下去，抓的就不是那个窗口";
    ASSERT_TRUE(outcome.error.has_value());
    EXPECT_EQ(outcome.error->code, ErrorCode::PlatformUnsupported);
    EXPECT_NE(outcome.error->message.find("改用区域截图"), std::string::npos) << "降级建议要跟着原因一起交回";
    EXPECT_FALSE(outcome.image.has_value());
}

/**
 * @brief 钉住失败一定进日志：所有错误必须有日志，这条要能在日志里查到
 */
TEST_F(CaptureServiceFixture, FailuresAreWrittenToLog)
{
    const auto      caseName     = std::string_view(::testing::UnitTest::GetInstance()->current_test_info()->name());
    const auto      logDirectory = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Capture-{}-logs", caseName);
    std::error_code ignored;
    std::filesystem::remove_all(logDirectory, ignored);
    ASSERT_TRUE(GyanisScribe::Base::Logger::initialize(logDirectory).has_value());

    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    source.screenResult = std::unexpected(Error{ErrorCode::ScreenUnavailable, "找不到名为「FakeScreen-9」的屏幕"});
    CaptureService service(source, settings, *platform);
    Outcome        outcome;
    wire(service, outcome);

    service.captureFullScreen(QStringLiteral("FakeScreen-9"));
    ASSERT_TRUE(outcome.error.has_value());

    // 先把 logger 收掉再去读文件：sink 是带缓冲的，边写边读就把「这行到没到盘上」交给了调度
    GyanisScribe::Base::Logger::shutdown();

    std::ifstream     input(logDirectory / "GyanisScribe.log", std::ios::binary);
    const std::string logText{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    EXPECT_NE(logText.find("截图失败"), std::string::npos) << "失败只发了信号没进日志，事后无从查起";
    EXPECT_NE(logText.find("找不到名为"), std::string::npos) << "日志里没带上原因文本";
}

/**
 * @brief 造一帧可辨认的整屏图：物理 width×height、逻辑一半、dpr 由入参给
 * @details 每帧涂一种颜色，接缝与「接没接上这一段」都能从像素颜色上直接看出来
 */
[[nodiscard]] static CapturedImage makeScreenFrame(const QSize &physical, const qreal devicePixelRatio, const QColor &color,
                                                   const QString &screenName = QStringLiteral("FakeScreen-1"))
{
    CapturedImage frame;
    frame.image = QImage{physical, QImage::Format_ARGB32_Premultiplied};
    frame.image.fill(color);
    frame.image.setDevicePixelRatio(1.0);
    frame.devicePixelRatio = devicePixelRatio;
    frame.logicalSize      = QSize{static_cast<int>(physical.width() / devicePixelRatio), static_cast<int>(physical.height() / devicePixelRatio)};
    frame.screenName       = screenName;
    return frame;
}

/**
 * @brief 钉住滚动长截图的整条流程：起一轮、逐帧接上、收口时交出一张自洽的长图
 * @details 判据不走计时器（那是赌调度），用例自己按帧调 grabNextLongFrame。
 *          收口交出的那份形状必须与普通截图同解：物理像素、逻辑尺寸、缩放因子、屏名四样互指同一个点，
 *          下游的标注与贴图全按这一套算，错一样就是成品图与界面对不上。
 */
TEST_F(CaptureServiceFixture, LongCaptureStitchesEachGrabbedFrame)
{
    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    // 三帧同宽同缩放、内容各不相同：对不上接缝，于是每一帧都整段接在下面
    source.screenQueue = {
            makeScreenFrame(QSize{200, 100}, 2.0, QColor{10, 200, 20}),
            makeScreenFrame(QSize{200, 100}, 2.0, QColor{200, 20, 20}),
            makeScreenFrame(QSize{200, 100}, 2.0, QColor{20, 20, 200}),
    };
    CaptureService service{source, settings, *platform};

    struct Watch
    {
        std::optional<CapturedImage> image;
        std::optional<Error>         error;
        int                          cancellations = 0;
    } watch;

    QObject::connect(&service, &ICaptureService::captured, &service, [&watch](const CapturedImage &image) { watch.image = image; });
    QObject::connect(&service, &ICaptureService::captureFailed, &service, [&watch](const Error &error) { watch.error = error; });
    QObject::connect(&service, &ICaptureService::captureCancelled, &service, [&watch] { ++watch.cancellations; });
    service.captureLongRegion(QRect{0, 0, 50, 25});
    EXPECT_FALSE(watch.error.has_value()) << "起一轮就把原因交回来了";
    EXPECT_FALSE(watch.image.has_value()) << "还没收口就交了成品";

    service.grabNextLongFrame();
    service.grabNextLongFrame();
    EXPECT_FALSE(watch.image.has_value()) << "接帧途中就把半成品当成品交了";

    service.finishLongCapture();
    ASSERT_TRUE(watch.image.has_value()) << "收口没交出长图";
    EXPECT_EQ(watch.image->image.size(), QSize(100, 150)) << "长图的物理尺寸不对";
    EXPECT_EQ(watch.image->logicalSize, QSize(50, 75)) << "逻辑尺寸没跟着长高";
    EXPECT_EQ(watch.image->devicePixelRatio, 2.0) << "缩放因子丢了";
    EXPECT_EQ(watch.image->screenName, QStringLiteral("FakeScreen-1")) << "来源屏丢了";

    // 收口之后再按「完成」不该再交第二次成品：一轮只有一个结果
    const auto delivered = watch.image->image.size();
    service.finishLongCapture();
    EXPECT_EQ(watch.image->image.size(), delivered) << "一轮交出了两次成品";
}

/**
 * @brief 钉住接不上的那一帧会把原因交回来，而不是静默少接一段
 * @details 中途换屏或改缩放时宽度对不上；继续装作在接就等于把用户的页面截了一半还不吭声。
 */
TEST_F(CaptureServiceFixture, LongCaptureReportsAFrameItCannotAttach)
{
    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    source.screenQueue = {
            makeScreenFrame(QSize{200, 100}, 2.0, QColor{10, 200, 20}),
            makeScreenFrame(QSize{200, 100}, 4.0, QColor{200, 20, 20}), // 缩放变了：裁出来的宽度对不上
    };
    CaptureService service{source, settings, *platform};

    std::optional<CapturedImage> finished;
    std::optional<Error>         failure;
    QObject::connect(&service, &ICaptureService::captured, &service, [&finished](const CapturedImage &image) { finished = image; });
    QObject::connect(&service, &ICaptureService::captureFailed, &service, [&failure](const Error &error) { failure = error; });

    service.captureLongRegion(QRect{0, 0, 50, 25});
    service.grabNextLongFrame();

    EXPECT_FALSE(finished.has_value()) << "接不上却把半成品当成品交了";
    ASSERT_TRUE(failure.has_value()) << "接不上却没报出任何原因";
    EXPECT_FALSE(failure->message.empty()) << "原因要能读：用户看不出是哪一帧换了比例";

    // 收口之后计时器那一支也该停下：再调一次不产出任何新结果
    const auto reported = *failure;
    service.grabNextLongFrame();
    EXPECT_EQ(failure->message, reported.message) << "这一轮已经死了还在重复报错";
}

/**
 * @brief 钉住取消这一轮只作废结果，不动「上次选区」那笔账
 * @details 选区在起轮时就已经记下（与单张截图同一条走法），取消扔掉的是这一轮的成品图；
 *          把账也一起退回去会让「重复上次选区」莫名其妙地失效。
 */
TEST_F(CaptureServiceFixture, CancellingLongCaptureDiscardsTheImageOnly)
{
    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    source.screenQueue = {makeScreenFrame(QSize{200, 100}, 2.0, QColor{10, 200, 20})};
    CaptureService service{source, settings, *platform};

    std::optional<CapturedImage> finished;
    int                          cancellations = 0;
    QObject::connect(&service, &ICaptureService::captured, &service, [&finished](const CapturedImage &image) { finished = image; });
    QObject::connect(&service, &ICaptureService::captureCancelled, &service, [&cancellations] { ++cancellations; });

    service.captureLongRegion(QRect{10, 5, 40, 20});
    service.grabNextLongFrame();
    service.cancelLongCapture();

    EXPECT_FALSE(finished.has_value()) << "取消之后还交了成品";
    EXPECT_EQ(cancellations, 1) << "取消没让这一轮可观察地收掉";
    EXPECT_EQ(settings.stringValue(GyanisScribe::Settings::kCaptureLastRegion, QString()), QStringLiteral("10,5,40,20")) << "取消把选区账也退掉了";
}

/**
 * @brief 钉住「这一帧根本没抓到」也当场报出来，而不是停在那里不吭声
 * @details 屏幕被拔掉、或者那块屏的名字在这一轮之间变了，抓帧会直接失败。
 *          此时控制条还在屏上摆着，用户看得见它不涨；没有原因信号就只剩一句「没反应」。
 */
TEST_F(CaptureServiceFixture, LongCaptureReportsAGrabThatNeverHappened)
{
    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    source.screenQueue = {makeScreenFrame(QSize{200, 100}, 2.0, QColor{10, 200, 20})};
    CaptureService service{source, settings, *platform};

    std::optional<CapturedImage> finished;
    std::optional<Error>         failure;
    QObject::connect(&service, &ICaptureService::captured, &service, [&finished](const CapturedImage &image) { finished = image; });
    QObject::connect(&service, &ICaptureService::captureFailed, &service, [&failure](const Error &error) { failure = error; });

    service.captureLongRegion(QRect{0, 0, 50, 25});
    // 第二帧起抓不到屏了：替身按队列给完就退回固定那份，这里显式改成失败
    source.screenResult = std::unexpected(Error{ErrorCode::ScreenUnavailable, "找不到名为「FakeScreen-1」的屏幕"});
    service.grabNextLongFrame();

    EXPECT_FALSE(finished.has_value()) << "抓不到帧却把半成品当成品交了";
    ASSERT_TRUE(failure.has_value()) << "抓不到帧没有报出原因";
    EXPECT_NE(failure->message.find("找不到名为"), std::string::npos) << "原因要把是哪块屏丢了写出来";

    // 报错之后这一轮就该结束：再按一次不许又冒出一份结果
    const auto reported = failure->message;
    service.grabNextLongFrame();
    EXPECT_EQ(failure->message, reported) << "这一轮已经死了还在重复报错";
}

/**
 * @brief 钉住「长截图还在接时再起一轮」被当场拒掉，而且不把手里那张一起丢掉
 * @details 第二层遮罩一铺，接了一半的那一轮既没交成品也没作废：用户取消遮罩之后会发现
 *          刚才滚的十几屏不见了。拒的那一句要给出路（先按完成或取消），并且这一轮仍然活着。
 */
TEST_F(CaptureServiceFixture, StartingAnotherCaptureWhileALongRoundRunsIsRefused)
{
    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    source.screenQueue = {makeScreenFrame(QSize{200, 100}, 2.0, QColor{10, 200, 20})};
    CaptureService service{source, settings, *platform};

    std::optional<CapturedImage> finished;
    std::optional<Error>         failure;
    QObject::connect(&service, &ICaptureService::captured, &service, [&finished](const CapturedImage &image) { finished = image; });
    QObject::connect(&service, &ICaptureService::captureFailed, &service, [&failure](const Error &error) { failure = error; });

    service.captureLongRegion(QRect{0, 0, 50, 25});
    service.grabNextLongFrame();

    service.startInteractive();
    ASSERT_TRUE(failure.has_value()) << "第二次框选没被拒，还在接的那一轮就被顶掉了";
    EXPECT_NE(failure->message.find("还在接"), std::string::npos) << "拒的时候要给出路：先按完成或取消";

    service.finishLongCapture();
    ASSERT_TRUE(finished.has_value()) << "被拒的那一次顺手把长截图的结果也丢了";
    // 队列里只备了一帧，第二帧退回替身那份固定的红图：内容不同 → 整段接上，长图从 50 行变 100 行
    EXPECT_EQ(finished->image.size(), QSize(100, 100)) << "被拒之后这一轮还能接着接，接出来的高度才对得上";
}

namespace
{
    /// 一枚 8×8 的实心绿指针：落点与画没画都从像素颜色上直接看得出来
    GyanisScribe::Capture::CursorSprite makeGreenSprite(const QPoint &position)
    {
        GyanisScribe::Capture::CursorSprite sprite;
        sprite.image = QImage(8, 8, QImage::Format_ARGB32_Premultiplied);
        sprite.image.fill(QColor(0, 255, 0));
        sprite.hotspot  = QPoint(0, 0);
        sprite.position = position;
        return sprite;
    }
} // namespace

/**
 * @brief 钉住「含指针」开关的三档：默认不画、开着才画、画完不改来源那帧
 * @details 默认档必须与开关无关地不动像素——区域截图刚松手时指针就在选区里，
 *
 * 默认画进去会让每个人的成品都多出一只箭头。开关开着时落点按帧自己的缩放因子算：
 *          逻辑 (10,10) 在 dpr 2 的帧上是物理 (20,20)。

 */
TEST_F(CaptureServiceFixture, CursorIsDrawnOnlyWhenTheSettingIsOn)
{
    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    source.cursorSprite = makeGreenSprite(QPoint(10, 10));

    {
        CaptureService service(source, settings, *platform);
        Outcome        outcome;
        wire(service, outcome);
        service.captureFullScreen();
        ASSERT_TRUE(outcome.image.has_value());
        EXPECT_EQ(outcome.image->image.pixelColor(20, 20), QColor(255, 0, 0)) << "没打开开关就画了指针，默认档会把箭头塞进每个人的截图";
        EXPECT_TRUE(source.requestedCursorScreens.empty()) << "开关关着时连形状都不该去问，白跑一次平台调用";
    }

    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureIncludeCursor, QVariant(true)).has_value());
    {
        CaptureService service(source, settings, *platform);
        Outcome        outcome;
        wire(service, outcome);
        service.captureFullScreen();
        ASSERT_TRUE(outcome.image.has_value());
        EXPECT_EQ(outcome.image->image.pixelColor(20, 20), QColor(0, 255, 0));
        EXPECT_EQ(outcome.image->image.pixelColor(27, 27), QColor(0, 255, 0));
        EXPECT_EQ(outcome.image->image.pixelColor(28, 28), QColor(255, 0, 0)) << "绿块铺过了 8×8，形状被放大了";
        ASSERT_EQ(source.requestedCursorScreens.size(), 1U);
        EXPECT_EQ(source.requestedCursorScreens[0], QStringLiteral("FakeScreen-1")) << "落点要折回抓帧用的那块屏，屏名必须同源";
    }

    // 来源那帧不能被上一轮就地改花：替身返回的是同一个 Expected，画歪一次就会留色给后面所有用例
    {
        CaptureService service(source, settings, *platform);
        Outcome        outcome;
        wire(service, outcome);
        service.captureFullScreen();
        ASSERT_TRUE(outcome.image.has_value());
        EXPECT_EQ(outcome.image->image.pixelColor(20, 20), QColor(0, 255, 0));
    }
}

/**
 * @brief 钉住区域截图先把指针画进整帧再裁
 * @details 顺序颠倒就会拿整帧坐标去裁选区：含指针的那一小块要么丢失，要么凭空多出在选区角落。
 *
 * 两条对照一起跑——框住指针的必须有绿，没框住的必须一格绿都没有。
 */
TEST_F(CaptureServiceFixture, RegionCropKeepsCursorOnlyWhereItFalls)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureIncludeCursor, QVariant(true)).has_value());
    FakeCaptureSource source;
    source.cursorSprite = makeGreenSprite(QPoint(10, 10));

    {
        CaptureService service(source, settings, *platform);
        Outcome        outcome;
        wire(service, outcome);
        // 选区 (10,10,40,15) 在 dpr 2 上是物理 (20,20)-(100,50)：指针尖端正好落在裁出图的原点
        service.captureRegion(QRect(10, 10, 40, 15), QStringLiteral("FakeScreen-1"));
        ASSERT_TRUE(outcome.image.has_value());
        EXPECT_EQ(outcome.image->image.pixelColor(0, 0), QColor(0, 255, 0)) << "指针在选区里却没画进来，合成与裁剪的顺序错了";
    }

    {
        CaptureService service(source, settings, *platform);
        Outcome        outcome;
        wire(service, outcome);
        service.captureRegion(QRect(60, 40, 20, 10), QStringLiteral("FakeScreen-1"));
        ASSERT_TRUE(outcome.image.has_value());
        for (int y = 0; y < outcome.image->image.height(); ++y)
        {
            for (int x = 0; x < outcome.image->image.width(); ++x)
            {
                EXPECT_EQ(outcome.image->image.pixelColor(x, y), QColor(255, 0, 0)) << "选区外 (60,40) 的指针被画进了这块选区";
            }
        }
    }
}

/**
 * @brief 钉住取不到形状时照常出图，并在日志里说明这次不含指针
 * @details 指针隐藏、或平台根本不是 Windows 时，来源交回空值。这时必须安静地出一张没指针的图，
 *
 * 而不是失败——也不能按「含了」交下去让用户以为箭头在。
 */
TEST_F(CaptureServiceFixture, MissingCursorShapeStillDeliversAFrameAndSaysSo)
{
    const auto      caseName     = std::string_view(::testing::UnitTest::GetInstance()->current_test_info()->name());
    const auto      logDirectory = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Capture-{}-logs", caseName);
    std::error_code ignored;
    std::filesystem::remove_all(logDirectory, ignored);
    ASSERT_TRUE(GyanisScribe::Base::Logger::initialize(logDirectory).has_value());

    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureIncludeCursor, QVariant(true)).has_value());

    FakeCaptureSource source; // cursorSprite 留空，等于本平台取不到形状
    CaptureService    service(source, settings, *platform);
    Outcome           outcome;
    wire(service, outcome);

    service.captureFullScreen();
    ASSERT_TRUE(outcome.image.has_value()) << "取不到指针形状不该把整次截图判失败";
    EXPECT_EQ(outcome.image->image.pixelColor(20, 20), QColor(255, 0, 0));

    // 先收 logger 再读文件：sink 带缓冲，边写边读就把「这行到没到盘上」交给了调度
    GyanisScribe::Base::Logger::shutdown();

    std::ifstream     input(logDirectory / "GyanisScribe.log", std::ios::binary);
    const std::string logText{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    EXPECT_NE(logText.find("取不到指针的形状"), std::string::npos) << "降级没写进日志，用户只能对着没有箭头的图猜";
}

/**
 * @brief 钉住滚动长截图的取帧一律不带指针
 * @details 长图是按帧接出来的：每一帧都画一枚指针，接完就是一串排到页底的箭头。
 *
 * 这一条把「设置开着、形状也给得出来」的前提摆足，再证明接出来的长图一格绿都没有。
 */
TEST_F(CaptureServiceFixture, LongCaptureFramesNeverCarryTheCursor)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureIncludeCursor, QVariant(true)).has_value());

    FakeCaptureSource source;
    source.cursorSprite = makeGreenSprite(QPoint(10, 10));
    source.screenQueue  = {makeFrame(), makeFrame()};

    CaptureService service(source, settings, *platform);
    Outcome        outcome;
    wire(service, outcome);

    service.captureLongRegion(QRect(0, 0, 50, 25));
    service.grabNextLongFrame();
    service.grabNextLongFrame();
    service.finishLongCapture();

    ASSERT_TRUE(outcome.image.has_value());
    EXPECT_TRUE(source.requestedCursorScreens.empty()) << "长截图去问了指针形状，接出来的长图会带一串箭头";
    for (int y = 0; y < outcome.image->image.height(); ++y)
    {
        for (int x = 0; x < outcome.image->image.width(); ++x)
        {
            EXPECT_EQ(outcome.image->image.pixelColor(x, y), QColor(255, 0, 0)) << "长图里混进了指针色";
        }
    }
}

namespace
{
    /// 替身照抄生产端的契约形状：能力位决定接线，问询记录用来核对「问了哪个点、排除了谁」
    class FakePlatformIntegration : public GyanisScribe::Platform::IPlatformIntegration
    {
    public:
        [[nodiscard]] GyanisScribe::Platform::PlatformCapabilities capabilities() const noexcept override
        {
            return capabilitiesValue;
        }

        [[nodiscard]] QString displayName() const override
        {
            return QStringLiteral("替身");
        }

        [[nodiscard]] std::unique_ptr<GyanisScribe::Platform::IHotkeyBackend> createHotkeyBackend() override
        {
            return nullptr;
        }

        [[nodiscard]] Expected<GyanisScribe::Platform::ForegroundWindow> foregroundWindow() const override
        {
            return std::unexpected(Error{ErrorCode::PlatformUnsupported, "替身不给前台窗口"});
        }

        [[nodiscard]] Expected<bool> autostartEnabled() const override
        {
            return std::unexpected(Error{ErrorCode::PlatformUnsupported, "替身不管开机启动"});
        }

        [[nodiscard]] Expected<void> setAutostart(bool enabled) override
        {
            static_cast<void>(enabled);
            return std::unexpected(Error{ErrorCode::PlatformUnsupported, "替身不管开机启动"});
        }

        [[nodiscard]] Expected<std::vector<GyanisScribe::Platform::ScreenWindow>> screenWindows() const override
        {
            // 默认仍是「没有窗口可列」：判据要验候选筛选时才往 windows 里填几张
            return windows;
        }

        [[nodiscard]] Expected<std::vector<QRect>> elementChainAtPoint(const QPoint &globalLogicalPoint, const std::vector<quintptr> &excludeWindowHandles) const override
        {
            queriedPoints.push_back(globalLogicalPoint);
            excludedHandles = excludeWindowHandles;
            return elementChain;
        }

        GyanisScribe::Platform::PlatformCapabilities      capabilitiesValue;
        std::vector<GyanisScribe::Platform::ScreenWindow> windows; ///< 这一轮让平台「列」出来的窗口，默认空
        Expected<std::vector<QRect>>                      elementChain{std::vector<QRect>{}};
        mutable std::vector<QPoint>                       queriedPoints;
        mutable std::vector<quintptr>                     excludedHandles;
    };

    /// 找这一轮铺出来的遮罩：它是一张顶层控件，按类型认出来
    [[nodiscard]] CaptureOverlay *findOverlay()
    {
        for (auto *widget: QApplication::topLevelWidgets())
        {
            if (auto *overlay = qobject_cast<CaptureOverlay *>(widget))
            {
                return overlay;
            }
        }
        return nullptr;
    }
} // namespace

/**
 * @brief 钉住悬停接线：遮罩报点、服务问平台、链送回遮罩，排除名单里带着自家遮罩窗
 * @details 三处各钉一步：没接线就没人问（用户看到元素检测没生效）；排除名单漏了自家遮罩，
 *          UIA 命中会认到盖在全屏上的遮罩自己（本机探针实测，置顶工具窗也挡不住命中）；
 *          链没送回遮罩，悬停就退回窗口候选。同一个点连报两次只问一次——每次问都是一串跨进程调用。
 */
TEST_F(CaptureServiceFixture, HoverAsksThePlatformAndFeedsTheChainBackToTheOverlay)
{
    if (QGuiApplication::screens().isEmpty())
    {
        GTEST_SKIP() << "这个环境没有屏幕可枚举，铺不出遮罩";
    }

    SettingsStore           settings(m_iniFile);
    FakeCaptureSource       source;
    FakePlatformIntegration fake;
    fake.capabilitiesValue.uiElements = true;
    CaptureService service(source, settings, fake);

    service.startInteractive();
    auto *overlay = findOverlay();
    ASSERT_NE(overlay, nullptr) << "开始框选之后没铺出遮罩";

    const auto localPoint  = QRect(QPoint{0, 0}, overlay->size()).center();
    const auto globalPoint = overlay->geometry().topLeft() + localPoint;
    const auto localDeep   = QRect{localPoint.x() - 10, localPoint.y() - 10, 20, 20};
    const auto localWide   = QRect{localPoint.x() - 100, localPoint.y() - 100, 300, 300};
    fake.elementChain      = std::vector<QRect>{localDeep.translated(overlay->geometry().topLeft()), localWide.translated(overlay->geometry().topLeft())};

    QMouseEvent move{QEvent::MouseMove, QPointF{localPoint}, QPointF{globalPoint}, Qt::NoButton, Qt::NoButton, Qt::KeyboardModifiers{}};
    QApplication::sendEvent(overlay, &move);

    ASSERT_EQ(fake.queriedPoints.size(), 1U) << "悬停没有去问平台要界面元素";
    EXPECT_EQ(fake.queriedPoints[0], globalPoint) << "问的不是全局坐标，问到的是别的屏上的元素";
    EXPECT_NE(std::ranges::find(fake.excludedHandles, static_cast<quintptr>(overlay->winId())), fake.excludedHandles.end())
            << "自家遮罩窗没进排除名单：命中会认到盖住全屏的遮罩自己";
    ASSERT_TRUE(overlay->hoveredRegion().has_value()) << "链送回来了，悬停却没落在链上";
    EXPECT_EQ(*overlay->hoveredRegion(), localDeep) << "链的全局到局部换算与遮罩自己那条不一致";

    // 同一点再报一次：不重复问
    QApplication::sendEvent(overlay, &move);
    EXPECT_EQ(fake.queriedPoints.size(), 1U) << "同一个点被问了两次，每次都是一串跨进程调用";
}

/**
 * @brief 钉住平台没有元素能力时不接这条线：鼠标一动就问一次是白花跨进程开销
 */
TEST_F(CaptureServiceFixture, HoverDoesNotAskWhenThePlatformHasNoElementDetection)
{
    if (QGuiApplication::screens().isEmpty())
    {
        GTEST_SKIP() << "这个环境没有屏幕可枚举，铺不出遮罩";
    }

    SettingsStore           settings(m_iniFile);
    FakeCaptureSource       source;
    FakePlatformIntegration fake; // 能力位全假：连 uiElements 都不给
    CaptureService          service(source, settings, fake);

    service.startInteractive();
    auto *overlay = findOverlay();
    ASSERT_NE(overlay, nullptr) << "开始框选之后没铺出遮罩";

    const auto  localPoint  = QRect(QPoint{0, 0}, overlay->size()).center();
    const auto  globalPoint = overlay->geometry().topLeft() + localPoint;
    QMouseEvent move{QEvent::MouseMove, QPointF{localPoint}, QPointF{globalPoint}, Qt::NoButton, Qt::NoButton, Qt::KeyboardModifiers{}};
    QApplication::sendEvent(overlay, &move);

    EXPECT_TRUE(fake.queriedPoints.empty()) << "能力位为假还是去问了平台";
}

/**
 * @brief 钉住比例锁跨轮次记住的整条路：开遮罩时装档、拖动按档、解开落盘清空
 * @details 三处各钉一步：装档漏了下一轮白锁一次；拖动不按档等于锁定没生效；
 *          落盘漏了「记住上次那档」这句话就没有出处。锁上写的是约分后的样子（100:50 → 2:1）。
 */
TEST_F(CaptureServiceFixture, AspectRatioLockLoadsAppliesAndPersists)
{
    if (QGuiApplication::screens().isEmpty())
    {
        GTEST_SKIP() << "这个环境没有屏幕可枚举，铺不出遮罩";
    }

    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureAspectRatio, QVariant(QStringLiteral("2:1"))).has_value());
    settings.sync();

    FakeCaptureSource       source;
    FakePlatformIntegration fake;
    CaptureService          service(source, settings, fake);

    service.startInteractive();
    auto *overlay = findOverlay();
    ASSERT_NE(overlay, nullptr) << "开始框选之后没铺出遮罩";
    EXPECT_EQ(overlay->lockedAspectRatio(), (QSize(2, 1))) << "开遮罩没把记住的那一档装上";

    // 拖一块：光标差 (200,200)，2:1 下按纵轴要的格数算 → 宽 400、高 200
    const auto  anchor = QPoint{60, 60};
    const auto  pull   = QPoint{260, 260};
    const auto  global = overlay->geometry().topLeft();
    QMouseEvent press{QEvent::MouseButtonPress, QPointF{anchor}, QPointF{global + anchor}, Qt::LeftButton, Qt::LeftButton, Qt::KeyboardModifiers{}};
    QApplication::sendEvent(overlay, &press);
    QMouseEvent move{QEvent::MouseMove, QPointF{pull}, QPointF{global + pull}, Qt::NoButton, Qt::LeftButton, Qt::KeyboardModifiers{}};
    QApplication::sendEvent(overlay, &move);
    EXPECT_EQ(overlay->selectedRegion(), QRect(60, 60, 400, 200)) << "拖动没按装上的那一档来：记住的档没走到绘制这一层";

    // 解开：落盘的那一档跟着清
    QKeyEvent unlock{QEvent::KeyPress, Qt::Key_A, Qt::NoModifier};
    QApplication::sendEvent(overlay, &unlock);
    settings.sync();
    SettingsStore reopened(m_iniFile);
    EXPECT_TRUE(reopened.stringValue(GyanisScribe::Settings::kCaptureAspectRatio, QString()).isEmpty()) << "解开没有落盘，下一轮会又锁上";
}

/**
 * @brief 钉住圆角截图开关：开着时区域成品四角透明、内里不动；关着是原样方角
 * @details 替身帧是 2 倍比的纯色图：裁 (0,0,20,20) 得 40×40；半径 12 逻辑 × 2 = 24 物理，
 *
 * 角尖 (0,0) 距圆角圆心 (24,24) 有 34 格、必在弧外；中心与边上不动。
 */
TEST_F(CaptureServiceFixture, RoundCornersToggleShapesTheRegionProduct)
{
    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    CaptureService    service(source, settings, *platform);

    Outcome outcome;
    wire(service, outcome);
    service.captureRegion(QRect(0, 0, 20, 20), QStringLiteral("FakeScreen-1"));
    ASSERT_TRUE(outcome.image.has_value()) << (outcome.error ? outcome.error->message : std::string{});
    EXPECT_NE(outcome.image->image.pixelColor(0, 0).alpha(), 0) << "默认档不该切圆角";

    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureRoundCorners, QVariant(true)).has_value());
    Outcome rounded;
    wire(service, rounded);
    service.captureRegion(QRect(0, 0, 20, 20), QStringLiteral("FakeScreen-1"));
    ASSERT_TRUE(rounded.image.has_value());
    EXPECT_EQ(rounded.image->image.pixelColor(0, 0).alpha(), 0) << "开了圆角，角尖还是实心";
    EXPECT_NE(rounded.image->image.pixelColor(10, 10).alpha(), 0) << "开了圆角，内里被一起切了";
}

/**
 * @brief 钉住圆角半径这一格真的作用到成品上，坏值按默认档继续
 * @details 判据数的是整张成品上全透明的像素数，不取某一探点：弧的边界正好压在格点上时抗锯齿会让那一点
 *          既不实心也不全透。四个角各切掉约 0.2146·r²，r 从 24 物理（默认 12 逻辑）到 40 物理（20 逻辑）
 *          全图透出的格数从约 500 涨到约 1370，差得远；而「坏值回落默认档」要求数出来的格数与默认档逐格相同。
 */
TEST_F(CaptureServiceFixture, CornerRadiusSettingShapesTheRoundedProduct)
{
    // 数整张成品上被切透的格子：只看全透明，边界上那些半透明的格子两种半径都算不进
    const auto cutPixels = [](const CapturedImage &product)
    {
        auto cut = 0;
        for (auto y = 0; y < product.image.height(); ++y)
        {
            for (auto x = 0; x < product.image.width(); ++x)
            {
                if (product.image.pixelColor(x, y).alpha() == 0)
                {
                    ++cut;
                }
            }
        }
        return cut;
    };

    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureRoundCorners, QVariant(true)).has_value());

    FakeCaptureSource defaultSource;
    CaptureService    defaultService(defaultSource, settings, *platform);
    Outcome           atDefault;
    wire(defaultService, atDefault);
    defaultService.captureRegion(QRect(0, 0, 40, 40), QStringLiteral("FakeScreen-1"));
    ASSERT_TRUE(atDefault.image.has_value()) << (atDefault.error ? atDefault.error->message : std::string{});
    const auto defaultCut = cutPixels(*atDefault.image);
    EXPECT_GT(defaultCut, 0) << "默认档就该把四角切透";
    EXPECT_NE(atDefault.image->image.pixelColor(40, 40).alpha(), 0) << "切圆不该把中间一起吃掉";

    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureCornerRadius, QVariant(20)).has_value());
    FakeCaptureSource wideSource;
    CaptureService    wideService(wideSource, settings, *platform);
    Outcome           atWide;
    wire(wideService, atWide);
    wideService.captureRegion(QRect(0, 0, 40, 40), QStringLiteral("FakeScreen-1"));
    ASSERT_TRUE(atWide.image.has_value());
    const auto wideCut = cutPixels(*atWide.image);
    EXPECT_GT(wideCut, defaultCut + 200) << "半径从 12 调到 20 该多切出一大片角：这一格没被吃进几何";

    // 坏值（0）不该让截图失败，也不该悄悄按 0 切：记一条原因之后按默认档继续
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureCornerRadius, QVariant(0)).has_value());
    FakeCaptureSource brokenSource;
    CaptureService    brokenService(brokenSource, settings, *platform);
    Outcome           atBroken;
    wire(brokenService, atBroken);
    brokenService.captureRegion(QRect(0, 0, 40, 40), QStringLiteral("FakeScreen-1"));
    ASSERT_TRUE(atBroken.image.has_value()) << "半径填坏了不该把这一次截图拒掉";
    EXPECT_EQ(cutPixels(*atBroken.image), defaultCut) << "坏值要回落到默认 12 格，而不是按 0 处理（那等于没切）";
}

/**
 * @brief 钉住遮罩翻出来的那一档压过配置
 * @details 配置关着而这一轮说要含：交出去的帧必须带上指针，否则 `P` 那一档只是界面上一行字。
 */
TEST_F(CaptureServiceFixture, CursorToggleFromOverlayBeatsTheSetting)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureIncludeCursor, QVariant(false)).has_value());

    FakeCaptureSource source;
    source.cursorSprite = makeGreenSprite(QPoint(10, 10));

    CaptureService service(source, settings, *platform);
    service.setCursorIncludedForRound(true);
    Outcome outcome;
    wire(service, outcome);
    service.captureFullScreen();

    ASSERT_TRUE(outcome.image.has_value());
    EXPECT_EQ(outcome.image->image.pixelColor(20, 20), QColor(0, 255, 0)) << "遮罩里按了 P，交出去的帧就该带上指针";
}

/**
 * @brief 钉住这一档只活到下一次交付
 * @details 没有遮罩参与的那几路（托盘、热键、命令行）不该被上一轮的临时档污染。
 */
TEST_F(CaptureServiceFixture, CursorOverrideExpiresAfterTheDelivery)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureIncludeCursor, QVariant(false)).has_value());

    FakeCaptureSource source;
    source.cursorSprite = makeGreenSprite(QPoint(10, 10));

    CaptureService service(source, settings, *platform);
    service.setCursorIncludedForRound(true);
    service.captureFullScreen();
    service.captureFullScreen();

    EXPECT_EQ(source.requestedCursorScreens.size(), 1U) << "那一档只该活一次：第二张又去问指针形状，就是被上一轮污染了";
}

/**
 * @brief 钉住拒绝面：配置开着而这一轮说不含，就不该去问指针的形状
 */
TEST_F(CaptureServiceFixture, CursorToggleCanTurnTheCursorOffForOneRound)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureIncludeCursor, QVariant(true)).has_value());

    FakeCaptureSource source;
    source.cursorSprite = makeGreenSprite(QPoint(10, 10));

    CaptureService service(source, settings, *platform);
    service.setCursorIncludedForRound(false);
    service.captureFullScreen();

    EXPECT_TRUE(source.requestedCursorScreens.empty()) << "翻成不含指针之后还去画指针，那一档就成了摆设";
}

/**
 * @brief 钉住排除清单真的从悬停候选里拿掉了那几张
 * @details 只钉纯函数不够：清单读没读、筛在哪一步、筛完还喂不喂给遮罩，是三件事。
 *          这里让替身列出两扇窗（一扇在排除清单里、一扇不在），把光标分别挪到两扇窗上：
 *          被排除的那扇不该被认出来，另一扇要照旧认得——只验「排掉了」会把「整片候选都丢了」也放过。
 */
TEST_F(CaptureServiceFixture, ExcludedProcessesDropOutOfHoverCandidates)
{
    if (QGuiApplication::screens().isEmpty())
    {
        GTEST_SKIP() << "这个环境没有屏幕可枚举，铺不出遮罩";
    }

    SettingsStore           settings(m_iniFile);
    FakeCaptureSource       source;
    FakePlatformIntegration fake;
    fake.capabilitiesValue.windowEnumeration = true;
    ASSERT_TRUE(static_cast<bool>(settings.setValue(GyanisScribe::Settings::kCaptureDetectionExcludes, QStringLiteral("chrome.exe")))) << "写入失败";

    const auto screenRect  = QGuiApplication::primaryScreen()->geometry();
    const auto bannedRect  = QRect{screenRect.left() + 40, screenRect.top() + 40, screenRect.width() / 3, screenRect.height() / 3};
    const auto allowedRect = QRect{screenRect.left() + screenRect.width() / 2, screenRect.top() + screenRect.height() / 2, screenRect.width() / 3, screenRect.height() / 3};
    fake.windows           = std::vector<GyanisScribe::Platform::ScreenWindow>{
            GyanisScribe::Platform::ScreenWindow{1, QStringLiteral("被排除的窗"), QStringLiteral("chrome.exe"), bannedRect, bannedRect},
            GyanisScribe::Platform::ScreenWindow{2, QStringLiteral("留下的窗"), QStringLiteral("notepad.exe"), allowedRect, allowedRect},
    };

    CaptureService service(source, settings, fake);
    service.startInteractive();
    auto *overlay = findOverlay();
    ASSERT_NE(overlay, nullptr) << "开始框选之后没铺出遮罩";

    const auto hoverAt = [overlay](const QRect &globalRect)
    {
        const auto  local     = globalRect.center() - overlay->geometry().topLeft();
        const auto  globalPos = QPointF{globalRect.center()};
        QMouseEvent move{QEvent::MouseMove, QPointF{local}, globalPos, Qt::NoButton, Qt::NoButton, Qt::KeyboardModifiers{}};
        QApplication::sendEvent(overlay, &move);
        return overlay->hoveredRegion();
    };

    EXPECT_FALSE(hoverAt(bannedRect).has_value()) << "被排除的程序还是被认成了候选";
    ASSERT_TRUE(hoverAt(allowedRect).has_value()) << "没被排除的那扇也没认出来：候选整片丢了，判据分不清这两件事";
    EXPECT_EQ(*hoverAt(allowedRect), allowedRect.translated(-overlay->geometry().topLeft())) << "认出来的不是那一扇窗";
}
