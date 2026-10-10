#include "Capture/CaptureService.h"

#include "Capture/IScreenCaptureSource.h"
#include "Core/Logger.h"
#include "Platform/IPlatformIntegration.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QString>
#include <QVariant>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using GyanisScribe::Capture::CaptureService;
using GyanisScribe::Capture::ICaptureService;
using GyanisScribe::Capture::IScreenCaptureSource;
using GyanisScribe::Core::CapturedImage;
using GyanisScribe::Core::Error;
using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Core::Expected;
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

        mutable std::vector<CapturedImage> screenQueue;
        mutable std::vector<QString>       requestedScreens;
        mutable std::vector<quintptr>      requestedWindows;
        Expected<CapturedImage>            screenResult{makeFrame()};
        Expected<CapturedImage>            windowResult{makeFrame()};
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

    // 记的文本坏了：拒绝，且不猜成任意一块屏
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kCaptureLastRegion, QVariant(QStringLiteral("10,20,30"))).has_value());
    {
        Outcome outcome;
        wire(service, outcome);
        service.captureLastRegion();
        ASSERT_TRUE(outcome.error.has_value()) << "三段文本不该被补成四段";
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
    ASSERT_TRUE(GyanisScribe::Core::Logger::initialize(logDirectory).has_value());

    SettingsStore     settings(m_iniFile);
    FakeCaptureSource source;
    source.screenResult = std::unexpected(Error{ErrorCode::ScreenUnavailable, "找不到名为「FakeScreen-9」的屏幕"});
    CaptureService service(source, settings, *platform);
    Outcome        outcome;
    wire(service, outcome);

    service.captureFullScreen(QStringLiteral("FakeScreen-9"));
    ASSERT_TRUE(outcome.error.has_value());

    // 先把 logger 收掉再去读文件：sink 是带缓冲的，边写边读就把「这行到没到盘上」交给了调度
    GyanisScribe::Core::Logger::shutdown();

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
