// 钉住记忆索引的建表、增删改查、FTS5 中文检索、应用与时间筛选、容量淘汰、统计与 1 万条性能预算

#include "Core/Memory/MemoryIndex.h"
#include "Core/Memory/MemoryToken.h"
#include "Core/Memory/MemoryTypes.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QStringList>
#include <QVariant>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <vector>

namespace Core = GyanisScribe::Core;
using GyanisScribe::Base::ErrorCode;

namespace
{
    /// QSQLITE 是 Qt 插件，加载前进程里要先有一个应用对象
    class QtApplicationEnvironment : public ::testing::Environment
    {
    public:
        void SetUp() override
        {
            static int   argc   = 1;
            static char  name[] = "TestCoreMemory";
            static char *argv[] = {name, nullptr};
            m_application       = std::make_unique<QCoreApplication>(argc, argv);
        }

        void TearDown() override
        {
            m_application.reset();
        }

    private:
        std::unique_ptr<QCoreApplication> m_application;
    };

    [[maybe_unused]] const bool kQtApplicationRegistered = []
    {
        ::testing::AddGlobalTestEnvironment(new QtApplicationEnvironment);
        return true;
    }();

    Core::MemoryUpsert entryFor(const std::filesystem::path &path, const Core::MemorySource source, const QDateTime &capturedAt, const QSize &size, const QString &processName = {},
                                const QString &windowTitle = {})
    {
        Core::MemoryUpsert entry;
        entry.filePath    = path;
        entry.source      = source;
        entry.capturedAt  = capturedAt;
        entry.size        = size;
        entry.processName = processName;
        entry.windowTitle = windowTitle;
        return entry;
    }

    bool containsPath(const std::vector<Core::MemoryResult> &results, const std::filesystem::path &path)
    {
        return std::ranges::any_of(results, [&path](const Core::MemoryResult &result) { return result.filePath == path; });
    }

    QString unknownProcessFilter()
    {
        return QString::fromLatin1(Core::kUnknownProcessFilter.data(), static_cast<qsizetype>(Core::kUnknownProcessFilter.size()));
    }

    /// 测试直连同一份数据库读取 search 结果看不全的列，如 ocr_text 长度与 truncated 标记
    class RawDatabase
    {
    public:
        explicit RawDatabase(const std::filesystem::path &databasePath) : m_connectionName(QStringLiteral("TestMemoryIndexRaw-%1").arg(s_nextConnection.fetch_add(1)))
        {
            m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
            m_database.setDatabaseName(QString::fromStdWString(databasePath.wstring()));
            EXPECT_TRUE(m_database.open());
        }

        ~RawDatabase()
        {
            m_database.close();
            m_database = QSqlDatabase{};
            QSqlDatabase::removeDatabase(m_connectionName);
        }

        RawDatabase(const RawDatabase &)            = delete;
        RawDatabase &operator=(const RawDatabase &) = delete;

        QSqlDatabase &database()
        {
            return m_database;
        }

    private:
        static inline std::atomic<int> s_nextConnection{0};
        QString                        m_connectionName;
        QSqlDatabase                   m_database;
    };

} // namespace

class MemoryIndex : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_temp.isValid());
        m_index = std::make_unique<Core::MemoryIndex>(std::filesystem::path(m_temp.path().toStdString()) / "index.sqlite");
        ASSERT_TRUE(m_index->open().has_value());
    }

    std::filesystem::path tempFile(const QString &name) const
    {
        return std::filesystem::path(m_temp.path().toStdString()) / name.toStdString();
    }

    QTemporaryDir                      m_temp;
    std::unique_ptr<Core::MemoryIndex> m_index;
};

