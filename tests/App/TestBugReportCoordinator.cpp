// 报告协调器的直测：开窗时把截图来源写进环境、只开一扇；
// 扫描结果按 cacheKey 对号入座，旧窗迟到的结果不落新窗，忙时只排最新一张；
// 码识别后端缺席不拖住文字打码，引擎不可用与底图对不上都落到不可用状态。

#include "App/BugReportCoordinator.h"

#include "Base/Logger.h"
#include "Core/BugReport/BugReport.h"
#include "Core/Ocr/IOcrEngine.h"
#include "Core/Ocr/OcrTypes.h"
#include "Core/Settings/SettingsStore.h"
#include "Gui/BugReport/BugReportDialog.h"
#include "Plugins/Barcode/BarcodeDecoder.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QTimeZone>
#include <QWidget>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

using GyanisScribe::App::BugReportCoordinator;
using GyanisScribe::Base::Error;
using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Base::Expected;
using GyanisScribe::Core::IOcrEngine;
using GyanisScribe::Core::OcrLine;
using GyanisScribe::Core::OcrResult;
using GyanisScribe::Core::OcrWord;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Core::BugReport::BugReportCaptureContext;
using GyanisScribe::Core::BugReport::RedactionState;
using GyanisScribe::Gui::BugReportDialog;
using GyanisScribe::Plugins::BarcodeReading;

namespace
{
    /// 界面线程上等一次扫描落地的最长时限：到点判红，而不是让用例挂住
    constexpr qint64 kWaitPatienceMs = 5'000;

    /// 单飞用例里等假引擎进入识别体的时限
    constexpr std::chrono::seconds kEnterPatience{5};

    std::unique_ptr<QApplication> application;

    /**
     * @brief 造一张纯色图
     * @param size 图像尺寸
     * @param color 填充色
     * @return ARGB32 预乘格式的图
     */
    QImage makeImage(const QSize &size, const QColor &color)
    {
        QImage image{size, QImage::Format_ARGB32_Premultiplied};
        image.fill(color);
        return image;
    }

    /**
     * @brief 造一份能命中手机号的结果
     * @return 一行一个手机号词、词框落在图内的结果
     */
    OcrResult makePhoneResult()
    {
        OcrResult result;
        result.language = QStringLiteral("zh-Hans");
        result.lines    = {OcrLine{{OcrWord{QStringLiteral("13800138000"), QRect{40, 30, 80, 20}}}, QRect{40, 30, 80, 20}}};
        return result;
    }

    /**
     * @brief 造一份截图来源信息
     * @return 屏幕名、时刻、尺寸与前台窗口都齐的上下文字
     */
    BugReportCaptureContext makeContext()
    {
        BugReportCaptureContext context;
        context.logicalSize      = QSize{200, 100};
        context.devicePixelRatio = 1.0;
        context.screenName       = QStringLiteral("DISPLAY9");
        context.capturedAt       = QDateTime{QDate{2026, 11, 12}, QTime{15, 30, 12}, QTimeZone::fromSecondsAheadOfUtc(8 * 3600)};
        context.windowTitle      = QStringLiteral("记事本");
        context.processName      = QStringLiteral("notepad.exe");
        return context;
    }

    /// 一个码都没认出来的码识别函数
    std::function<Expected<std::vector<BarcodeReading>>(const QImage &)> emptyBarcodeReader()
    {
        return [](const QImage &) { return Expected<std::vector<BarcodeReading>>{std::vector<BarcodeReading>{}}; };
    }

