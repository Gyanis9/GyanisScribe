// 识别搬运工的用例。一条假后端把 recognize() 停在门闩上等用例放行，四件事因此是可观察的：
// 引擎不在界面线程被调用、它阻塞期间界面循环还能接别的活、忙时后到的请求说了算、析构会等手上的识别跑完。

#include "Core/Types.h"
#include "Ocr/IOcrEngine.h"
#include "Ocr/OcrTypes.h"
#include "Ocr/RecognitionRunner.h"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QMetaObject>
#include <QObject>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringList>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

using GyanisScribe::Core::Error;
using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Core::Expected;
using GyanisScribe::Ocr::IOcrEngine;
using GyanisScribe::Ocr::OcrLine;
using GyanisScribe::Ocr::OcrResult;
using GyanisScribe::Ocr::OcrWord;
using GyanisScribe::Ocr::RecognitionRunner;

namespace
{
    // argv 的缓冲要活过 QApplication 的整个生命期，所以放在文件作用域
    char  applicationArgument[]  = "TestRecognitionRunner";
    char *applicationArguments[] = {applicationArgument, nullptr};

    std::unique_ptr<QApplication> application;

    /// 假后端停在门闩上的最长等待：比用例把界面循环转起来要的时间宽得多，
    /// 又保证「识别其实还堵在界面线程上」那一种实现能在有限时间内把控制权还给测试
    constexpr std::chrono::milliseconds kGatePatience{2000};

    /// 用例等条件成立的最长等待
    constexpr std::chrono::milliseconds kWaitPatience{2000};

    /// 假后端按语言分成败的暗号：不必为一条失败分支再配第二种后端
    const QString kFailingLanguage = QStringLiteral("fail");

    /// 一次 recognize() 调用的现场：谁调的、认的哪门语言、哪张图、门闩是被放开的还是自己等超时
    struct Invocation
    {
        std::thread::id thread;           ///< 调用发生在哪条线程
        QString         language;         ///< 请求的语言标签
        QSize           imageSize;        ///< 送去认的那张图的尺寸
        bool            released = false; ///< true 表示是用例放开的门闩
    };

    /**
     * @brief 会把 recognize() 停在门闩上的假后端
     * @details 观测面全是原子量或加锁快照，测试线程与工作线程各读各的，不靠调度先后。
     */
    class GatedOcrEngine final : public IOcrEngine
    {
    public:
        [[nodiscard]] QString name() const override
        {
            return QStringLiteral("假后端");
        }

        [[nodiscard]] bool isAvailable() const override
        {
            return true;
        }

        [[nodiscard]] QString unavailableReason() const override
        {
            return {};
        }

        [[nodiscard]] QStringList supportedLanguages() const override
        {
            return {QStringLiteral("zh-Hans"), QStringLiteral("en"), kFailingLanguage};
        }

        Expected<OcrResult> recognize(const QImage &image, const QString &language) override
        {
            {
                const std::lock_guard guard{m_mutex};
                m_invocations.push_back(Invocation{std::this_thread::get_id(), language, image.size(), false});
            }

            // 门闩：用例放开就走得快，没人放开就等个上限自己收尾——永不返回的用例不是好用例
            const bool released = m_gate.try_acquire_for(kGatePatience);

            {
                const std::lock_guard guard{m_mutex};
                m_invocations.back().released = released;
            }
            m_finished = true;

            if (language == kFailingLanguage)
            {
                return std::unexpected(Error{ErrorCode::InternalError, "假后端说这一门语言它认不了"});
            }

            OcrResult result;
            result.language          = language;
            result.averageConfidence = 0.9;
            OcrLine line;
            line.words       = {OcrWord{language, QRect{0, 0, 8, 8}}};
            line.boundingBox = QRect{0, 0, 8, 8};
            result.lines     = {line};
            return result;
        }

        /// 放开一次门闩，让一次被拦住的识别走完
        void release()
        {
            m_gate.release();
        }