// 钉住首次打开建出 schema，upsert 后空查询能列出这一条并带回路径、尺寸与进程
TEST_F(MemoryIndex, CreatesSchemaAndStoresPendingRow)
{
    const auto path     = tempFile("first-shot.png");
    const auto captured = QDateTime::fromSecsSinceEpoch(1767225600 + 34200);

    const auto identifier = m_index->upsertPending(entryFor(path, Core::MemorySource::Capture, captured, QSize(640, 480), QStringLiteral("chrome"), QStringLiteral("示例窗口")));
    ASSERT_TRUE(identifier.has_value());

    const auto results = m_index->search(Core::MemoryQuery{});
    ASSERT_TRUE(results.has_value());
    ASSERT_EQ(results->size(), 1U);

    const Core::MemoryResult &result = results->front();
    EXPECT_EQ(result.id, *identifier);
    EXPECT_EQ(result.filePath, path);
    EXPECT_EQ(result.source, Core::MemorySource::Capture);
    EXPECT_EQ(result.capturedAt, captured);
    EXPECT_EQ(result.size, QSize(640, 480));
    EXPECT_EQ(result.processName, QStringLiteral("chrome"));
    EXPECT_EQ(result.windowTitle, QStringLiteral("示例窗口"));
    EXPECT_EQ(result.ocrStatus, QStringLiteral("pending"));
}

// 钉住中文查询：两字与三字片段都能命中预处理后的 OCR 文本，单字也能命中
TEST_F(MemoryIndex, SearchFindsChineseSubstrings)
{
    const auto firstPath  = tempFile("quote.png");
    const auto secondPath = tempFile("contract.png");
    const auto moment     = QDateTime::fromSecsSinceEpoch(1767225600 + 36000);

    const auto firstId  = m_index->upsertPending(entryFor(firstPath, Core::MemorySource::Capture, moment, QSize(100, 100), QStringLiteral("chrome")));
    const auto secondId = m_index->upsertPending(entryFor(secondPath, Core::MemorySource::Capture, moment.addSecs(60), QSize(100, 100), QStringLiteral("chrome")));
    ASSERT_TRUE(firstId.has_value());
    ASSERT_TRUE(secondId.has_value());
    ASSERT_TRUE(m_index->updateOcr(*firstId, QStringLiteral("本周报价表"), QString{}, QStringLiteral("done"), QString{}, false).has_value());
    ASSERT_TRUE(m_index->updateOcr(*secondId, QStringLiteral("季度合同"), QString{}, QStringLiteral("done"), QString{}, false).has_value());

    Core::MemoryQuery query;
    query.text           = QStringLiteral("报价");
    const auto quoteHits = m_index->search(query);
    ASSERT_TRUE(quoteHits.has_value());
    ASSERT_EQ(quoteHits->size(), 1U);
    EXPECT_EQ(quoteHits->front().id, *firstId);
    EXPECT_EQ(quoteHits->front().ocrSnippet, QStringLiteral("本周报价表"));

    query.text              = QStringLiteral("合同");
    const auto contractHits = m_index->search(query);
    ASSERT_TRUE(contractHits.has_value());
    ASSERT_EQ(contractHits->size(), 1U);
    EXPECT_EQ(contractHits->front().id, *secondId);

    query.text            = QStringLiteral("报");
    const auto singleHits = m_index->search(query);
    ASSERT_TRUE(singleHits.has_value());
    ASSERT_EQ(singleHits->size(), 1U);
    EXPECT_EQ(singleHits->front().id, *firstId);
}

// 钉住应用与时间筛选：进程名精确匹配、未知应用用保留值命中空进程行、from/to 为闭区间
TEST_F(MemoryIndex, SearchFiltersByAppAndTime)
{
    const auto chromePath   = tempFile("chrome.png");
    const auto unknownPath  = tempFile("unknown.png");
    const auto firstMoment  = QDateTime::fromSecsSinceEpoch(1767225600 + 36000);
    const auto secondMoment = QDateTime::fromSecsSinceEpoch(1767225600 + 86400 + 36000);

    ASSERT_TRUE(m_index->upsertPending(entryFor(chromePath, Core::MemorySource::Capture, firstMoment, QSize(10, 10), QStringLiteral("chrome"))).has_value());
    ASSERT_TRUE(m_index->upsertPending(entryFor(unknownPath, Core::MemorySource::Capture, secondMoment, QSize(10, 10))).has_value());

    Core::MemoryQuery byApp;
    byApp.processName  = QStringLiteral("chrome");
    const auto appHits = m_index->search(byApp);
    ASSERT_TRUE(appHits.has_value());
    ASSERT_EQ(appHits->size(), 1U);
    EXPECT_EQ(appHits->front().filePath, chromePath);

    Core::MemoryQuery byUnknown;
    byUnknown.processName  = unknownProcessFilter();
    const auto unknownHits = m_index->search(byUnknown);
    ASSERT_TRUE(unknownHits.has_value());
    ASSERT_EQ(unknownHits->size(), 1U);
    EXPECT_EQ(unknownHits->front().filePath, unknownPath);

    Core::MemoryQuery fromSecondDay;
    fromSecondDay.from  = secondMoment;
    const auto fromHits = m_index->search(fromSecondDay);
    ASSERT_TRUE(fromHits.has_value());
    ASSERT_EQ(fromHits->size(), 1U);
    EXPECT_EQ(fromHits->front().filePath, unknownPath);

    Core::MemoryQuery untilFirstDay;
    untilFirstDay.to  = firstMoment;
    const auto toHits = m_index->search(untilFirstDay);
    ASSERT_TRUE(toHits.has_value());
    ASSERT_EQ(toHits->size(), 1U);
    EXPECT_EQ(toHits->front().filePath, chromePath);
}

