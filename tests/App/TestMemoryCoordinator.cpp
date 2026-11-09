// 记忆协调器的直测：首次授权只攒一条截图、开启后才入库；关闭时只停队列不删库；
// OCR 失败保留行并可重试；启动恢复待识别行；移除、清空与重建索引都按路径与来源收口。
// 假引擎按调用顺序给结果，并用临时数据库与临时设置目录隔离每条用例。

#include "App/MemoryCoordinator.h"

#include "Base/Logger.h"
#include "Core/Capture/SourceInfo.h"
#include "Core/Memory/MemoryIndex.h"
#include "Core/Memory/MemoryTypes.h"
#include "Core/Ocr/IOcrEngine.h"
#include "Core/Ocr/OcrLanguage.h"
#include "Core/Ocr/OcrTypes.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"
#include "Gui/Controls/Toast.h"

#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QMetaType>
#include <QSignalSpy>
#include <QSize>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QVariant>

#include <gtest/gtest.h>

#include <atomic>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

Q_DECLARE_METATYPE(GyanisScribe::Gui::ToastKind)

using Coordinator = GyanisScribe::App::MemoryCoordinator;
using GyanisScribe::Base::Error;
using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Base::Expected;
using GyanisScribe::Core::CaptureSourceInfo;
using GyanisScribe::Core::IOcrEngine;
using GyanisScribe::Core::kDefaultOcrLanguage;
using GyanisScribe::Core::kMaxOcrTextBytes;
using GyanisScribe::Core::kMemoryConsentAsked;
using GyanisScribe::Core::kMemoryEnabled;
using GyanisScribe::Core::MemoryIndex;
using GyanisScribe::Core::MemoryQuery;
using GyanisScribe::Core::MemoryResult;
using GyanisScribe::Core::MemorySource;
using GyanisScribe::Core::MemoryStats;
using GyanisScribe::Core::MemoryUpsert;
using GyanisScribe::Core::OcrLine;
using GyanisScribe::Core::OcrResult;
using GyanisScribe::Core::OcrWord;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Gui::ToastKind;

namespace
{
    /// 等一次识别落地的最长时限：到点判红，而不是让用例挂住
    constexpr int kWaitPatienceMs = 5'000;

    std::unique_ptr<QCoreApplication> application;

    std::filesystem::path pathFromQString(const QString &text)
    {
        return std::filesystem::path(text.toStdWString());
    }

    QString displayPath(const std::filesystem::path &path)
    {
        return QString::fromStdWString(path.wstring());
    }

    /// 造一份一行一词的识别结果
    OcrResult makeResult(const QString &text)
    {
        OcrResult result;
        result.language = QString::fromUtf8(kDefaultOcrLanguage);

        OcrLine line;
        line.boundingBox = QRect{0, 0, 16, 8};
        line.words       = {OcrWord{text, QRect{0, 0, 16, 8}}};
        result.lines     = {line};
        return result;
    }

    Expected<OcrResult> failedResult()
    {
        return std::unexpected(Error{ErrorCode::InternalError, "测试替身：识别失败"});
    }

    /**
     * @brief 按顺序返回预设结果的识别后端
     * @details 预设用完后再被调用会返回失败，让「多调了一次」直接暴露；可用性可切换，供暂停与恢复的用例。
     */
    class FakeOcrEngine final : public IOcrEngine
    {
    public:
        explicit FakeOcrEngine(std::deque<Expected<OcrResult>> outcomes) : m_outcomes(std::move(outcomes))
        {
        }

        [[nodiscard]] QString name() const override
        {
            return QStringLiteral("测试替身");
        }

        [[nodiscard]] bool isAvailable() const override
        {
            std::scoped_lock lock(m_mutex);
            return m_available;
        }

        [[nodiscard]] QString unavailableReason() const override
        {
            std::scoped_lock lock(m_mutex);
            return m_available ? QString{} : m_unavailableReason;
        }

        [[nodiscard]] QStringList supportedLanguages() const override
        {
            return {QStringLiteral("zh-Hans"), QStringLiteral("en")};
        }

        [[nodiscard]] Expected<OcrResult> recognize(const QImage &, const QString &language) override
        {
            std::scoped_lock lock(m_mutex);
            ++m_calls;
            m_languages.append(language);
            if (m_outcomes.empty())
            {
                return std::unexpected(Error{ErrorCode::InternalError, "测试替身：没有更多预设的识别结果"});
            }

            Expected<OcrResult> outcome = std::move(m_outcomes.front());
            m_outcomes.pop_front();
            return outcome;
        }

        /// 关掉可用性并给出原因；只在请求之前调用
        void setUnavailable(QString reason)
        {
            std::scoped_lock lock(m_mutex);
            m_available         = false;
            m_unavailableReason = std::move(reason);
        }