        /// 已经进来的调用次数
        [[nodiscard]] int invocationCount() const
        {
            const std::lock_guard guard{m_mutex};
            return static_cast<int>(m_invocations.size());
        }

        /// 第 index 次调用的现场，越界返回空
        [[nodiscard]] std::optional<Invocation> invocationAt(const int index) const
        {
            const std::lock_guard guard{m_mutex};
            if (index < 0 || static_cast<size_t>(index) >= m_invocations.size())
            {
                return std::nullopt;
            }
            return m_invocations[static_cast<size_t>(index)];
        }

        /// 最近一次 recognize() 有没有返回
        [[nodiscard]] bool isFinished() const
        {
            return m_finished;
        }

    private:
        mutable std::mutex          m_mutex;           ///< 保护 m_invocations
        std::vector<Invocation>     m_invocations;     ///< 按调用顺序记下的现场
        std::counting_semaphore<16> m_gate{0};         ///< 门闩：用例 release 一次就放行一次调用
        std::atomic<bool>           m_finished{false}; ///< 最近一次调用是否已返回
    };

    /// 结果窗那一份「这一轮收到了什么」：两条槽各记一份，用例据此判先后
    struct Deliveries
    {
        int             succeededCount = 0;
        int             failedCount    = 0;
        QImage          image;
        QString         language;
        QString         errorText;
        std::thread::id deliveryThread = std::thread::id{};
    };

    /**
     * @brief 有界轮询等条件成立，其间把界面循环转起来
     * @param predicate 要等的条件
     * @return 条件成立返回 true；等到上限仍不成立返回 false
     * @details 条件由被测方决定（调用进来了、结果回来了），不是「睡一个大家都够用的时长」：
     *          实现阻塞时这里等到的就是超时，用例据此判红而不是挂住。
     */
    template<typename Predicate>
    bool waitUntil(const Predicate &predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + kWaitPatience;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
            {
                return true;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return predicate();
    }

    /// 一张能按尺寸认出来的图
    QImage makeImage(const int width, const int height)
    {
        QImage image{QSize{width, height}, QImage::Format_ARGB32};
        image.fill(QColor{180, 180, 180});
        return image;
    }

    /// 把 runner 的两条出口接到一份 Deliveries 上
    void collect(RecognitionRunner &runner, Deliveries &deliveries)
    {
        QObject::connect(&runner, &RecognitionRunner::succeeded,
                         [&deliveries](const QImage &image, const QString &language, const OcrResult &result)
                         {
                             ++deliveries.succeededCount;
                             deliveries.image          = image;
                             deliveries.language       = language;
                             deliveries.errorText      = result.isEmpty() ? QStringLiteral("空结果") : result.text();
                             deliveries.deliveryThread = std::this_thread::get_id();
                         });
        QObject::connect(&runner, &RecognitionRunner::failed,
                         [&deliveries](const Error &error)
                         {
                             ++deliveries.failedCount;
                             deliveries.errorText      = QString::fromStdString(error.message);
                             deliveries.deliveryThread = std::this_thread::get_id();
                         });
    }

    class RecognitionRunnerFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            int argumentCount = 1;
            application       = std::make_unique<QApplication>(argumentCount, applicationArguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }
    };