// 钉住同一个 path_key 的二次 upsert 走更新而不是再插一行，尺寸与标题以新值为准
TEST_F(MemoryIndex, UpsertSamePathUpdatesInsteadOfDuplicating)
{
    const auto path     = tempFile("same.png");
    const auto captured = QDateTime::fromSecsSinceEpoch(1767225600 + 28800);

    ASSERT_TRUE(m_index->upsertPending(entryFor(path, Core::MemorySource::Import, captured, QSize(100, 50), QStringLiteral("code"), QStringLiteral("旧标题"))).has_value());
    ASSERT_TRUE(m_index->upsertPending(entryFor(path, Core::MemorySource::Import, captured.addSecs(30), QSize(200, 100), QStringLiteral("code"), QStringLiteral("新标题")))
                        .has_value());

    const auto results = m_index->search(Core::MemoryQuery{});
    ASSERT_TRUE(results.has_value());
    ASSERT_EQ(results->size(), 1U);
    EXPECT_EQ(results->front().size, QSize(200, 100));
    EXPECT_EQ(results->front().windowTitle, QStringLiteral("新标题"));
}

// 钉住 Windows 的路径身份归一：大小写与分隔符不同的同一路径得到同一个 path_key
TEST_F(MemoryIndex, PathKeyTreatsWindowsCaseAndSeparatorsAsSame)
{
#ifdef _WIN32
    EXPECT_EQ(Core::MemoryIndex::pathKey(std::filesystem::path{"C:\\Temp\\A.png"}), Core::MemoryIndex::pathKey(std::filesystem::path{"c:/temp/a.png"}));
    EXPECT_EQ(Core::MemoryIndex::pathKey(std::filesystem::path{"C:\\Temp\\.\\A.png"}), Core::MemoryIndex::pathKey(std::filesystem::path{"c:/TEMP/a.PNG"}));
#else
    GTEST_SKIP() << "本用例钉的是 Windows 的路径大小写与分隔符归一";
#endif
}

// 钉住历史清空只删截图来源，导入行保留；清空索引后一条不剩
TEST_F(MemoryIndex, RemoveCaptureEntriesKeepsImports)
{
    const auto moment = QDateTime::fromSecsSinceEpoch(1767225600);
    ASSERT_TRUE(m_index->upsertPending(entryFor(tempFile("capture.png"), Core::MemorySource::Capture, moment, QSize(10, 10))).has_value());
    ASSERT_TRUE(m_index->upsertPending(entryFor(tempFile("import.png"), Core::MemorySource::Import, moment, QSize(10, 10))).has_value());

    ASSERT_TRUE(m_index->removeCaptureEntries().has_value());
    const auto afterCaptures = m_index->search(Core::MemoryQuery{});
    ASSERT_TRUE(afterCaptures.has_value());
    ASSERT_EQ(afterCaptures->size(), 1U);
    EXPECT_EQ(afterCaptures->front().source, Core::MemorySource::Import);

    ASSERT_TRUE(m_index->clear().has_value());
    const auto afterClear = m_index->search(Core::MemoryQuery{});
    ASSERT_TRUE(afterClear.has_value());
    EXPECT_TRUE(afterClear->empty());
}