        /// 恢复可用性
        void setAvailable()
        {
            std::scoped_lock lock(m_mutex);
            m_available = true;
            m_unavailableReason.clear();
        }

        /// 已进入识别体的次数
        [[nodiscard]] int calls() const
        {
            std::scoped_lock lock(m_mutex);
            return m_calls;
        }

        /// 每次识别收到的语言标签
        [[nodiscard]] QStringList languages() const
        {
            std::scoped_lock lock(m_mutex);
            return m_languages;
        }

    private:
        mutable std::mutex              m_mutex;             ///< 保护下面全部字段
        std::deque<Expected<OcrResult>> m_outcomes;          ///< 按调用顺序发放的结果
        bool                            m_available = true;  ///< 可用性
        QString                         m_unavailableReason; ///< 不可用原因
        int                             m_calls = 0;         ///< 进入识别体的次数
        QStringList                     m_languages;         ///< 每次收到的语言标签
    };

    std::optional<MemoryResult> findResult(MemoryIndex &index, const std::filesystem::path &path)
    {
        auto results = index.search(MemoryQuery{});
        if (!results)
        {
            return std::nullopt;
        }
        for (const MemoryResult &result: *results)
        {
            if (result.filePath == path)
            {
                return result;
            }
        }
        return std::nullopt;
    }

    bool hasStatus(MemoryIndex &index, const std::filesystem::path &path, const QString &status)
    {
        const auto result = findResult(index, path);
        return result.has_value() && result->ocrStatus == status;
    }

    /// 等条件成立，期间处理事件；到点返回 false，交给用例断言
    bool waitUntil(const std::function<bool()> &condition, const int timeoutMs = kWaitPatienceMs)
    {
        QElapsedTimer timer;
        timer.start();
        while (!condition())
        {
            if (timer.elapsed() >= timeoutMs)
            {
                return false;
            }
            QTest::qWait(10);
        }
        return true;
    }

    int resultCount(MemoryIndex &index)
    {
        auto results = index.search(MemoryQuery{});
        return results ? static_cast<int>(results->size()) : -1;
    }

    /// 直接从数据库读回某条目的 OCR 字符数与截断标记；search 的摘要看不全这两列
    std::optional<std::pair<int, bool>> storedOcrSummary(const std::filesystem::path &databasePath, const std::filesystem::path &shotPath)
    {
        static std::atomic<int> connectionCounter{0};
        const QString           connectionName = QStringLiteral("TestMemoryCoordinatorRaw-%1").arg(connectionCounter.fetch_add(1));

        std::optional<std::pair<int, bool>> summary;
        {
            QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            database.setDatabaseName(displayPath(databasePath));
            if (database.open())
            {
                QSqlQuery query{database};
                query.prepare(QStringLiteral("SELECT length(ocr_text), truncated FROM items WHERE path_key = ?"));
                query.addBindValue(MemoryIndex::pathKey(shotPath));
                if (query.exec() && query.next())
                {
                    summary = std::make_pair(query.value(0).toInt(), query.value(1).toInt() != 0);
                }
            }
            database.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
        return summary;
    }

    class MemoryCoordinator : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestAppMemoryCoordinator";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QCoreApplication>(argumentCount, arguments);

            qRegisterMetaType<MemoryStats>("GyanisScribe::Core::MemoryStats");
            qRegisterMetaType<GyanisScribe::Gui::ToastKind>("GyanisScribe::Gui::ToastKind");

            const auto logDirectory = std::filesystem::temp_directory_path() / "GyanisScribe-MemoryCoordinator-logs";
            static_cast<void>(GyanisScribe::Base::Logger::initialize(logDirectory, GyanisScribe::Base::Severity::Error));
        }

        static void TearDownTestSuite()
        {
            GyanisScribe::Base::Logger::shutdown();
            application.reset();
        }

        void SetUp() override
        {
            ASSERT_TRUE(m_temp.isValid());
        }

        [[nodiscard]] std::filesystem::path root() const
        {
            return pathFromQString(m_temp.path());
        }

        [[nodiscard]] std::filesystem::path settingsPath() const
        {
            return root() / "settings.ini";
        }

        [[nodiscard]] std::filesystem::path databasePath() const
        {
            return root() / "memory-index.sqlite";
        }

        [[nodiscard]] std::filesystem::path makeShot(const QString &name, const QColor &color, const QSize &size = QSize{32, 24}) const
        {
            const auto path = root() / std::filesystem::path{name.toStdWString()};
            QImage     image{size, QImage::Format_ARGB32};
            image.fill(color);
            EXPECT_TRUE(image.save(displayPath(path), "PNG"));
            return path;
        }

        QTemporaryDir m_temp; ///< 这一格用例自己的临时目录
    };
} // namespace

