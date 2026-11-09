// 隐私扫描协调器的直测：成功只发图与结果、失败与后端不可用都给中文原因、空图拒绝、单飞时第二次只提示。
// 假引擎覆盖成功、失败、不可用与带闸门四种形态；协调器不打开结果窗口，也没有把识别文本送到别处。
// 判据集中在信号次数、isScanning 的起落与假引擎的调用次数，不依赖真实 OCR 后端。

#include "App/PrivacyCoordinator.h"

#include "Base/Logger.h"
#include "Core/Ocr/IOcrEngine.h"
#include "Core/Ocr/OcrLanguage.h"
#include "Core/Ocr/OcrTypes.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"
#include "Gui/Controls/Toast.h"

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QMetaType>
#include <QSignalSpy>
#include <QSize>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <utility>

Q_DECLARE_METATYPE(GyanisScribe::Base::Error)
Q_DECLARE_METATYPE(GyanisScribe::Core::OcrResult)
Q_DECLARE_METATYPE(GyanisScribe::Gui::ToastKind)

using GyanisScribe::App::PrivacyCoordinator;
using GyanisScribe::Base::Error;
using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Base::Expected;
using GyanisScribe::Core::IOcrEngine;
using GyanisScribe::Core::kDefaultOcrLanguage;
using GyanisScribe::Core::kOcrLanguage;
using GyanisScribe::Core::OcrLine;
using GyanisScribe::Core::OcrResult;
using GyanisScribe::Core::OcrWord;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Gui::ToastKind;

namespace
{
    /// 界面线程上等一次识别落地的最长时限：到点判红，而不是让用例挂住
    constexpr int kWaitPatienceMs = 5'000;

    /// 单飞用例里等假引擎进入识别体的时限
    constexpr std::chrono::seconds kEnterPatience{5};

    std::unique_ptr<QCoreApplication> application;

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
     * @brief 造一份小结果：一行一个词，足够让置信度与行文本都参与断言
     * @return 语言为 zh-Hans、正文为「测试文本」的结果
     */
    OcrResult makeResult()
    {
        OcrResult result;
        result.language = QStringLiteral("zh-Hans");

        OcrLine line;
        line.boundingBox = QRect{2, 3, 40, 12};
        line.words       = {OcrWord{QStringLiteral("测试文本"), QRect{2, 3, 40, 12}}};
        result.lines     = {line};
        return result;
    }