    /**
     * @brief 有界等待：条件成立才返回，超时返回最后一次判定
     * @param predicate 要等的条件
     * @param patienceMs 最长等待毫秒数
     * @return 最后一次判定
     */
    template<typename Predicate>
    [[nodiscard]] bool waitUntil(Predicate predicate, const qint64 patienceMs = kWaitPatienceMs)
    {
        QElapsedTimer watch;
        watch.start();
        while (watch.elapsed() < patienceMs)
        {
            if (predicate())
            {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return predicate();
    }

    /**
     * @brief 可注入的识别后端替身
     * @details 成功/失败由构造时给的结果决定；可用性可关；闸门打开后 recognize() 会停在内部直到 release()，
     *          让单飞用例把第二次请求压在识别途中。所有字段由同一把锁保护，测试线程与工作线程各读一侧。
     */
    class FakeOcrEngine final : public IOcrEngine
    {
    public:
        /**
         * @brief 构造
         * @param outcome 每次 recognize() 的返回值
         */
        explicit FakeOcrEngine(Expected<OcrResult> outcome) : m_outcome(std::move(outcome))
        {
        }

        /**
         * @brief 后端显示名
         * @return 固定为测试替身
         */
        [[nodiscard]] QString name() const override
        {
            return QStringLiteral("测试替身");
        }

        /**
         * @brief 本后端此刻能不能用
         * @return m_available 的读数
         */
        [[nodiscard]] bool isAvailable() const override
        {
            std::scoped_lock lock(m_mutex);
            return m_available;
        }

        /**
         * @brief 不可用的原因
         * @return 可用时为空串，否则为 m_unavailableReason
         */
        [[nodiscard]] QString unavailableReason() const override
        {
            std::scoped_lock lock(m_mutex);
            return m_available ? QString{} : m_unavailableReason;
        }

        /**
         * @brief 可选语言标签
         * @return 与真实词表同形的两项
         */
        [[nodiscard]] QStringList supportedLanguages() const override
        {
            return {QStringLiteral("zh-Hans"), QStringLiteral("en")};
        }

        /**
         * @brief 记下调用并返回进入时那一份结果
         * @param language 记下来给用例断言
         * @return 进入识别体时 m_outcome 的读数
         * @details 结果在进入时取走：用例会在第一张还压在闸门里时换掉下一次的返回值，
         *          用来分辨迟到的结果与排队的这一张各自带来了什么。
         */
        [[nodiscard]] Expected<OcrResult> recognize(const QImage &, const QString &language) override
        {
            std::unique_lock lock(m_mutex);
            ++m_recognizeCalls;
            m_lastLanguage     = language;
            m_entered          = true;
            const auto outcome = m_outcome;
            m_condition.notify_all();

            // 闸门打开时停在识别体内；每次放行只解一张，下一张仍会停住等用例再放
            if (m_gated)
            {
                m_condition.wait(lock, [this] { return m_released; });
                m_released = false;
            }
            return outcome;
        }

        /// 换掉之后每次识别的返回值；已在识别体内的那一次不受影响
        void setOutcome(Expected<OcrResult> outcome)
        {
            std::scoped_lock lock(m_mutex);
            m_outcome = std::move(outcome);
        }

        /// 关掉可用性并给出原因；只在请求之前调用
        void setUnavailable(QString reason)
        {
            std::scoped_lock lock(m_mutex);
            m_available         = false;
            m_unavailableReason = std::move(reason);
        }

        /// 打开闸门：recognize() 会停住直到 release()
        void setGated()
        {
            std::scoped_lock lock(m_mutex);
            m_gated = true;
        }

        /// 放行停在 recognize() 里的这一次；下一次仍会停住
        void release()
        {
            {
                std::scoped_lock lock(m_mutex);
                m_released = true;
            }
            m_condition.notify_all();
        }

        /// 彻底打开闸门：之后进来的识别不再停住
        void openGate()
        {
            {
                std::scoped_lock lock(m_mutex);
                m_gated    = false;
                m_released = true;
            }
            m_condition.notify_all();
        }

        /// 等假引擎真的进入识别体，返回是否在时限内进入
        [[nodiscard]] bool waitUntilEntered(std::chrono::milliseconds timeout)
        {
            std::unique_lock lock(m_mutex);
            return m_condition.wait_for(lock, timeout, [this] { return m_entered; });
        }

        /// 已进入识别体的次数
        [[nodiscard]] int recognizeCalls() const
        {
            std::scoped_lock lock(m_mutex);
            return m_recognizeCalls;
        }

        /// 最近一次收到的语言标签
        [[nodiscard]] QString lastLanguage() const
        {
            std::scoped_lock lock(m_mutex);
            return m_lastLanguage;
        }

    private:
        Expected<OcrResult>     m_outcome;                ///< 下一次识别的返回值，进入识别体时取走
        bool                    m_available = true;       ///< 可用性
        QString                 m_unavailableReason;      ///< 不可用原因
        bool                    m_gated          = false; ///< 是否在 recognize() 里等 release()
        bool                    m_released       = false; ///< 闸门是否已放行
        bool                    m_entered        = false; ///< 是否已进入过识别体
        int                     m_recognizeCalls = 0;     ///< 进入识别体的次数
        QString                 m_lastLanguage;           ///< 最近一次的语言标签
        mutable std::mutex      m_mutex;                  ///< 保护上面全部字段
        std::condition_variable m_condition;              ///< 进入与放行的通知
    };

    /**
     * @brief 用例提前返回时也放行闸门
     * @details 协调器析构会等工作线程认完；若 ASSERT 失败直接返回而闸门还关着，
     *          析构就会挂住。守卫声明在协调器之后，先于它析构，保证退出路径都会放行。
     */
    class GateReleaseGuard final
    {
    public:
        /**
         * @brief 绑一个假引擎
         * @param engine 要放行的引擎
         */
        explicit GateReleaseGuard(FakeOcrEngine &engine) : m_engine(engine)
        {
        }

        /// 析构即彻底打开闸门：用例中途 ASSERT 失败时也不会把工作线程留在里面
        ~GateReleaseGuard()
        {
            m_engine.openGate();
        }

        GateReleaseGuard(const GateReleaseGuard &)            = delete;
        GateReleaseGuard &operator=(const GateReleaseGuard &) = delete;

    private:
        FakeOcrEngine &m_engine; ///< 要放行的引擎，不拥有
    };

    class BugReportCoordinatorFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestAppBugReport";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);

            // 日志只留错误级并落到临时目录：码识别失败那条用例会写一条 warning，控制台上不该被它冲散
            const auto logDirectory = std::filesystem::temp_directory_path() / "GyanisScribe-BugReportCoordinator-logs";
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
            m_iniFile           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-BugReportCoordinator-{}.ini", caseName);
            std::error_code ignored;
            std::filesystem::remove(m_iniFile, ignored);
            m_settings = std::make_unique<SettingsStore>(m_iniFile);
        }

        void TearDown() override
        {
            m_settings.reset();
            std::error_code ignored;
            std::filesystem::remove(m_iniFile, ignored);
        }

        std::filesystem::path          m_iniFile;
        std::unique_ptr<SettingsStore> m_settings;
    };
} // namespace