/**
 * @brief 第一张可索引截图只触发授权，不入库也不识别
 */
TEST_F(MemoryCoordinator, FirstScreenshotAsksForConsentWithoutIndexing)
{
    SettingsStore settings{settingsPath()};

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{makeResult(QStringLiteral("授权后文本"))});
    auto       *fake   = engine.get();
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    QSignalSpy  consent(&coordinator, &Coordinator::consentRequested);

    coordinator.start();
    coordinator.noteScreenshot(makeShot(QStringLiteral("first.png"), Qt::red), QSize{32, 24}, CaptureSourceInfo{QStringLiteral("notepad.exe"), QStringLiteral("标题")});

    EXPECT_TRUE(coordinator.needsConsent());
    EXPECT_EQ(consent.count(), 1) << "第一张截图应当只问一次授权";
    EXPECT_EQ(resultCount(coordinator.index()), 0) << "授权前不该建立索引行";
    EXPECT_EQ(fake->calls(), 0) << "授权前不该开始识别";

    coordinator.noteScreenshot(makeShot(QStringLiteral("second.png"), Qt::blue), QSize{32, 24}, CaptureSourceInfo{});
    EXPECT_EQ(consent.count(), 1) << "等待回答期间再来截图不该重复请求授权";
}

/**
 * @brief 同意后写下两个设置，并把等待中的那一条入库、识别完成
 */
TEST_F(MemoryCoordinator, ResolvingConsentEnablesAndIndexesThePendingShot)
{
    SettingsStore settings{settingsPath()};

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{makeResult(QStringLiteral("识别文本"))});
    auto       *fake   = engine.get();
    Coordinator coordinator{settings, databasePath(), std::move(engine)};

    coordinator.start();
    const auto shot = makeShot(QStringLiteral("pending.png"), Qt::green);
    coordinator.noteScreenshot(shot, QSize{32, 24}, CaptureSourceInfo{QStringLiteral("Notepad.EXE"), QStringLiteral("无标题")});
    ASSERT_TRUE(coordinator.needsConsent());

    coordinator.resolveConsent(true);

    EXPECT_TRUE(settings.boolValue(kMemoryEnabled, false));
    EXPECT_TRUE(settings.boolValue(kMemoryConsentAsked, false));
    EXPECT_FALSE(coordinator.needsConsent());

    ASSERT_TRUE(findResult(coordinator.index(), shot).has_value()) << "同意后等待中的截图没有入库";
    EXPECT_TRUE(waitUntil([&] { return hasStatus(coordinator.index(), shot, QStringLiteral("done")); }));

    const auto result = findResult(coordinator.index(), shot);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ocrSnippet, QStringLiteral("识别文本"));
    EXPECT_EQ(result->processName, QStringLiteral("notepad")) << "进程名应归一为去掉 .exe 的小写名";
    EXPECT_EQ(result->windowTitle, QStringLiteral("无标题"));
    EXPECT_EQ(fake->calls(), 1);
    ASSERT_EQ(fake->languages().size(), 1);
    EXPECT_EQ(fake->languages().first(), QString::fromUtf8(kDefaultOcrLanguage));
}

/**
 * @brief 暂不授权会记住已问过并丢掉等待中的截图，之后不再主动询问
 */
TEST_F(MemoryCoordinator, DecliningConsentRemembersAndDropsTheShot)
{
    SettingsStore settings{settingsPath()};

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{});
    auto       *fake   = engine.get();
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    QSignalSpy  consent(&coordinator, &Coordinator::consentRequested);

    coordinator.start();
    coordinator.noteScreenshot(makeShot(QStringLiteral("declined.png"), Qt::yellow), QSize{32, 24}, CaptureSourceInfo{});
    ASSERT_EQ(consent.count(), 1);

    coordinator.resolveConsent(false);

    EXPECT_FALSE(settings.boolValue(kMemoryEnabled, true));
    EXPECT_TRUE(settings.boolValue(kMemoryConsentAsked, false));
    EXPECT_FALSE(coordinator.needsConsent());
    EXPECT_EQ(resultCount(coordinator.index()), 0) << "暂不授权不该留下索引行";
    EXPECT_EQ(fake->calls(), 0);

    coordinator.noteScreenshot(makeShot(QStringLiteral("later.png"), Qt::magenta), QSize{32, 24}, CaptureSourceInfo{});
    EXPECT_EQ(consent.count(), 1) << "已经问过之后不该再请求授权";
    EXPECT_EQ(resultCount(coordinator.index()), 0);
}

/**
 * @brief 关闭状态且已经问过授权时，截图直接丢弃
 */