// 钉住文件缺失清理：文件还在时一条不删，文件消失后返回删除数并清空命中
TEST_F(MemoryIndex, PruneMissingDropsVanishedFiles)
{
    const auto path     = tempFile("vanishing.png");
    const auto captured = QDateTime::fromSecsSinceEpoch(1767225600);
    {
        std::ofstream file{path};
        file << "placeholder";
    }
    ASSERT_TRUE(std::filesystem::exists(path));
    ASSERT_TRUE(m_index->upsertPending(entryFor(path, Core::MemorySource::Capture, captured, QSize(10, 10))).has_value());

    const auto nothingMissing = m_index->pruneMissing();
    ASSERT_TRUE(nothingMissing.has_value());
    EXPECT_EQ(*nothingMissing, 0);

    std::error_code removalError;
    ASSERT_TRUE(std::filesystem::remove(path, removalError));
    ASSERT_FALSE(removalError);

    const auto pruned = m_index->pruneMissing();
    ASSERT_TRUE(pruned.has_value());
    EXPECT_EQ(*pruned, 1);

    const auto results = m_index->search(Core::MemoryQuery{});
    ASSERT_TRUE(results.has_value());
    EXPECT_TRUE(results->empty());
}

// 钉住待识别队列：截图来源优先于导入，重置识别后重新入队且旧文本仍在
TEST_F(MemoryIndex, PendingItemsPrioritizeCapturesAndFollowReset)
{
    const auto moment    = QDateTime::fromSecsSinceEpoch(1767225600);
    const auto importId  = m_index->upsertPending(entryFor(tempFile("import.png"), Core::MemorySource::Import, moment, QSize(10, 10)));
    const auto captureId = m_index->upsertPending(entryFor(tempFile("capture.png"), Core::MemorySource::Capture, moment.addSecs(60), QSize(10, 10)));
    ASSERT_TRUE(importId.has_value());
    ASSERT_TRUE(captureId.has_value());

    const auto queued = m_index->pendingItems(10);
    ASSERT_TRUE(queued.has_value());
    ASSERT_EQ(queued->size(), 2U);
    EXPECT_EQ(queued->at(0).id, *captureId);
    EXPECT_EQ(queued->at(1).id, *importId);

    ASSERT_TRUE(m_index->updateOcr(*captureId, QStringLiteral("已完成"), QString{}, QStringLiteral("done"), QString{}, false).has_value());
    ASSERT_TRUE(m_index->resetOcr(*captureId).has_value());

    const auto requeued = m_index->pendingItems(10);
    ASSERT_TRUE(requeued.has_value());
    ASSERT_EQ(requeued->size(), 2U);
    EXPECT_EQ(requeued->at(0).id, *captureId);
    EXPECT_EQ(requeued->at(0).ocrSnippet, QStringLiteral("已完成"));

    ASSERT_TRUE(m_index->updateOcr(*captureId, QStringLiteral("已完成"), QString{}, QStringLiteral("done"), QString{}, false).has_value());
    const auto remaining = m_index->pendingItems(10);
    ASSERT_TRUE(remaining.has_value());
    ASSERT_EQ(remaining->size(), 1U);
    EXPECT_EQ(remaining->at(0).id, *importId);
}

// 钉住按路径与按应用的删除：重复删除幂等，进程名比较不区分大小写，空进程名的导入行不受影响
TEST_F(MemoryIndex, RemovesByPathAndProcessIdempotently)
{
    const auto moment      = QDateTime::fromSecsSinceEpoch(1767225600);
    const auto chromePath  = tempFile("chrome.png");
    const auto firefoxPath = tempFile("firefox.png");
    ASSERT_TRUE(m_index->upsertPending(entryFor(chromePath, Core::MemorySource::Capture, moment, QSize(10, 10), QStringLiteral("chrome"))).has_value());
    ASSERT_TRUE(m_index->upsertPending(entryFor(firefoxPath, Core::MemorySource::Capture, moment.addSecs(1), QSize(10, 10), QStringLiteral("firefox"))).has_value());
    ASSERT_TRUE(m_index->upsertPending(entryFor(tempFile("import.png"), Core::MemorySource::Import, moment.addSecs(2), QSize(10, 10))).has_value());

    const auto appsBefore = m_index->apps();
    ASSERT_TRUE(appsBefore.has_value());
    ASSERT_EQ(appsBefore->size(), 3U);

    ASSERT_TRUE(m_index->removeByPath(chromePath).has_value());
    ASSERT_TRUE(m_index->removeByPath(chromePath).has_value());
    const auto afterPathRemoval = m_index->search(Core::MemoryQuery{});
    ASSERT_TRUE(afterPathRemoval.has_value());
    EXPECT_EQ(afterPathRemoval->size(), 2U);

    ASSERT_TRUE(m_index->removeProcess(QString{}).has_value());
    ASSERT_TRUE(m_index->removeProcess(QStringLiteral("FIREFOX")).has_value());
    const auto afterProcessRemoval = m_index->search(Core::MemoryQuery{});
    ASSERT_TRUE(afterProcessRemoval.has_value());
    ASSERT_EQ(afterProcessRemoval->size(), 1U);
    EXPECT_EQ(afterProcessRemoval->front().source, Core::MemorySource::Import);
}

