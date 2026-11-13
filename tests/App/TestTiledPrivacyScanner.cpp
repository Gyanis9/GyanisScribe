// 分带扫描器的直测：逐带串行、结果按带原点平移合并、任一带失败即整次失败、收尾交回原图。
// 假引擎按接收顺序给每带一个不同的文本，判据落在调用次数、行坐标与交回那张图的 cacheKey 上。

#include "App/TiledPrivacyScanner.h"

#include "Base/Logger.h"
#include "Base/Types.h"
#include "Core/Ocr/IOcrEngine.h"
#include "Core/Ocr/OcrTypes.h"
#include "Core/Settings/SettingsStore.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>
#include <utility>
#include <vector>

using GyanisScribe::App::TiledPrivacyScanner;
using GyanisScribe::Base::Error;
using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Base::Expected;
using GyanisScribe::Core::IOcrEngine;
using GyanisScribe::Core::OcrLine;
using GyanisScribe::Core::OcrResult;
using GyanisScribe::Core::OcrWord;
using GyanisScribe::Core::SettingsStore;

namespace
{
    /// 界面线程上等一次扫描落地的最长时限：到点判红，而不是让用例挂住
    constexpr qint64 kWaitPatienceMs = 5'000;

    /**
     * @brief 按调用顺序返回不同文本的假引擎
     * @details 第 n 次调用返回一行「band-n」，行框在带内坐标固定；用例据行坐标判平移、
     *          据调用次数判每一带都真的交出去过。
     */
    class CountingEngine final : public IOcrEngine
    {
    public:
        /**
         * @brief 后端显示名
         * @return 固定为分带替身
         */
        [[nodiscard]] QString name() const override
        {
            return QStringLiteral("分带替身");
        }

        /**
         * @brief 本后端此刻能不能用
         * @return m_available 的读数
         */
        [[nodiscard]] bool isAvailable() const override
        {
            return m_available;
        }

        /**
         * @brief 不可用的原因
         * @return 用例不动这一格，为空串
         */
        [[nodiscard]] QString unavailableReason() const override
        {
            return {};
        }

        /**
         * @brief 可选语言标签
         * @return 与本仓识别链同形的一项
         */
        [[nodiscard]] QStringList supportedLanguages() const override
        {
            return {QStringLiteral("zh-Hans")};
        }

        /**
         * @brief 记下调用序号并返回一行落在带内的结果
         * @param image 这一带，用例只看它是不是真的送进来了
         * @param language 不使用
         * @return 一行「band-n」的结果；命中 failOnCall 时返回失败
         */
        [[nodiscard]] Expected<OcrResult> recognize(const QImage &image, const QString &) override
        {
            const int call = ++calls;
            if (failOnCall > 0 && call == failOnCall)
            {
                return std::unexpected(Error{ErrorCode::PlatformUnsupported, "第 N 带识别失败"});
            }

            OcrLine line;
            line.boundingBox = QRect{5, 20, 80, 20};
            line.words       = {OcrWord{QStringLiteral("band-%1").arg(call), QRect{5, 20, 80, 20}}};

            OcrResult result;
            result.language = QStringLiteral("zh-Hans");
            result.lines    = {line};
            if (image.isNull())
            {
                return std::unexpected(Error{ErrorCode::InvalidArgument, "交进来的带是空图"});
            }
            return result;
        }

        bool m_available = true; ///< 可用性
        int  calls       = 0;    ///< 进入识别体的次数
        int  failOnCall  = 0;    ///< 第几次调用失败；0 表示不失败
    };

    std::unique_ptr<QApplication> application;

    class TiledPrivacyScannerFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestTiledPrivacyScanner";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);

            // 日志只留错误级并落到临时目录：协调器每一带都会说一句「正在检查」，控制台上不该被它冲散
            const auto logDirectory = std::filesystem::temp_directory_path() / "GyanisScribe-TiledScanner-logs";
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
            m_iniFile           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-TiledScanner-{}.ini", caseName);
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

    /**
     * @brief 在界面线程上等一个判据成立
     * @tparam Predicate 判据类型
     * @param predicate 要等到成立的那一条
     * @param patienceMs 最长等待毫秒数
     * @return 最后由判据给出的结果
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
} // namespace

/// 逐带扫描：三带图各带一次调用，结果行坐标已平移
TEST_F(TiledPrivacyScannerFixture, ScansEveryBandAndShiftsTheRows)
{
    auto                engine = std::make_unique<CountingEngine>();
    auto               *fake   = engine.get();
    TiledPrivacyScanner scanner{*m_settings, std::move(engine)};

    QImage             delivered;
    std::vector<QRect> boxes;
    QObject::connect(&scanner, &TiledPrivacyScanner::scanSucceeded, &scanner,
                     [&](const QImage &image, const OcrResult &result)
                     {
                         delivered = image;
                         for (const auto &line: result.lines)
                         {
                             boxes.push_back(line.boundingBox);
                         }
                     });

    QImage image{QSize{400, 3000}, QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::black);
    scanner.start(image);

    ASSERT_TRUE(waitUntil([&] { return !scanner.isScanning(); })) << "扫描没有收尾";
    EXPECT_EQ(fake->calls, 3) << "3000 高按 1200/120 应切三带";
    EXPECT_EQ(delivered.cacheKey(), image.cacheKey()) << "交回的不是原图，下游的 cacheKey 校验会判过期";
    ASSERT_EQ(boxes.size(), 3U);
    // 三带的带原点分别是 0、1080、2160；行在带内 y=20
    EXPECT_EQ(boxes[0].top(), 20);
    EXPECT_EQ(boxes[1].top(), 1100);
    EXPECT_EQ(boxes[2].top(), 2180);
}

/// 任一带失败：整次失败，不交结果
TEST_F(TiledPrivacyScannerFixture, FailsTheWholeScanWhenOneBandFails)
{
    auto  engine     = std::make_unique<CountingEngine>();
    auto *fake       = engine.get();
    fake->failOnCall = 2;
    TiledPrivacyScanner scanner{*m_settings, std::move(engine)};

    bool succeeded = false;
    bool failed    = false;
    QObject::connect(&scanner, &TiledPrivacyScanner::scanSucceeded, &scanner, [&](const QImage &, const OcrResult &) { succeeded = true; });
    QObject::connect(&scanner, &TiledPrivacyScanner::scanFailed, &scanner, [&](const Error &) { failed = true; });

    QImage image{QSize{400, 3000}, QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::black);
    scanner.start(image);

    ASSERT_TRUE(waitUntil([&] { return failed || succeeded; }));
    EXPECT_TRUE(failed) << "第二带失败却整次没报错";
    EXPECT_FALSE(succeeded) << "失败时不该交半截结果";
    EXPECT_FALSE(scanner.isScanning());
}