    /// 共享的失配文案：用例与假引擎共用，避免两处各改一半
    QString unavailableReasonText()
    {
        return QStringLiteral("本机没有可用的文字识别后端：请安装系统语言包，或换用 Tesseract 语言包后再试");
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
         * @brief 记下调用并返回构造时给的结果
         * @param image 不使用
         * @param language 记下来给用例断言
         * @return 构造时给的结果
         */
        [[nodiscard]] Expected<OcrResult> recognize(const QImage &, const QString &language) override
        {
            std::unique_lock lock(m_mutex);
            ++m_recognizeCalls;
            m_lastLanguage = language;
            m_entered      = true;
            m_condition.notify_all();

            // 闸门打开时停在识别体内：单飞用例据此把第二次请求压在「正在扫描」这个状态上
            if (m_gated)
            {
                m_condition.wait(lock, [this] { return m_released; });
            }
            return m_outcome;
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

        /// 放行停在 recognize() 里的工作线程
        void release()
        {
            {
                std::scoped_lock lock(m_mutex);
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
        Expected<OcrResult>     m_outcome;                ///< 每次识别的返回值
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

        /// 析构即放行
        ~GateReleaseGuard()
        {
            m_engine.release();
        }

        GateReleaseGuard(const GateReleaseGuard &)            = delete;
        GateReleaseGuard &operator=(const GateReleaseGuard &) = delete;

    private:
        FakeOcrEngine &m_engine; ///< 要放行的引擎，不拥有
    };

    class PrivacyCoordinatorFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestPrivacyCoordinator";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QCoreApplication>(argumentCount, arguments);

            // QSignalSpy 按信号参数的元类型名取值：自定义类型必须先按全名注册，否则整条 spy 收不到载荷
            qRegisterMetaType<GyanisScribe::Base::Error>("GyanisScribe::Base::Error");
            qRegisterMetaType<GyanisScribe::Core::OcrResult>("GyanisScribe::Core::OcrResult");
            qRegisterMetaType<GyanisScribe::Gui::ToastKind>("GyanisScribe::Gui::ToastKind");

            // 日志只留错误级并落到临时目录：失败用例会写一条，控制台上不该被 info 冲散
            const auto logDirectory = std::filesystem::temp_directory_path() / "GyanisScribe-PrivacyCoordinator-logs";
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
            m_iniFile           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-PrivacyCoordinator-{}.ini", caseName);
            std::error_code ignored;
            std::filesystem::remove(m_iniFile, ignored);
        }

        void TearDown() override
        {
            std::error_code ignored;
            std::filesystem::remove(m_iniFile, ignored);
        }

        std::filesystem::path m_iniFile;
    };
} // namespace

TEST_F(PrivacyCoordinatorFixture, SuccessfulScanEmitsImageAndResultAndClearsScanning)
{
    SettingsStore settings{m_iniFile};
    ASSERT_TRUE(settings.setValue(kOcrLanguage, QStringLiteral("en")));

    auto               engine = std::make_unique<FakeOcrEngine>(Expected<OcrResult>{makeResult()});
    auto              *fake   = engine.get();
    PrivacyCoordinator coordinator{settings, std::move(engine)};

    QSignalSpy succeeded(&coordinator, &PrivacyCoordinator::scanSucceeded);
    QSignalSpy failed(&coordinator, &PrivacyCoordinator::scanFailed);
    QSignalSpy messages(&coordinator, &PrivacyCoordinator::messageForUser);

    const QImage image = makeImage(QSize{32, 16}, QColor{200, 30, 40});
    coordinator.requestScan(image);

    EXPECT_TRUE(coordinator.isScanning());
    ASSERT_EQ(messages.count(), 1);
    EXPECT_EQ(messages.at(0).at(0).toString(), QStringLiteral("正在检查敏感信息…"));
    EXPECT_EQ(messages.at(0).at(1).value<ToastKind>(), ToastKind::Information);

    ASSERT_TRUE(succeeded.wait(kWaitPatienceMs)) << "识别结果没有回到界面线程";
    EXPECT_FALSE(coordinator.isScanning());
    ASSERT_EQ(succeeded.count(), 1);

    const auto arguments = succeeded.at(0);
    const auto delivered = arguments.at(0).value<QImage>();
    EXPECT_EQ(delivered.size(), image.size());
    EXPECT_EQ(delivered.pixelColor(0, 0), image.pixelColor(0, 0));

    const auto result = arguments.at(1).value<OcrResult>();
    EXPECT_EQ(result.language, QStringLiteral("zh-Hans"));
    EXPECT_EQ(result.text(), QStringLiteral("测试文本"));

    EXPECT_EQ(failed.count(), 0);
    EXPECT_EQ(messages.count(), 1) << "成功路径不该再补提示，也不该打开结果窗口";
    EXPECT_EQ(fake->recognizeCalls(), 1);
    EXPECT_EQ(fake->lastLanguage(), QStringLiteral("en"));
}

TEST_F(PrivacyCoordinatorFixture, UnavailableEngineReportsChineseReasonWithoutCallingRunner)
{
    SettingsStore settings{m_iniFile};

    auto  engine = std::make_unique<FakeOcrEngine>(Expected<OcrResult>{makeResult()});
    auto *fake   = engine.get();
    fake->setUnavailable(unavailableReasonText());
    PrivacyCoordinator coordinator{settings, std::move(engine)};

    QSignalSpy succeeded(&coordinator, &PrivacyCoordinator::scanSucceeded);
    QSignalSpy failed(&coordinator, &PrivacyCoordinator::scanFailed);
    QSignalSpy messages(&coordinator, &PrivacyCoordinator::messageForUser);

    coordinator.requestScan(makeImage(QSize{16, 16}, QColor{10, 120, 220}));

    EXPECT_FALSE(coordinator.isScanning());
    ASSERT_EQ(failed.count(), 1);
    const auto error = failed.at(0).at(0).value<Error>();
    EXPECT_EQ(error.code, ErrorCode::PlatformUnsupported);
    EXPECT_EQ(QString::fromUtf8(error.message.c_str()), unavailableReasonText());

    ASSERT_EQ(messages.count(), 1);
    EXPECT_EQ(messages.at(0).at(0).toString(), unavailableReasonText());
    EXPECT_EQ(messages.at(0).at(1).value<ToastKind>(), ToastKind::Danger);

    EXPECT_EQ(succeeded.count(), 0);
    EXPECT_EQ(fake->recognizeCalls(), 0);
}

TEST_F(PrivacyCoordinatorFixture, NullImageIsRejectedWithoutStartingScan)
{
    SettingsStore settings{m_iniFile};

    auto               engine = std::make_unique<FakeOcrEngine>(Expected<OcrResult>{makeResult()});
    auto              *fake   = engine.get();
    PrivacyCoordinator coordinator{settings, std::move(engine)};

    QSignalSpy succeeded(&coordinator, &PrivacyCoordinator::scanSucceeded);
    QSignalSpy failed(&coordinator, &PrivacyCoordinator::scanFailed);
    QSignalSpy messages(&coordinator, &PrivacyCoordinator::messageForUser);

    coordinator.requestScan(QImage{});

    EXPECT_FALSE(coordinator.isScanning());
    EXPECT_EQ(succeeded.count(), 0);
    EXPECT_EQ(failed.count(), 0);
    EXPECT_EQ(fake->recognizeCalls(), 0);

    ASSERT_EQ(messages.count(), 1);
    EXPECT_EQ(messages.at(0).at(1).value<ToastKind>(), ToastKind::Information);
    const auto message = messages.at(0).at(0).toString();
    EXPECT_FALSE(message.isEmpty());
    EXPECT_TRUE(message.contains(QStringLiteral("图"))) << "空图提示要告诉用户先准备一张图";
}

TEST_F(PrivacyCoordinatorFixture, RecognizerFailureEmitsScanFailedAndActionableMessage)
{
    SettingsStore settings{m_iniFile};

    const Error        engineError{ErrorCode::DependencyMissing, "测试替身：识别后端临时不可用"};
    auto               engine = std::make_unique<FakeOcrEngine>(Expected<OcrResult>{std::unexpected(engineError)});
    auto              *fake   = engine.get();
    PrivacyCoordinator coordinator{settings, std::move(engine)};

    QSignalSpy succeeded(&coordinator, &PrivacyCoordinator::scanSucceeded);
    QSignalSpy failed(&coordinator, &PrivacyCoordinator::scanFailed);
    QSignalSpy messages(&coordinator, &PrivacyCoordinator::messageForUser);

    coordinator.requestScan(makeImage(QSize{20, 20}, QColor{90, 90, 90}));
    ASSERT_TRUE(failed.wait(kWaitPatienceMs)) << "识别失败没有回到界面线程";

    EXPECT_FALSE(coordinator.isScanning());
    ASSERT_EQ(failed.count(), 1);
    const auto error = failed.at(0).at(0).value<Error>();
    EXPECT_EQ(error.code, engineError.code);
    EXPECT_EQ(QString::fromUtf8(error.message.c_str()), QString::fromUtf8(engineError.message.c_str()));

    ASSERT_EQ(messages.count(), 2) << "先有正在检查，再有失败说明";
    const auto message = messages.at(1).at(0).toString();
    EXPECT_EQ(messages.at(1).at(1).value<ToastKind>(), ToastKind::Danger);
    EXPECT_TRUE(message.contains(QStringLiteral("敏感信息检查失败")));
    EXPECT_TRUE(message.contains(QString::fromUtf8(engineError.message.c_str())));
    EXPECT_TRUE(message.contains(QStringLiteral("请重试"))) << "失败提示要给下一步动作";

    EXPECT_EQ(succeeded.count(), 0);
    EXPECT_EQ(fake->recognizeCalls(), 1);
    EXPECT_EQ(fake->lastLanguage(), QString::fromUtf8(kDefaultOcrLanguage));
}

TEST_F(PrivacyCoordinatorFixture, SecondRequestWhileScanningOnlyShowsMessage)
{
    SettingsStore settings{m_iniFile};

    auto  engine = std::make_unique<FakeOcrEngine>(Expected<OcrResult>{makeResult()});
    auto *fake   = engine.get();
    fake->setGated();
    PrivacyCoordinator coordinator{settings, std::move(engine)};
    GateReleaseGuard   releaseGuard{*fake};

    QSignalSpy succeeded(&coordinator, &PrivacyCoordinator::scanSucceeded);
    QSignalSpy failed(&coordinator, &PrivacyCoordinator::scanFailed);
    QSignalSpy messages(&coordinator, &PrivacyCoordinator::messageForUser);

    const QImage first  = makeImage(QSize{32, 16}, QColor{220, 40, 40});
    const QImage second = makeImage(QSize{48, 24}, QColor{40, 40, 220});

    coordinator.requestScan(first);
    ASSERT_TRUE(fake->waitUntilEntered(kEnterPatience)) << "假引擎没有进入识别体";
    EXPECT_TRUE(coordinator.isScanning());

    coordinator.requestScan(second);

    EXPECT_TRUE(coordinator.isScanning());
    EXPECT_EQ(fake->recognizeCalls(), 1) << "单飞期间不该把第二张图交给搬运工";
    ASSERT_EQ(messages.count(), 2);
    EXPECT_EQ(messages.at(1).at(0).toString(), QStringLiteral("正在检查敏感信息…"));
    EXPECT_EQ(messages.at(1).at(1).value<ToastKind>(), ToastKind::Information);

    fake->release();
    ASSERT_TRUE(succeeded.wait(kWaitPatienceMs)) << "放行后识别结果没有回到界面线程";

    EXPECT_FALSE(coordinator.isScanning());
    ASSERT_EQ(succeeded.count(), 1);
    const auto delivered = succeeded.at(0).at(0).value<QImage>();
    EXPECT_EQ(delivered.size(), first.size()) << "单飞期间攒下的第二张图不该顶掉正在认的这一张";
    EXPECT_EQ(delivered.pixelColor(0, 0), first.pixelColor(0, 0));
    EXPECT_EQ(failed.count(), 0);
    EXPECT_EQ(fake->recognizeCalls(), 1);
}