TEST_F(BugReportCoordinatorFixture, OpensOneDialogAndUsesTheCaptureContext)
{
    auto                 engine = std::make_unique<FakeOcrEngine>(Expected<OcrResult>{makePhoneResult()});
    auto                *fake   = engine.get();
    BugReportCoordinator coordinator{*m_settings, std::move(engine), emptyBarcodeReader()};

    QWidget    parent;
    const auto context = makeContext();
    coordinator.openReport(makeImage(QSize{200, 100}, QColor{30, 60, 90}), makeImage(QSize{200, 100}, QColor{30, 60, 90}), context, &parent);

    auto *dialog = coordinator.dialogForTesting();
    ASSERT_NE(dialog, nullptr);
    EXPECT_TRUE(dialog->isVisible());
    EXPECT_EQ(dialog->windowModality(), Qt::WindowModal);
    EXPECT_EQ(dialog->environmentForTesting().screenName, context.screenName);
    EXPECT_EQ(dialog->environmentForTesting().capturedAt, context.capturedAt);
    EXPECT_EQ(dialog->environmentForTesting().logicalSize, context.logicalSize);
    EXPECT_EQ(dialog->environmentForTesting().physicalSize, (QSize{200, 100}));
    EXPECT_EQ(dialog->environmentForTesting().windowTitle, context.windowTitle);
    EXPECT_EQ(dialog->environmentForTesting().processName, context.processName);
    EXPECT_TRUE(waitUntil([dialog] { return dialog->redactionForTesting().state == RedactionState::Masked; })) << "扫描结果没有落到对话框";
    EXPECT_GT(fake->recognizeCalls(), 0);
    EXPECT_FALSE(coordinator.isScanningForTesting());
    EXPECT_TRUE(dialog->reportText().contains(QStringLiteral("DISPLAY9")));
    EXPECT_TRUE(dialog->reportText().contains(QStringLiteral("记事本")));

    coordinator.openReport(makeImage(QSize{200, 100}, QColor{1, 2, 3}), makeImage(QSize{200, 100}, QColor{1, 2, 3}), context, &parent);
    EXPECT_EQ(coordinator.dialogForTesting(), dialog) << "第二次打开叠了一扇新窗";
    EXPECT_EQ(parent.findChildren<BugReportDialog *>().size(), 1);
    EXPECT_EQ(fake->recognizeCalls(), 1) << "已有报告窗时再打开只该抬窗，不该重新扫一遍";
}

TEST_F(BugReportCoordinatorFixture, UnavailableEngineLandsInUnavailableState)
{
    auto engine = std::make_unique<FakeOcrEngine>(Expected<OcrResult>{OcrResult{}});
    engine->setUnavailable(QStringLiteral("没有后端"));
    BugReportCoordinator coordinator{*m_settings, std::move(engine), emptyBarcodeReader()};
    QWidget              parent;
    coordinator.openReport(makeImage(QSize{100, 50}, QColor{10, 20, 30}), makeImage(QSize{100, 50}, QColor{10, 20, 30}), makeContext(), &parent);
    auto *dialog = coordinator.dialogForTesting();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->redactionForTesting().state, RedactionState::Unavailable);
    EXPECT_TRUE(dialog->reportText().contains(QStringLiteral("没有后端")));
}