// 钉住容量淘汰顺序：先删除最新一行外最旧的导入，导入删完后才轮到最旧的截图
TEST_F(MemoryIndex, CapacityEvictsOldestImportFirst)
{
    const auto oldestImport  = tempFile("oldest-import.png");
    const auto newerImport   = tempFile("newer-import.png");
    const auto newestCapture = tempFile("newest-capture.png");
    const auto moment        = QDateTime::fromSecsSinceEpoch(1767225600);

    ASSERT_TRUE(m_index->upsertPending(entryFor(oldestImport, Core::MemorySource::Import, moment, QSize(10, 10))).has_value());
    ASSERT_TRUE(m_index->upsertPending(entryFor(newerImport, Core::MemorySource::Import, moment.addSecs(60), QSize(10, 10))).has_value());
    ASSERT_TRUE(m_index->upsertPending(entryFor(newestCapture, Core::MemorySource::Capture, moment.addSecs(120), QSize(10, 10))).has_value());

    const auto firstEviction = m_index->enforceCapacity(2);
    ASSERT_TRUE(firstEviction.has_value());
    EXPECT_EQ(*firstEviction, 1);

    const auto afterFirst = m_index->search(Core::MemoryQuery{});
    ASSERT_TRUE(afterFirst.has_value());
    ASSERT_EQ(afterFirst->size(), 2U);
    EXPECT_FALSE(containsPath(*afterFirst, oldestImport));
    EXPECT_TRUE(containsPath(*afterFirst, newerImport));
    EXPECT_TRUE(containsPath(*afterFirst, newestCapture));

    const auto secondEviction = m_index->enforceCapacity(1);
    ASSERT_TRUE(secondEviction.has_value());
    EXPECT_EQ(*secondEviction, 1);

    const auto afterSecond = m_index->search(Core::MemoryQuery{});
    ASSERT_TRUE(afterSecond.has_value());
    ASSERT_EQ(afterSecond->size(), 1U);
    EXPECT_EQ(afterSecond->front().filePath, newestCapture);
}