TEST_F(MemoryCoordinator, DisabledCoordinatorDoesNotIndex)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, false).has_value());
    ASSERT_TRUE(settings.setValue(kMemoryConsentAsked, true).has_value());

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{});
    auto       *fake   = engine.get();
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    QSignalSpy  consent(&coordinator, &Coordinator::consentRequested);

    coordinator.start();
    coordinator.noteScreenshot(makeShot(QStringLiteral("disabled.png"), Qt::cyan), QSize{32, 24}, CaptureSourceInfo{QStringLiteral("chrome.exe"), QStringLiteral("网页")});

    EXPECT_EQ(resultCount(coordinator.index()), 0);
    EXPECT_EQ(consent.count(), 0);
    EXPECT_EQ(fake->calls(), 0);
}

/**
 * @brief 排除列表先于授权判断：被排除的程序既不询问授权也不入库
 */
TEST_F(MemoryCoordinator, ExcludedProcessSkipsConsent)
{
    SettingsStore settings{settingsPath()};

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{});
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    QSignalSpy  consent(&coordinator, &Coordinator::consentRequested);

    coordinator.start();
    coordinator.setExcludedProcesses({QStringLiteral("Notepad.EXE")});
    coordinator.noteScreenshot(makeShot(QStringLiteral("excluded.png"), Qt::red), QSize{32, 24}, CaptureSourceInfo{QStringLiteral("notepad.exe"), QStringLiteral("标题")});

    EXPECT_EQ(consent.count(), 0) << "排除名单里的程序不该触发首次授权";
    EXPECT_TRUE(coordinator.needsConsent()) << "排除的判断不该顺手把授权问题用过";
    EXPECT_EQ(resultCount(coordinator.index()), 0);

    coordinator.noteScreenshot(makeShot(QStringLiteral("allowed.png"), Qt::blue), QSize{32, 24}, CaptureSourceInfo{QStringLiteral("chrome.exe"), QStringLiteral("网页")});
    EXPECT_EQ(consent.count(), 1) << "不在排除名单里的第一张截图仍应询问授权";
}

/**
 * @brief 截图任务按入队顺序识别，不因后一张到来而乱序
 */
TEST_F(MemoryCoordinator, CaptureQueueKeepsFifoOrder)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());
    ASSERT_TRUE(settings.setValue(kMemoryConsentAsked, true).has_value());

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{makeResult(QStringLiteral("第一张")), makeResult(QStringLiteral("第二张"))});
    auto       *fake   = engine.get();
    Coordinator coordinator{settings, databasePath(), std::move(engine)};

    coordinator.start();
    const auto first  = makeShot(QStringLiteral("first.png"), Qt::red);
    const auto second = makeShot(QStringLiteral("second.png"), Qt::blue);

    coordinator.noteScreenshot(first, QSize{32, 24}, CaptureSourceInfo{});
    coordinator.noteScreenshot(second, QSize{32, 24}, CaptureSourceInfo{});

    EXPECT_TRUE(waitUntil([&] { return hasStatus(coordinator.index(), first, QStringLiteral("done")) && hasStatus(coordinator.index(), second, QStringLiteral("done")); }));

    const auto firstResult  = findResult(coordinator.index(), first);
    const auto secondResult = findResult(coordinator.index(), second);
    ASSERT_TRUE(firstResult.has_value());
    ASSERT_TRUE(secondResult.has_value());
    EXPECT_EQ(firstResult->ocrSnippet, QStringLiteral("第一张")) << "先入队的截图应当先识别";
    EXPECT_EQ(secondResult->ocrSnippet, QStringLiteral("第二张"));
    EXPECT_EQ(fake->calls(), 2);
}

/**
 * @brief 识别失败保留行与错误，重试后状态回到完成
 */
TEST_F(MemoryCoordinator, OcrFailureKeepsRowAndRetryRecovers)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{failedResult(), makeResult(QStringLiteral("重试文本"))});
    auto       *fake   = engine.get();
    Coordinator coordinator{settings, databasePath(), std::move(engine)};

    coordinator.start();
    const auto shot = makeShot(QStringLiteral("retry.png"), Qt::darkGreen);
    coordinator.noteScreenshot(shot, QSize{32, 24}, CaptureSourceInfo{});

    EXPECT_TRUE(waitUntil([&] { return hasStatus(coordinator.index(), shot, QStringLiteral("failed")); }));
    const auto failed = findResult(coordinator.index(), shot);
    ASSERT_TRUE(failed.has_value());
    EXPECT_EQ(failed->ocrStatus, QStringLiteral("failed"));
    EXPECT_EQ(fake->calls(), 1);

    coordinator.retryOcr(failed->id);

    EXPECT_TRUE(waitUntil([&] { return hasStatus(coordinator.index(), shot, QStringLiteral("done")); }));
    const auto recovered = findResult(coordinator.index(), shot);
    ASSERT_TRUE(recovered.has_value());
    EXPECT_EQ(recovered->ocrSnippet, QStringLiteral("重试文本"));
    EXPECT_EQ(fake->calls(), 2);
}

