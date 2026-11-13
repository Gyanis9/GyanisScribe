// 步骤截图批扫器的直测：逐张识别、命中即覆写、读不出的记失败；第二批在跑时当场收尾而不是打断第一批。
// 命中图用渐变而不是纯色：马赛克在一整块同色上平均出来的像素与原图逐点相同，
// 「字节变了」这条判据就会为了错误的原因成立，判不出覆写有没有真的发生。

#include "App/StepsAutoRedactor.h"

#include "Base/Logger.h"
#include "Base/Types.h"
#include "Core/Ocr/IOcrEngine.h"
#include "Core/Ocr/OcrTypes.h"
#include "Core/Settings/SettingsStore.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

using GyanisScribe::App::StepsAutoRedactor;
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
    /// 界面线程上等一批落地的最长时限：到点判红，而不是让用例挂住
    constexpr qint64 kWaitPatienceMs = 5'000;

    /**
     * @brief 命中框：落在图片内部，四边都留出余量
     * @return 假引擎与判据共用的那一块矩形
     */
    QRect hitBox()
    {
        return QRect{10, 10, 120, 20};
    }

    /**
     * @brief 对任意图都返回一个手机号的假引擎
     * @details 批扫器不挑后端，只看结果里有没有命中；文本固定写成一个合法的 11 位号码，
     *          行框与词框都取 hitBox，命中项的物理框因此可预期。
     */
    class PhoneEngine final : public IOcrEngine
    {
    public:
        /**
         * @brief 后端显示名
         * @return 固定为批扫替身
         */
        [[nodiscard]] QString name() const override
        {
            return QStringLiteral("批扫替身");
        }

        /**
         * @brief 本后端此刻能不能用
         * @return 恒为可用
         */
        [[nodiscard]] bool isAvailable() const override
        {
            return true;
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
         * @brief 返回一行手机号
         * @param image 交进来的那张图，用例只看它是不是真的送到了
         * @param language 不使用
         * @return 一行「13800138000」的结果；空图返回失败
         */
        [[nodiscard]] Expected<OcrResult> recognize(const QImage &image, const QString &) override
        {
            ++calls;
            if (image.isNull())
            {
                return std::unexpected(Error{ErrorCode::InvalidArgument, "交进来的图是空图"});
            }

            OcrLine line;
            line.boundingBox = hitBox();
            line.words       = {OcrWord{QStringLiteral("13800138000"), hitBox()}};

            OcrResult result;
            result.language = QStringLiteral("zh-Hans");
            result.lines    = {line};
            return result;
        }

        int calls = 0; ///< 进入识别体的次数
    };

    std::unique_ptr<QApplication> application;

    class StepsAutoRedactorFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestStepsAutoRedactor";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);

            // 日志只留错误级并落到临时目录：协调器每一张都会说一句「正在检查」，控制台上不该被它冲散
            const auto logDirectory = std::filesystem::temp_directory_path() / "GyanisScribe-StepsAutoRedactor-logs";
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
            m_iniFile           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-StepsAutoRedactor-{}.ini", caseName);
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

    /**
     * @brief 写一张渐变 PNG 当步骤截图
     * @param path 目标文件
     * @param size 像素尺寸
     * @return 写成功为 true
     * @details 渐变让每一块马赛克的均值都与其覆盖的像素不同，覆写与否在像素上分得出来
     */
    bool writeGradientPng(const std::filesystem::path &path, const QSize &size)
    {
        QImage image{size, QImage::Format_ARGB32_Premultiplied};
        for (int y = 0; y < size.height(); ++y)
        {
            for (int x = 0; x < size.width(); ++x)
            {
                image.setPixelColor(x, y, QColor{(x * 255) / size.width(), (y * 255) / size.height(), 128});
            }
        }
        return image.save(QString::fromStdWString(path.wstring()), "PNG");
    }

    /**
     * @brief 读一个文件的全部字节
     * @param path 目标文件
     * @return 文件内容；读不出来时为空
     */
    std::vector<char> readBytes(const std::filesystem::path &path)
    {
        std::ifstream stream{path, std::ios::binary};
        return std::vector<char>{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }
} // namespace

/// 三张图里两张命中、一张读不出来：命中即覆写，坏图原样留着并记失败
TEST_F(StepsAutoRedactorFixture, RedactsHitsAndCountsFailures)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    const auto root   = std::filesystem::path{directory.path().toStdWString()};
    const auto first  = root / "step-001.png";
    const auto second = root / "step-002.png";
    const auto broken = root / "step-003.png";
    ASSERT_TRUE(writeGradientPng(first, QSize{240, 160}));
    ASSERT_TRUE(writeGradientPng(second, QSize{240, 160}));
    // 零字节文件：QImage 读出来是空图，批扫器该把它记成失败而不是当成没命中
    std::ofstream emptyFile{broken, std::ios::binary};
    ASSERT_TRUE(emptyFile.good());
    emptyFile.close();
    ASSERT_TRUE(readBytes(broken).empty()) << "坏图应当是零字节";

    const QImage originalFirst{QString::fromStdWString(first.wstring())};
    ASSERT_FALSE(originalFirst.isNull());
    const std::vector<char> beforeFirst  = readBytes(first);
    const std::vector<char> beforeSecond = readBytes(second);
    ASSERT_FALSE(beforeFirst.empty());

    auto              engine = std::make_unique<PhoneEngine>();
    auto             *fake   = engine.get();
    StepsAutoRedactor redactor{*m_settings, std::move(engine)};

    int  scanned  = 0;
    int  redacted = 0;
    int  failed   = 0;
    bool finished = false;
    redactor.run({first, second, broken},
                 [&](const int scannedCount, const int redactedCount, const int failedCount)
                 {
                     scanned  = scannedCount;
                     redacted = redactedCount;
                     failed   = failedCount;
                     finished = true;
                 });

    ASSERT_TRUE(waitUntil([&] { return finished; })) << "一批没有收尾";
    EXPECT_FALSE(redactor.isRunning());
    EXPECT_EQ(scanned, 3) << "三张都要过一遍";
    EXPECT_EQ(redacted, 2) << "两张命中应当被抓出来";
    EXPECT_EQ(failed, 1) << "零字节那张应当记成失败";
    EXPECT_EQ(fake->calls, 2) << "坏图不该送到引擎";

    // 命中的两张真的被覆写；坏图一个字节都没动
    EXPECT_TRUE(readBytes(first) != beforeFirst) << "命中的图字节没变";
    EXPECT_TRUE(readBytes(second) != beforeSecond) << "命中的图字节没变";
    EXPECT_TRUE(readBytes(broken).empty()) << "坏图不该被写进东西";

    // 覆写发生在命中框里：同一块矩形内重读出来的像素与原来不同
    const QImage redactedFirst{QString::fromStdWString(first.wstring())};
    ASSERT_EQ(redactedFirst.size(), originalFirst.size());
    const QRect box     = hitBox();
    int         changed = 0;
    for (int y = box.top(); y <= box.bottom(); ++y)
    {
        for (int x = box.left(); x <= box.right(); ++x)
        {
            if (redactedFirst.pixelColor(x, y) != originalFirst.pixelColor(x, y))
            {
                ++changed;
            }
        }
    }
    EXPECT_GT(changed, 0) << "命中框里应当有像素被马赛克改掉";
}