// 钉住 OCR 文本按 UTF-8 字节截到 64 KiB 并置 truncated，ASCII 与多字节字符都不越界
TEST_F(MemoryIndex, OcrTextIsTruncatedAndFlagged)
{
    const auto path = tempFile("long-ocr.png");
    const auto id   = m_index->upsertPending(entryFor(path, Core::MemorySource::Capture, QDateTime::fromSecsSinceEpoch(1767225600), QSize(10, 10)));
    ASSERT_TRUE(id.has_value());

    const QString asciiText(Core::kMaxOcrTextBytes + 512, QLatin1Char('a'));
    ASSERT_TRUE(m_index->updateOcr(*id, asciiText, QString{}, QStringLiteral("done"), QString{}, false).has_value());

    {
        RawDatabase raw{tempFile("index.sqlite")};
        QSqlQuery   query{raw.database()};
        ASSERT_TRUE(query.prepare(QStringLiteral("SELECT length(ocr_text), length(CAST(ocr_text AS BLOB)), truncated FROM items WHERE id = ?")));
        query.addBindValue(*id);
        ASSERT_TRUE(query.exec());
        ASSERT_TRUE(query.next());
        EXPECT_EQ(query.value(0).toLongLong(), static_cast<qint64>(Core::kMaxOcrTextBytes));
        EXPECT_EQ(query.value(1).toLongLong(), static_cast<qint64>(Core::kMaxOcrTextBytes));
        EXPECT_EQ(query.value(2).toInt(), 1);
    }

    // 多字节字符不能切出半个 UTF-8 序列：按 BLOB 长度复核不超过上限
    const QString wideText = QStringLiteral("记").repeated(Core::kMaxOcrTextBytes);
    ASSERT_TRUE(m_index->updateOcr(*id, wideText, QString{}, QStringLiteral("done"), QString{}, false).has_value());
    {
        RawDatabase raw{tempFile("index.sqlite")};
        QSqlQuery   query{raw.database()};
        ASSERT_TRUE(query.prepare(QStringLiteral("SELECT length(CAST(ocr_text AS BLOB)), truncated FROM items WHERE id = ?")));
        query.addBindValue(*id);
        ASSERT_TRUE(query.exec());
        ASSERT_TRUE(query.next());
        const qint64 storedBytes = query.value(0).toLongLong();
        EXPECT_GT(storedBytes, 0);
        EXPECT_LE(storedBytes, static_cast<qint64>(Core::kMaxOcrTextBytes));
        EXPECT_EQ(storedBytes % 3, 0);
        EXPECT_EQ(query.value(1).toInt(), 1);
    }
}

// 钉住页脚统计：总数、待识别与失败计数正确，数据库字节数含 WAL 且大于零
TEST_F(MemoryIndex, StatsCountsPendingFailedAndBytes)
{
    const auto moment   = QDateTime::fromSecsSinceEpoch(1767225600);
    const auto doneId   = m_index->upsertPending(entryFor(tempFile("done.png"), Core::MemorySource::Capture, moment, QSize(10, 10)));
    const auto failedId = m_index->upsertPending(entryFor(tempFile("failed.png"), Core::MemorySource::Capture, moment.addSecs(1), QSize(10, 10)));
    ASSERT_TRUE(m_index->upsertPending(entryFor(tempFile("pending.png"), Core::MemorySource::Capture, moment.addSecs(2), QSize(10, 10))).has_value());
    ASSERT_TRUE(doneId.has_value());
    ASSERT_TRUE(failedId.has_value());

    ASSERT_TRUE(m_index->updateOcr(*doneId, QStringLiteral("完成"), QString{}, QStringLiteral("done"), QString{}, false).has_value());
    ASSERT_TRUE(m_index->updateOcr(*failedId, QString{}, QString{}, QStringLiteral("failed"), QStringLiteral("读图失败"), false).has_value());

    const auto counted = m_index->stats(3);
    ASSERT_TRUE(counted.has_value());
    EXPECT_EQ(counted->totalItems, 3);
    EXPECT_EQ(counted->pendingOcr, 1);
    EXPECT_EQ(counted->failedOcr, 1);
    EXPECT_GT(counted->databaseBytes, 0);
    EXPECT_EQ(counted->excludedProcessCount, 3);
}

// 钉住版本闸门：user_version 高于本版本的数据库拒绝打开，错误类型为平台不支持
TEST_F(MemoryIndex, RejectsNewerSchemaVersion)
{
    const auto databasePath = tempFile("newer.sqlite");
    {
        RawDatabase raw{databasePath};
        QSqlQuery   query{raw.database()};
        ASSERT_TRUE(query.exec(QStringLiteral("PRAGMA user_version = 2")));
    }

    Core::MemoryIndex newerIndex{databasePath};
    const auto        opened = newerIndex.open();
    ASSERT_FALSE(opened.has_value());
    EXPECT_EQ(opened.error().code, ErrorCode::PlatformUnsupported);
    EXPECT_FALSE(opened.error().message.empty());
    EXPECT_FALSE(newerIndex.isOpen());
}