/**
 * @brief 重新识别失败保留上一次的文本与指纹，旧文本仍可被搜到
 */
TEST_F(MemoryCoordinator, FailedRetryKeepsPreviousTextSearchable)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{makeResult(QStringLiteral("旧文本 报价单")), failedResult()});
    auto       *fake   = engine.get();
    Coordinator coordinator{settings, databasePath(), std::move(engine)};

    coordinator.start();
    const auto shot = makeShot(QStringLiteral("failed-retry-text.png"), Qt::darkYellow);
    coordinator.noteScreenshot(shot, QSize{32, 24}, CaptureSourceInfo{});
    ASSERT_TRUE(waitUntil([&] { return hasStatus(coordinator.index(), shot, QStringLiteral("done")); }));

    const auto succeeded = findResult(coordinator.index(), shot);
    ASSERT_TRUE(succeeded.has_value());
    ASSERT_EQ(succeeded->ocrSnippet, QStringLiteral("旧文本 报价单"));

    coordinator.retryOcr(succeeded->id);

    ASSERT_TRUE(waitUntil([&] { return hasStatus(coordinator.index(), shot, QStringLiteral("failed")); }));
    const auto failed = findResult(coordinator.index(), shot);
    ASSERT_TRUE(failed.has_value());
    EXPECT_EQ(failed->ocrStatus, QStringLiteral("failed"));
    EXPECT_EQ(failed->ocrSnippet, QStringLiteral("旧文本 报价单")) << "重新识别失败不该清掉上一次的文本";
    EXPECT_EQ(failed->dhash, succeeded->dhash) << "重新识别失败不该清掉指纹";
    EXPECT_EQ(fake->calls(), 2);

    MemoryQuery query;
    query.text      = QStringLiteral("报价");
    const auto hits = coordinator.index().search(query);
    ASSERT_TRUE(hits.has_value());
    ASSERT_EQ(hits->size(), 1U) << "旧文本应当仍留在全文索引里";
    EXPECT_EQ(hits->front().id, succeeded->id);
}

/**
 * @brief 启动时把库中的待识别行重新入队并写回文本
 */
TEST_F(MemoryCoordinator, PendingRowsAreRecoveredOnStart)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{makeResult(QStringLiteral("恢复文本"))});
    auto       *fake   = engine.get();
    Coordinator coordinator{settings, databasePath(), std::move(engine)};

    const auto shot = makeShot(QStringLiteral("pending-recovered.png"), Qt::darkBlue);
    ASSERT_TRUE(coordinator.index().open().has_value());
    MemoryUpsert entry;
    entry.filePath    = shot;
    entry.source      = MemorySource::Capture;
    entry.capturedAt  = QDateTime::currentDateTime();
    entry.size        = QSize{32, 24};
    entry.processName = QStringLiteral("code");
    ASSERT_TRUE(coordinator.index().upsertPending(entry).has_value());

    coordinator.start();

    EXPECT_TRUE(waitUntil([&] { return hasStatus(coordinator.index(), shot, QStringLiteral("done")); }));
    const auto result = findResult(coordinator.index(), shot);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ocrSnippet, QStringLiteral("恢复文本"));
    EXPECT_EQ(fake->calls(), 1);
}

/**
 * @brief 按路径删除、清截图来源与清空全部各自只动该动的行
 */
TEST_F(MemoryCoordinator, RemoveAndClearUpdateRows)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{makeResult(QStringLiteral("一")), makeResult(QStringLiteral("二"))});
    Coordinator coordinator{settings, databasePath(), std::move(engine)};

    coordinator.start();
    const auto first  = makeShot(QStringLiteral("first.png"), Qt::red);
    const auto second = makeShot(QStringLiteral("second.png"), Qt::blue);
    coordinator.noteScreenshot(first, QSize{32, 24}, CaptureSourceInfo{});
    coordinator.noteScreenshot(second, QSize{32, 24}, CaptureSourceInfo{});
    EXPECT_TRUE(waitUntil([&] { return hasStatus(coordinator.index(), first, QStringLiteral("done")) && hasStatus(coordinator.index(), second, QStringLiteral("done")); }));

    const auto   imported = makeShot(QStringLiteral("imported.png"), Qt::gray);
    MemoryUpsert importEntry;
    importEntry.filePath   = imported;
    importEntry.source     = MemorySource::Import;
    importEntry.capturedAt = QDateTime::currentDateTime();
    importEntry.size       = QSize{32, 24};
    ASSERT_TRUE(coordinator.index().upsertPending(importEntry).has_value());

    coordinator.removePath(first);
    EXPECT_FALSE(findResult(coordinator.index(), first).has_value()) << "按路径删除没有去掉对应的行";
    EXPECT_TRUE(findResult(coordinator.index(), second).has_value());
    EXPECT_TRUE(findResult(coordinator.index(), imported).has_value());

    coordinator.clearCaptureEntries();
    EXPECT_FALSE(findResult(coordinator.index(), second).has_value()) << "清截图来源没有删掉截图行";
    EXPECT_TRUE(findResult(coordinator.index(), imported).has_value()) << "清截图来源误删了导入行";

    coordinator.clearAll();
    EXPECT_EQ(resultCount(coordinator.index()), 0);
}