/// 一批没跑完又来一批：后来这次当场收尾，第一批照旧跑完
TEST_F(StepsAutoRedactorFixture, FinishesTheSecondRunImmediately)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    const auto root   = std::filesystem::path{directory.path().toStdWString()};
    const auto first  = root / "step-001.png";
    const auto second = root / "step-002.png";
    ASSERT_TRUE(writeGradientPng(first, QSize{240, 160}));
    ASSERT_TRUE(writeGradientPng(second, QSize{240, 160}));

    auto              engine = std::make_unique<PhoneEngine>();
    StepsAutoRedactor redactor{*m_settings, std::move(engine)};

    int  redacted = 0;
    bool finished = false;
    redactor.run({first, second},
                 [&](const int, const int redactedCount, const int)
                 {
                     redacted = redactedCount;
                     finished = true;
                 });

    bool secondDone = false;
    redactor.run({first}, [&](const int, const int, const int) { secondDone = true; });
    // 调用方拿这一次的回调去开审核窗：不同步收尾就会让那扇窗永远等下去
    EXPECT_TRUE(secondDone) << "第二批的调用方没有被当场收尾";
    EXPECT_TRUE(redactor.isRunning()) << "第一批还在跑";

    ASSERT_TRUE(waitUntil([&] { return finished; })) << "第一批没有收尾";
    EXPECT_EQ(redacted, 2) << "第一批的图不该被第二批顶掉";
    EXPECT_FALSE(redactor.isRunning());
}