// 钉住关闭后可原地重开：close 断开连接，open 复用同一对象恢复读写
TEST_F(MemoryIndex, CloseAllowsReopenInPlace)
{
    const auto id = m_index->upsertPending(entryFor(tempFile("reopen.png"), Core::MemorySource::Capture, QDateTime::fromSecsSinceEpoch(1767225600), QSize(10, 10)));
    ASSERT_TRUE(id.has_value());

    m_index->close();
    EXPECT_FALSE(m_index->isOpen());
    EXPECT_FALSE(m_index->search(Core::MemoryQuery{}).has_value()) << "关闭之后查询应报告数据库未打开";

    ASSERT_TRUE(m_index->open().has_value());
    EXPECT_TRUE(m_index->isOpen());
    const auto results = m_index->search(Core::MemoryQuery{});
    ASSERT_TRUE(results.has_value());
    ASSERT_EQ(results->size(), 1U);
    EXPECT_EQ(results->front().id, *id);
}

// 钉住失败写回只改状态与原因：旧文本、可检索文本与指纹都保留
TEST_F(MemoryIndex, MarkOcrFailedKeepsPreviousText)
{
    const auto path = tempFile("failed-retry.png");
    const auto id   = m_index->upsertPending(entryFor(path, Core::MemorySource::Capture, QDateTime::fromSecsSinceEpoch(1767225600), QSize(20, 10)));
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(m_index->updateOcr(*id, QStringLiteral("本周报价表"), QStringLiteral("0123456789abcdef"), QStringLiteral("done"), QString{}, false).has_value());

    ASSERT_TRUE(m_index->markOcrFailed(*id, QStringLiteral("再次识别失败")).has_value());

    const auto results = m_index->search(Core::MemoryQuery{});
    ASSERT_TRUE(results.has_value());
    ASSERT_EQ(results->size(), 1U);
    EXPECT_EQ(results->front().ocrStatus, QStringLiteral("failed"));
    EXPECT_EQ(results->front().ocrSnippet, QStringLiteral("本周报价表")) << "失败写回不该清掉旧文本";
    EXPECT_EQ(results->front().dhash, QStringLiteral("0123456789abcdef")) << "失败写回不该清掉指纹";

    Core::MemoryQuery query;
    query.text      = QStringLiteral("报价");
    const auto hits = m_index->search(query);
    ASSERT_TRUE(hits.has_value());
    EXPECT_EQ(hits->size(), 1U) << "旧文本应当仍在全文索引里";

    RawDatabase raw{tempFile("index.sqlite")};
    QSqlQuery   errorQuery{raw.database()};
    ASSERT_TRUE(errorQuery.prepare(QStringLiteral("SELECT ocr_error FROM items WHERE id = ?")));
    errorQuery.addBindValue(*id);
    ASSERT_TRUE(errorQuery.exec());
    ASSERT_TRUE(errorQuery.next());
    EXPECT_EQ(errorQuery.value(0).toString(), QStringLiteral("再次识别失败"));
}

// 钉住按主键回查：不受单页 200 条搜索上限影响，缺失主键返回空而不是错误
TEST_F(MemoryIndex, FindByIdReturnsRowOrEmpty)
{
    const auto moment = QDateTime::fromSecsSinceEpoch(1767225600);
    qint64     oldest = 0;
    for (int index = 0; index < 250; ++index)
    {
        const auto id = m_index->upsertPending(entryFor(tempFile(QStringLiteral("page-%1.png").arg(index)), Core::MemorySource::Capture, moment.addSecs(index), QSize(10, 10)));
        ASSERT_TRUE(id.has_value());
        if (index == 0)
        {
            oldest = *id;
        } else
        {
            ASSERT_TRUE(m_index->updateOcr(*id, QStringLiteral("第 %1 条").arg(index), QString{}, QStringLiteral("done"), QString{}, false).has_value());
        }
    }
    ASSERT_TRUE(m_index->updateOcr(oldest, QStringLiteral("最旧一条"), QString{}, QStringLiteral("failed"), QString{}, false).has_value());

    const auto found = m_index->findById(oldest);
    ASSERT_TRUE(found.has_value());
    ASSERT_TRUE(found->has_value());
    EXPECT_EQ((*found)->id, oldest);
    EXPECT_EQ((*found)->ocrSnippet, QStringLiteral("最旧一条"));
    EXPECT_EQ((*found)->ocrStatus, QStringLiteral("failed"));

    const auto missing = m_index->findById(999999);
    ASSERT_TRUE(missing.has_value());
    EXPECT_FALSE(missing->has_value());
}