/**
 * @brief 数据库打不开时发一次错误与一条中文提示，不崩溃
 */
TEST_F(MemoryCoordinator, DatabaseErrorSurfacesMessage)
{
    SettingsStore settings{settingsPath()};

    const auto blocker = root() / "not-a-directory";
    QFile      blockerFile{displayPath(blocker)};
    ASSERT_TRUE(blockerFile.open(QIODevice::WriteOnly));
    static_cast<void>(blockerFile.write("blocker"));
    blockerFile.close();

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{});
    Coordinator coordinator{settings, blocker / "memory-index.sqlite", std::move(engine)};
    QSignalSpy  databaseErrors(&coordinator, &Coordinator::databaseError);
    QSignalSpy  messages(&coordinator, &Coordinator::messageForUser);

    coordinator.start();

    ASSERT_EQ(databaseErrors.count(), 1) << "库打不开应发一次 databaseError";
    EXPECT_FALSE(databaseErrors.at(0).at(0).toString().isEmpty());
    ASSERT_EQ(messages.count(), 1) << "库打不开应给一条中文提示";
    EXPECT_FALSE(messages.at(0).at(0).toString().isEmpty());
    EXPECT_EQ(messages.at(0).at(1).value<ToastKind>(), ToastKind::Danger);
}

/**
 * @brief 重建索引把旧库改名留档，新库可打开且为空
 */
TEST_F(MemoryCoordinator, RebuildIndexMovesOldDatabaseAndReopens)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{makeResult(QStringLiteral("旧文本"))});
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    QSignalSpy  changed(&coordinator, &Coordinator::changed);
    QSignalSpy  stats(&coordinator, &Coordinator::statsChanged);

    coordinator.start();
    const auto shot = makeShot(QStringLiteral("before-rebuild.png"), Qt::darkRed);
    coordinator.noteScreenshot(shot, QSize{32, 24}, CaptureSourceInfo{});
    EXPECT_TRUE(waitUntil([&] { return hasStatus(coordinator.index(), shot, QStringLiteral("done")); }));
    ASSERT_TRUE(std::filesystem::exists(databasePath()));

    coordinator.rebuildIndex();

    EXPECT_TRUE(coordinator.index().isOpen());
    const auto results = coordinator.index().search(MemoryQuery{});
    ASSERT_TRUE(results.has_value());
    EXPECT_TRUE(results->empty()) << "重建后新库应当是空的";

    bool foundCorrupt = false;
    for (const auto &entry: std::filesystem::directory_iterator(root()))
    {
        const QString name = QString::fromStdWString(entry.path().filename().wstring());
        if (name.startsWith(QStringLiteral("memory-index.corrupt-")) && name.endsWith(QStringLiteral(".sqlite")))
        {
            foundCorrupt = true;
        }
    }
    EXPECT_TRUE(foundCorrupt) << "旧库应改名为 memory-index.corrupt-<时间戳>.sqlite 留档";
    EXPECT_GT(changed.count(), 0);
    EXPECT_GT(stats.count(), 0);
}

/**
 * @brief 重建索引复用同一个索引对象，重建前拿到的引用仍能写入与搜索
 */
TEST_F(MemoryCoordinator, RebuildKeepsSameIndexObject)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{});
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    coordinator.start();
    ASSERT_TRUE(coordinator.index().isOpen());

    const MemoryIndex *before = &coordinator.index();
    coordinator.rebuildIndex();

    ASSERT_TRUE(coordinator.index().isOpen());
    ASSERT_EQ(before, &coordinator.index()) << "重建索引应复用同一个索引对象，不能让旧引用悬空";

    const auto   shot = makeShot(QStringLiteral("after-rebuild.png"), Qt::darkCyan);
    MemoryUpsert entry;
    entry.filePath   = shot;
    entry.source     = MemorySource::Capture;
    entry.capturedAt = QDateTime::currentDateTime();
    entry.size       = QSize{32, 24};
    ASSERT_TRUE(coordinator.index().upsertPending(entry).has_value());

    const auto results = before->search(MemoryQuery{});
    ASSERT_TRUE(results.has_value());
    ASSERT_EQ(results->size(), 1U) << "重建后的新库应能通过重建前的引用查到新行";
    EXPECT_EQ(results->front().filePath, shot);
}