TEST_F(BugReportCoordinatorFixture, ClosingDuringAScanIgnoresTheLateResultAndQueuesTheNextDialog)
{
    auto  engine = std::make_unique<FakeOcrEngine>(Expected<OcrResult>{makePhoneResult()});
    auto *fake   = engine.get();
    fake->setGated();
    BugReportCoordinator coordinator{*m_settings, std::move(engine), emptyBarcodeReader()};
    GateReleaseGuard     releaseGuard{*fake};
    QWidget              parent;

    coordinator.openReport(makeImage(QSize{200, 100}, QColor{20, 40, 60}), makeImage(QSize{200, 100}, QColor{20, 40, 60}), makeContext(), &parent);
    auto *first = coordinator.dialogForTesting();
    ASSERT_NE(first, nullptr);
    ASSERT_TRUE(fake->waitUntilEntered(kEnterPatience));
    EXPECT_TRUE(coordinator.isScanningForTesting());
    first->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(coordinator.dialogForTesting(), nullptr) << "关掉的报告窗还挂在协调器上";

    coordinator.openReport(makeImage(QSize{200, 100}, QColor{60, 40, 20}), makeImage(QSize{200, 100}, QColor{60, 40, 20}), makeContext(), &parent);
    auto *second = coordinator.dialogForTesting();
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->redactionForTesting().state, RedactionState::Pending) << "上一轮没回来时新窗不该假装在扫";

    // 新窗等的是新的一张：换掉之后的返回值，迟到的那份手机号结果就不该再出现
    fake->setOutcome(Expected<OcrResult>{OcrResult{}});
    fake->release();
    ASSERT_TRUE(waitUntil([fake] { return fake->recognizeCalls() >= 2; }, 10'000)) << "放行后没有开始补扫排队的这一张";
    EXPECT_EQ(second->redactionForTesting().state, RedactionState::Pending) << "旧图迟到的结果落进了新窗";

    fake->release();
    EXPECT_TRUE(waitUntil([second] { return second->redactionForTesting().state == RedactionState::NoHits; }, 10'000)) << "第二扇窗没有拿到自己的结果";
    EXPECT_FALSE(coordinator.isScanningForTesting());
    EXPECT_EQ(fake->recognizeCalls(), 2);
    EXPECT_EQ(parent.findChildren<BugReportDialog *>().size(), 1);
}

TEST_F(BugReportCoordinatorFixture, BarcodeFailureStillMasksTextPii)
{
    auto                 engine = std::make_unique<FakeOcrEngine>(Expected<OcrResult>{makePhoneResult()});
    BugReportCoordinator coordinator{*m_settings, std::move(engine),
                                     [](const QImage &) { return Expected<std::vector<BarcodeReading>>{std::unexpected(Error{ErrorCode::PlatformUnsupported, "没有码后端"})}; }};
    QWidget              parent;
    coordinator.openReport(makeImage(QSize{200, 100}, QColor{10, 20, 30}), makeImage(QSize{200, 100}, QColor{10, 20, 30}), makeContext(), &parent);
    auto *dialog = coordinator.dialogForTesting();
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitUntil([dialog] { return dialog->redactionForTesting().state == RedactionState::Masked; })) << "码识别失败拖住了文字打码";
    EXPECT_EQ(dialog->redactionForTesting().maskedCount, 1);
}

TEST_F(BugReportCoordinatorFixture, DetectionImageWithoutMatchingSizeSkipsTheScan)
{
    auto                 engine = std::make_unique<FakeOcrEngine>(Expected<OcrResult>{makePhoneResult()});
    auto                *fake   = engine.get();
    BugReportCoordinator coordinator{*m_settings, std::move(engine), emptyBarcodeReader()};
    QWidget              parent;

    coordinator.openReport(makeImage(QSize{200, 100}, QColor{10, 20, 30}), makeImage(QSize{100, 50}, QColor{10, 20, 30}), makeContext(), &parent);
    auto *dialog = coordinator.dialogForTesting();
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->redactionForTesting().state, RedactionState::Unavailable);
    EXPECT_EQ(fake->recognizeCalls(), 0) << "底图对不上还去认，结果框会整体错位";

    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    coordinator.openReport(makeImage(QSize{200, 100}, QColor{10, 20, 30}), QImage{}, makeContext(), &parent);
    auto *nullDetectionDialog = coordinator.dialogForTesting();
    ASSERT_NE(nullDetectionDialog, nullptr);
    EXPECT_EQ(nullDetectionDialog->redactionForTesting().state, RedactionState::Unavailable);
    EXPECT_EQ(fake->recognizeCalls(), 0) << "没有检查底图时不该凭空认一遍";
}