// 钉住超过 256 字符的查询在绑定前被截断，FTS5 收到合法短语而不是超长语法错误
TEST_F(MemoryIndex, LongQueryIsCappedBeforeFts)
{
    const auto path = tempFile("long-query.png");
    const auto id   = m_index->upsertPending(entryFor(path, Core::MemorySource::Capture, QDateTime::fromSecsSinceEpoch(1767225600), QSize(10, 10)));
    ASSERT_TRUE(id.has_value());
    // 正文只有 256 个「记」；查询多出的部分若没被截掉，整段短语就对不上，这条命中即证明截断生效
    ASSERT_TRUE(m_index->updateOcr(*id, QStringLiteral("记").repeated(256) + QStringLiteral("忘").repeated(200), QString{}, QStringLiteral("done"), QString{}, false).has_value());

    Core::MemoryQuery query;
    query.text = QStringLiteral("记").repeated(400);

    const auto results = m_index->search(query);
    ASSERT_TRUE(results.has_value()) << "超过 256 字符的查询必须正常返回，而不是 FTS5 语法错误";
    EXPECT_EQ(results->size(), 1U) << "查询没有截到 256 字符，FTS5 用整段 400 字短语匹配不上正文";
}

// 钉住 1 万条规模下的查询预算：Release 平均不超过 200ms，Debug 放宽到 2s
TEST_F(MemoryIndex, SearchTenThousandEntriesUnderBudget)
{
    constexpr int kEntryCount = 10000;
    constexpr int kQueryCount = 20;
    const qint64  baseMoment  = 1767225600000;

    {
        RawDatabase raw{tempFile("index.sqlite")};
        ASSERT_TRUE(raw.database().transaction());

        QSqlQuery insert{raw.database()};
        ASSERT_TRUE(insert.prepare(
                QStringLiteral("INSERT INTO items(path, path_key, source, captured_at, added_at, width, height, process_name, window_title, ocr_text, ocr_status, search_text) "
                               "VALUES(:path, :path_key, 'capture', :captured_at, :added_at, 640, 480, 'chrome', '窗口', :ocr_text, 'done', :search_text)")));

        for (int index = 0; index < kEntryCount; ++index)
        {
            const QString path    = QStringLiteral("C:\\synthetic\\shot-%1.png").arg(index);
            const QString pathKey = QStringLiteral("c:/synthetic/shot-%1.png").arg(index);
            const QString ocrText = QStringLiteral("第%1号截图 报价 合同 项目 报告").arg(index);
            insert.bindValue(QStringLiteral(":path"), path);
            insert.bindValue(QStringLiteral(":path_key"), pathKey);
            insert.bindValue(QStringLiteral(":captured_at"), baseMoment + index);
            insert.bindValue(QStringLiteral(":added_at"), baseMoment + index);
            insert.bindValue(QStringLiteral(":ocr_text"), ocrText);
            insert.bindValue(QStringLiteral(":search_text"), Core::prepareFtsText(ocrText));
            ASSERT_TRUE(insert.exec()) << insert.lastError().text().toStdString();
        }

        ASSERT_TRUE(raw.database().commit());
    }

    const QStringList terms{QStringLiteral("报价"), QStringLiteral("合同"), QStringLiteral("项目"), QStringLiteral("报告"), QStringLiteral("截图")};

    std::chrono::nanoseconds total{0};
    for (int index = 0; index < kQueryCount; ++index)
    {
        Core::MemoryQuery query;
        query.text = terms.at(index % terms.size());

        const auto started = std::chrono::steady_clock::now();
        const auto results = m_index->search(query);
        total += std::chrono::steady_clock::now() - started;

        ASSERT_TRUE(results.has_value()) << results.error().message;
        ASSERT_EQ(results->size(), static_cast<std::size_t>(Core::kMemorySearchLimit));
    }

    const auto average = std::chrono::duration_cast<std::chrono::milliseconds>(total / kQueryCount).count();
    std::printf("1 万条记忆索引 %d 次查询平均 %lld ms\n", kQueryCount, static_cast<long long>(average));

#ifdef NDEBUG
    constexpr qint64 kAverageBudgetMilliseconds = 200;
#else
    constexpr qint64 kAverageBudgetMilliseconds = 2000;
#endif
    EXPECT_LE(average, kAverageBudgetMilliseconds) << "查询平均耗时超出预算";
}