/**
 * @brief 主键不在最近一页搜索结果里时，重试仍能按主键找到旧行
 */
TEST_F(MemoryCoordinator, RetryFindsRowOutsideRecentSearch)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());
    ASSERT_TRUE(settings.setValue(kMemoryConsentAsked, true).has_value());

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{makeResult(QStringLiteral("很久以前"))});
    auto       *fake   = engine.get();
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    ASSERT_TRUE(coordinator.index().open().has_value());

    // 最旧一行先入库并标记失败，随后 249 行完成；按时间倒序它不在最近 200 条里
    const auto moment     = QDateTime::currentDateTime();
    const auto oldestShot = makeShot(QStringLiteral("oldest.png"), Qt::darkMagenta);
    qint64     oldestId   = 0;
    for (int index = 0; index < 250; ++index)
    {
        MemoryUpsert entry;
        entry.filePath   = index == 0 ? oldestShot : root() / std::filesystem::path{QStringLiteral("synthetic-%1.png").arg(index).toStdWString()};
        entry.source     = MemorySource::Capture;
        entry.capturedAt = moment.addSecs(index);
        entry.size       = QSize{32, 24};

        const auto id = coordinator.index().upsertPending(entry);
        ASSERT_TRUE(id.has_value());
        if (index == 0)
        {
            oldestId = *id;
        } else
        {
            ASSERT_TRUE(coordinator.index().updateOcr(*id, QStringLiteral("第 %1 条").arg(index), QString{}, QStringLiteral("done"), QString{}, false).has_value());
        }
    }
    ASSERT_TRUE(coordinator.index().updateOcr(oldestId, QStringLiteral("旧文本"), QString{}, QStringLiteral("failed"), QStringLiteral("旧失败"), false).has_value());

    coordinator.retryOcr(oldestId);

    ASSERT_TRUE(waitUntil(
            [&]
            {
                const auto row = coordinator.index().findById(oldestId);
                return row.has_value() && row->has_value() && (*row)->ocrStatus == QStringLiteral("done");
            }))
            << "超过 200 行的旧主键应当仍能按主键重新入队";
    const auto retried = coordinator.index().findById(oldestId);
    ASSERT_TRUE(retried.has_value());
    ASSERT_TRUE(retried->has_value());
    EXPECT_EQ((*retried)->ocrSnippet, QStringLiteral("很久以前"));
    EXPECT_EQ(fake->calls(), 1);
}

/**
 * @brief 后端不可用时队列停住、只报一次，恢复后重试待识别行即可完成
 */
TEST_F(MemoryCoordinator, UnavailableEnginePausesUntilRetry)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    auto          engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{makeResult(QStringLiteral("恢复后文本"))});
    auto         *fake   = engine.get();
    const QString reason = QStringLiteral("测试替身：识别后端不可用");
    fake->setUnavailable(reason);
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    QSignalSpy  availability(&coordinator, &Coordinator::ocrAvailabilityChanged);

    coordinator.start();

    ASSERT_GE(availability.count(), 1);
    EXPECT_FALSE(availability.at(0).at(0).toBool());
    EXPECT_EQ(availability.at(0).at(1).toString(), reason);

    const auto shot = makeShot(QStringLiteral("paused.png"), Qt::darkMagenta);
    coordinator.noteScreenshot(shot, QSize{32, 24}, CaptureSourceInfo{});
    const auto pending = findResult(coordinator.index(), shot);
    ASSERT_TRUE(pending.has_value());
    EXPECT_EQ(pending->ocrStatus, QStringLiteral("pending")) << "后端不可用时条目应保持待识别";
    EXPECT_EQ(fake->calls(), 0);

    fake->setAvailable();
    coordinator.retryPending();

    EXPECT_TRUE(waitUntil([&] { return hasStatus(coordinator.index(), shot, QStringLiteral("done")); }));
    EXPECT_EQ(fake->calls(), 1);
    ASSERT_GE(availability.count(), 2);
    EXPECT_TRUE(availability.last().at(0).toBool()) << "后端恢复后应发一次可用";
    EXPECT_TRUE(availability.last().at(1).toString().isEmpty());
}

/**
 * @brief 关闭后不再收新截图，重新开启时把库里的待识别行恢复出来
 */