    TEST_F(RecognitionRunnerFixture, EngineRunsOffTheGuiThreadWhileTheGuiLoopStillWorks)
    {
        GatedOcrEngine    engine;
        RecognitionRunner runner{engine};
        Deliveries        deliveries;
        collect(runner, deliveries);

        runner.request(makeImage(40, 20), QStringLiteral("zh-Hans"));

        ASSERT_TRUE(waitUntil([&] { return engine.invocationCount() >= 1; })) << "识别一次都没进来";
        // 调用已经返回、活却还没干完，这才证明 recognize() 不是跑在界面线程上
        EXPECT_FALSE(engine.isFinished()) << "request() 返回时识别已经跑完：它还堵在界面线程上";

        bool guiTickRan = false;
        QMetaObject::invokeMethod(&runner, [&guiTickRan] { guiTickRan = true; }, Qt::QueuedConnection);
        ASSERT_TRUE(waitUntil([&] { return guiTickRan; })) << "识别占用期间界面循环接不到别的活";
        EXPECT_FALSE(engine.isFinished()) << "界面循环转完之前识别就完了：门闩没起作用";

        engine.release();
        ASSERT_TRUE(waitUntil([&] { return deliveries.succeededCount == 1; })) << "放开门闩之后没收到结果";
        EXPECT_EQ(deliveries.deliveryThread, std::this_thread::get_id()) << "结果不在界面线程上送，控制器不能在这儿建窗口";
        EXPECT_EQ(deliveries.language, QStringLiteral("zh-Hans"));
        EXPECT_EQ(deliveries.image.size(), QSize(40, 20));
        EXPECT_EQ(deliveries.failedCount, 0);
    }

    TEST_F(RecognitionRunnerFixture, TheNewestRequestWinsWhileOneIsRunning)
    {
        GatedOcrEngine    engine;
        RecognitionRunner runner{engine};
        Deliveries        deliveries;
        collect(runner, deliveries);

        runner.request(makeImage(40, 20), QStringLiteral("zh-Hans"));
        ASSERT_TRUE(waitUntil([&] { return engine.invocationCount() >= 1; })) << "第一条识别没进来";

        // 忙时的第二条不排队堆叠，只把最新这一条攒着
        runner.request(makeImage(60, 30), QStringLiteral("en"));
        engine.release();

        ASSERT_TRUE(waitUntil([&] { return engine.invocationCount() == 2; })) << "攒着的最新一条没被认出去";
        const auto second = engine.invocationAt(1);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(second->language, QStringLiteral("en")) << "第二条认的不是用户最后要的那门语言";
        EXPECT_EQ(second->imageSize, QSize(60, 30)) << "第二条认的不是用户最后要的那张图";

        engine.release();
        ASSERT_TRUE(waitUntil([&] { return deliveries.succeededCount == 2; })) << "第二条的结果没送到";
        EXPECT_EQ(deliveries.language, QStringLiteral("en"));
        EXPECT_EQ(deliveries.image.size(), QSize(60, 30)) << "最后落到界面的是旧那一张：方框会跟新图错位";
    }

    TEST_F(RecognitionRunnerFixture, FailureComesBackOnTheFailedChannel)
    {
        GatedOcrEngine    engine;
        RecognitionRunner runner{engine};
        Deliveries        deliveries;
        collect(runner, deliveries);

        runner.request(makeImage(40, 20), kFailingLanguage);
        ASSERT_TRUE(waitUntil([&] { return engine.invocationCount() >= 1; })) << "识别一次都没进来";
        engine.release();

        ASSERT_TRUE(waitUntil([&] { return deliveries.failedCount == 1; })) << "失败没走 failed 出口";
        EXPECT_EQ(deliveries.succeededCount, 0) << "失败的同时又报成功，界面会拿空结果开窗";
        EXPECT_NE(deliveries.errorText.indexOf(QStringLiteral("认不了")), -1) << deliveries.errorText.toStdString();
        EXPECT_EQ(deliveries.deliveryThread, std::this_thread::get_id());
    }

    TEST_F(RecognitionRunnerFixture, DestructionWaitsForTheRecognitionInFlight)
    {
        // 引擎比 runner 活得久：析构不等的后果是工作线程在引擎身后继续用它，这里要钉住「等」
        GatedOcrEngine engine;
        auto           runner = std::make_unique<RecognitionRunner>(engine);

        runner->request(makeImage(40, 20), QStringLiteral("zh-Hans"));
        ASSERT_TRUE(waitUntil([&] { return engine.invocationCount() >= 1; })) << "识别一次都没进来";

        runner.reset();
        EXPECT_TRUE(engine.isFinished()) << "析构直接返回了：手上的识别还没跑完，引擎就已经没人管了";
    }

} // namespace