TEST_F(MemoryCoordinator, DisablingStopsQueueAndReEnablingRecoversPending)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());
    ASSERT_TRUE(settings.setValue(kMemoryConsentAsked, true).has_value());

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{makeResult(QStringLiteral("关闭前")), makeResult(QStringLiteral("重新开启后"))});
    auto       *fake   = engine.get();
    Coordinator coordinator{settings, databasePath(), std::move(engine)};

    coordinator.start();
    const auto first = makeShot(QStringLiteral("before-disable.png"), Qt::red);
    coordinator.noteScreenshot(first, QSize{32, 24}, CaptureSourceInfo{});
    EXPECT_TRUE(waitUntil([&] { return hasStatus(coordinator.index(), first, QStringLiteral("done")); }));
    EXPECT_EQ(fake->calls(), 1);

    coordinator.setEnabled(false);
    EXPECT_FALSE(settings.boolValue(kMemoryEnabled, true));
    EXPECT_TRUE(coordinator.index().isOpen()) << "关闭开关不该关掉数据库";

    const auto second = makeShot(QStringLiteral("while-disabled.png"), Qt::blue);
    coordinator.noteScreenshot(second, QSize{32, 24}, CaptureSourceInfo{});
    EXPECT_FALSE(findResult(coordinator.index(), second).has_value()) << "关闭后不该建立新索引";
    EXPECT_EQ(fake->calls(), 1);

    MemoryUpsert entry;
    entry.filePath   = second;
    entry.source     = MemorySource::Capture;
    entry.capturedAt = QDateTime::currentDateTime();
    entry.size       = QSize{32, 24};
    ASSERT_TRUE(coordinator.index().upsertPending(entry).has_value());

    coordinator.setEnabled(true);
    EXPECT_TRUE(settings.boolValue(kMemoryEnabled, false));
    EXPECT_TRUE(settings.boolValue(kMemoryConsentAsked, false));
    EXPECT_TRUE(waitUntil([&] { return hasStatus(coordinator.index(), second, QStringLiteral("done")); }));
    EXPECT_EQ(fake->calls(), 2);
}

/**
 * @brief 清理缺失文件时删行、发一次提示，再清一次不再重复提示
 */
TEST_F(MemoryCoordinator, PruneMissingRemovesRowsAndNotifiesOnce)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{});
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    coordinator.start();

    QSignalSpy messages(&coordinator, &Coordinator::messageForUser);
    QSignalSpy changed(&coordinator, &Coordinator::changed);

    const auto   missing = root() / "already-gone.png";
    MemoryUpsert entry;
    entry.filePath   = missing;
    entry.source     = MemorySource::Capture;
    entry.capturedAt = QDateTime::currentDateTime();
    entry.size       = QSize{32, 24};
    ASSERT_TRUE(coordinator.index().upsertPending(entry).has_value());
    ASSERT_EQ(resultCount(coordinator.index()), 1);

    coordinator.pruneMissing();

    EXPECT_EQ(resultCount(coordinator.index()), 0);
    ASSERT_EQ(messages.count(), 1) << "删掉缺失行应提示一次";
    EXPECT_EQ(messages.at(0).at(1).value<ToastKind>(), ToastKind::Information);
    EXPECT_GT(changed.count(), 0);

    coordinator.pruneMissing();
    EXPECT_EQ(messages.count(), 1) << "没有删行时不该重复提示";
}

/**
 * @brief 超长识别文本按 UTF-8 字节截到上限，并在库里留下截断标记
 */
TEST_F(MemoryCoordinator, LongOcrTextIsTruncatedAndFlagged)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    const QString longText = QString(kMaxOcrTextBytes + 4'464, QLatin1Char('a'));
    auto          engine   = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{makeResult(longText)});
    Coordinator   coordinator{settings, databasePath(), std::move(engine)};

    coordinator.start();
    const auto shot = makeShot(QStringLiteral("long-text.png"), Qt::darkGray);
    coordinator.noteScreenshot(shot, QSize{32, 24}, CaptureSourceInfo{});
    EXPECT_TRUE(waitUntil([&] { return hasStatus(coordinator.index(), shot, QStringLiteral("done")); }));

    const auto summary = storedOcrSummary(databasePath(), shot);
    ASSERT_TRUE(summary.has_value()) << "读不回条目的 OCR 长度与截断标记";
    EXPECT_EQ(summary->first, kMaxOcrTextBytes);
    EXPECT_TRUE(summary->second) << "超过上限的文本应标记为已截断";
}

/**
 * @brief 一行都没认出来也算完成，文本为空而不是失败
 */
TEST_F(MemoryCoordinator, EmptyOcrResultIsDoneWithEmptyText)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    auto        engine = std::make_unique<FakeOcrEngine>(std::deque<Expected<OcrResult>>{OcrResult{}});
    Coordinator coordinator{settings, databasePath(), std::move(engine)};

    coordinator.start();
    const auto shot = makeShot(QStringLiteral("empty.png"), Qt::black);
    coordinator.noteScreenshot(shot, QSize{32, 24}, CaptureSourceInfo{});

    EXPECT_TRUE(waitUntil([&] { return hasStatus(coordinator.index(), shot, QStringLiteral("done")); }));
    const auto result = findResult(coordinator.index(), shot);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->ocrSnippet.isEmpty());
}
