// 记忆库导入的直测：文件夹扫描、已在库跳过、大小写重复、超限与坏图、取消、排除应用、分块事务与容量淘汰。
// 假引擎统一返回「本周报价表」，让五百张导入后能只靠 OCR 文本检索；数据库与设置都落在临时目录。

#include "App/MemoryCoordinator.h"
#include "App/MemoryImportJob.h"

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
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
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
using GyanisScribe::Core::kMemoryConsentAsked;
using GyanisScribe::Core::kMemoryEnabled;
using GyanisScribe::Core::kMemoryExcludeProcesses;
using GyanisScribe::Core::MemoryImportProgress;
using GyanisScribe::Core::MemoryImportReport;
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
    /// 等一次导入或一轮识别落地的最长时限：到点判红，而不是让用例挂住
    constexpr int kWaitPatienceMs = 10'000;

    /// 五百张导入与后台识别的时限：只有明显失控时才判红
    constexpr int kImportPatienceMs = 60'000;

    /// 五百张导入与搜索的预算，写进报告时要给出实测
    constexpr qint64 kFiveHundredBudgetMs = 60'000;

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

    /**
     * @brief 每次都返回同一段文字的识别后端
     * @details 导入用例只关心「文本写进索引后能否搜到」，不关心识别调用顺序；调用次数用原子量记，供排除用例核对没有新识别。
     */
    class ConstantOcrEngine final : public IOcrEngine
    {
    public:
        explicit ConstantOcrEngine(QString text) : m_text(std::move(text))
        {
        }

        [[nodiscard]] QString name() const override
        {
            return QStringLiteral("测试替身");
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
            return {QString::fromUtf8(kDefaultOcrLanguage)};
        }

        [[nodiscard]] Expected<OcrResult> recognize(const QImage &, const QString &) override
        {
            m_calls.fetch_add(1);
            return makeResult(m_text);
        }

        /// 已进入识别体的次数
        [[nodiscard]] int calls() const
        {
            return m_calls.load();
        }

    private:
        QString          m_text;     ///< 每次返回的文本
        std::atomic<int> m_calls{0}; ///< 进入识别体的次数
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

    class MemoryImport : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            if (QCoreApplication::instance() == nullptr)
            {
                static int   argumentCount  = 1;
                static char  argumentZero[] = "TestAppMemoryCoordinator";
                static char *arguments[]    = {argumentZero, nullptr};
                application                 = std::make_unique<QCoreApplication>(argumentCount, arguments);
            }

            qRegisterMetaType<MemoryStats>("GyanisScribe::Core::MemoryStats");
            qRegisterMetaType<MemoryImportProgress>("GyanisScribe::Core::MemoryImportProgress");
            qRegisterMetaType<MemoryImportReport>("GyanisScribe::Core::MemoryImportReport");
            qRegisterMetaType<ToastKind>("GyanisScribe::Gui::ToastKind");

            const auto logDirectory = std::filesystem::temp_directory_path() / "GyanisScribe-MemoryImport-logs";
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

        [[nodiscard]] std::filesystem::path importFolder() const
        {
            std::error_code error;
            const auto      folder = root() / "imports";
            std::filesystem::create_directories(folder, error);
            EXPECT_FALSE(error) << "造不出导入目录";
            return folder;
        }

        [[nodiscard]] std::filesystem::path makePng(const QString &relative, const QSize &size = QSize{8, 8}, const QColor &color = Qt::red) const
        {
            const auto      path = root() / std::filesystem::path{relative.toStdWString()};
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            EXPECT_FALSE(error) << "造不出截图目录";

            QImage image{size, QImage::Format_ARGB32};
            image.fill(color);
            EXPECT_TRUE(image.save(displayPath(path), "PNG"));
            return path;
        }

        /// 造一个只占文件大小、不读内容的超限文件
        void makeOversizeFile(const std::filesystem::path &path) const
        {
            QFile file{displayPath(path)};
            ASSERT_TRUE(file.open(QIODevice::WriteOnly));
            ASSERT_TRUE(file.seek(100LL * 1024 * 1024));
            ASSERT_EQ(file.write("x", 1), 1);
            file.close();
        }

        QTemporaryDir m_temp; ///< 这一格用例自己的临时目录
    };
} // namespace

/**
 * @brief 一个文件夹里的截图逐张入库，第二次导入只增加跳过数
 */
TEST_F(MemoryImport, ImportsFolderAndSkipsAlreadyIndexedPaths)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    auto        engine = std::make_unique<ConstantOcrEngine>(QStringLiteral("本周报价表"));
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    coordinator.start();

    const auto folder = importFolder();
    static_cast<void>(makePng(QStringLiteral("imports/first.png"), QSize{8, 8}, Qt::red));
    static_cast<void>(makePng(QStringLiteral("imports/second.png"), QSize{8, 8}, Qt::green));
    static_cast<void>(makePng(QStringLiteral("imports/third.png"), QSize{8, 8}, Qt::blue));

    QSignalSpy finished(&coordinator, &Coordinator::importFinished);
    coordinator.importFolder(folder);
    ASSERT_TRUE(waitUntil([&] { return finished.count() > 0; }, kImportPatienceMs)) << "第一次导入没有结束";

    const auto firstReport = finished.last().at(0).value<MemoryImportReport>();
    EXPECT_EQ(firstReport.scanned, 3);
    EXPECT_EQ(firstReport.inserted, 3);
    EXPECT_EQ(firstReport.skipped, 0);
    EXPECT_EQ(firstReport.failed, 0);
    EXPECT_FALSE(firstReport.cancelled);

    const auto firstStats = coordinator.index().stats(0);
    ASSERT_TRUE(firstStats.has_value());
    EXPECT_EQ(firstStats->totalItems, 3);

    finished.clear();
    coordinator.importFolder(folder);
    ASSERT_TRUE(waitUntil([&] { return finished.count() > 0; }, kImportPatienceMs)) << "第二次导入没有结束";

    const auto secondReport = finished.last().at(0).value<MemoryImportReport>();
    EXPECT_EQ(secondReport.scanned, 3);
    EXPECT_EQ(secondReport.inserted, 0) << "已在库的路径不该重复入库";
    EXPECT_EQ(secondReport.skipped, 3);
    EXPECT_EQ(secondReport.failed, 0);
    EXPECT_EQ(resultCount(coordinator.index()), 3);
}

/**
 * @brief 导入五百张后等识别收口，按 OCR 文本能翻页找回全部条目
 * @details 单次搜索上限是 200 条，这里用 captured_at 上界翻三页，合计命中 500 条；
 *          同时记录从开始导入到全部识别完成并完成检索的耗时。
 */
TEST_F(MemoryImport, ImportsFiveHundredAndFindsByOcrText)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    auto        engine = std::make_unique<ConstantOcrEngine>(QStringLiteral("本周报价表"));
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    coordinator.start();

    const auto      folder   = importFolder();
    const QDateTime baseTime = QDateTime::currentDateTime();
    for (int index = 0; index < 500; ++index)
    {
        const QString name = QStringLiteral("imports/shot-%1.png").arg(index, 3, 10, QLatin1Char('0'));
        const auto    path = makePng(name, QSize{8, 8}, Qt::white);

        // 每张差一小时：单次搜索最多 200 条，翻页靠 captured_at 上界把三页分开
        const auto      fileTime = std::filesystem::file_time_type::clock::now() + std::chrono::hours(index);
        std::error_code error;
        std::filesystem::last_write_time(path, fileTime, error);
        ASSERT_FALSE(error) << "改不了文件修改时间";
    }

    // 计时从导入开始，测试自己造 500 个文件的时间不占预算
    QElapsedTimer timer;
    timer.start();

    QSignalSpy finished(&coordinator, &Coordinator::importFinished);
    coordinator.importFolder(folder);
    ASSERT_TRUE(waitUntil([&] { return finished.count() > 0; }, kImportPatienceMs)) << "五百张导入没有结束";

    const auto report = finished.last().at(0).value<MemoryImportReport>();
    EXPECT_EQ(report.scanned, 500);
    EXPECT_EQ(report.inserted, 500);
    EXPECT_EQ(report.skipped, 0);
    EXPECT_EQ(report.failed, 0);

    ASSERT_TRUE(waitUntil(
            [&]
            {
                const auto stats = coordinator.index().stats(0);
                return stats.has_value() && stats->pendingOcr == 0;
            },
            kImportPatienceMs))
            << "后台识别没有收口";

    const auto finalStats = coordinator.index().stats(0);
    ASSERT_TRUE(finalStats.has_value());
    EXPECT_EQ(finalStats->totalItems, 500);
    EXPECT_EQ(finalStats->failedOcr, 0);

    int         totalHits = 0;
    MemoryQuery query;
    query.text   = QStringLiteral("报价");
    auto pageOne = coordinator.index().search(query);
    ASSERT_TRUE(pageOne.has_value());
    EXPECT_EQ(pageOne->size(), 200U) << "单页应达到 200 条上限";
    for (const MemoryResult &result: *pageOne)
    {
        EXPECT_EQ(result.ocrSnippet, QStringLiteral("本周报价表"));
        totalHits += 1;
    }

    query.to     = baseTime.addSecs((299LL * 60 * 60) + (30 * 60));
    auto pageTwo = coordinator.index().search(query);
    ASSERT_TRUE(pageTwo.has_value());
    for (const MemoryResult &result: *pageTwo)
    {
        EXPECT_EQ(result.ocrSnippet, QStringLiteral("本周报价表"));
        totalHits += 1;
    }

    query.to       = baseTime.addSecs((99LL * 60 * 60) + (30 * 60));
    auto pageThree = coordinator.index().search(query);
    ASSERT_TRUE(pageThree.has_value());
    for (const MemoryResult &result: *pageThree)
    {
        EXPECT_EQ(result.ocrSnippet, QStringLiteral("本周报价表"));
        totalHits += 1;
    }

    EXPECT_EQ(totalHits, 500) << "按 OCR 文本应能翻页找回全部五百张";
    EXPECT_EQ(pageTwo->size() + pageThree->size() + pageOne->size(), 500U);

    const qint64 elapsed = timer.elapsed();
    RecordProperty("importAndSearchMillis", static_cast<int>(elapsed));
    std::cout << "[  TIMING  ] 五百张导入、识别与翻页检索合计 " << elapsed << " ms\n";
    EXPECT_LT(elapsed, kFiveHundredBudgetMs) << "五百张导入与检索超出预算";
}

/**
 * @brief 同一文件换大小写拼法再导入，只增加跳过数，总数不变
 */
TEST_F(MemoryImport, SkipsCaseOnlyDuplicatePath)
{
#ifdef _WIN32
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    auto        engine = std::make_unique<ConstantOcrEngine>(QStringLiteral("本周报价表"));
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    coordinator.start();

    const auto      folder = root() / "CaseImports";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    ASSERT_FALSE(error);
    static_cast<void>(makePng(QStringLiteral("CaseImports/Picture.PNG"), QSize{8, 8}, Qt::magenta));

    QSignalSpy finished(&coordinator, &Coordinator::importFinished);
    coordinator.importFolder(folder);
    ASSERT_TRUE(waitUntil([&] { return finished.count() > 0; }, kImportPatienceMs));
    const auto firstReport = finished.last().at(0).value<MemoryImportReport>();
    ASSERT_EQ(firstReport.inserted, 1);

    const auto alternate = root() / "cASEiMPORTS";
    finished.clear();
    coordinator.importFolder(alternate);
    ASSERT_TRUE(waitUntil([&] { return finished.count() > 0; }, kImportPatienceMs));

    const auto secondReport = finished.last().at(0).value<MemoryImportReport>();
    EXPECT_EQ(secondReport.inserted, 0) << "仅大小写不同的同一路径不该再次入库";
    EXPECT_EQ(secondReport.skipped, 1);
    EXPECT_EQ(resultCount(coordinator.index()), 1);
#else
    GTEST_SKIP() << "本用例钉的是 Windows 路径大小写折叠";
#endif
}

/**
 * @brief 超过单文件上限与读不出尺寸的文件都计入跳过，坏文件不进库
 */
TEST_F(MemoryImport, SkipsOversizeAndUnreadableFiles)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    auto        engine = std::make_unique<ConstantOcrEngine>(QStringLiteral("本周报价表"));
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    coordinator.start();

    const auto folder = importFolder();
    static_cast<void>(makePng(QStringLiteral("imports/ok.png"), QSize{8, 8}, Qt::green));
    makeOversizeFile(folder / "huge.png");

    QFile badFile{displayPath(folder / "bad.png")};
    ASSERT_TRUE(badFile.open(QIODevice::WriteOnly));
    ASSERT_EQ(badFile.write("not an image"), 12);
    badFile.close();

    QSignalSpy finished(&coordinator, &Coordinator::importFinished);
    coordinator.importFolder(folder);
    ASSERT_TRUE(waitUntil([&] { return finished.count() > 0; }, kImportPatienceMs));

    const auto report = finished.last().at(0).value<MemoryImportReport>();
    EXPECT_EQ(report.scanned, 3);
    EXPECT_EQ(report.inserted, 1);
    EXPECT_EQ(report.skipped, 2) << "超限与读不出尺寸的文件都应跳过";
    EXPECT_EQ(report.failed, 0);
    EXPECT_EQ(resultCount(coordinator.index()), 1);
}

/**
 * @brief 导入后立刻取消：不再调度新文件，报告标记取消，已入库条数少于总数
 */
TEST_F(MemoryImport, CancelStopsSchedulingNewItems)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());

    auto        engine = std::make_unique<ConstantOcrEngine>(QStringLiteral("本周报价表"));
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    coordinator.start();

    const auto folder = importFolder();
    for (int index = 0; index < 5; ++index)
    {
        static_cast<void>(makePng(QStringLiteral("imports/cancel-%1.png").arg(index), QSize{8, 8}, Qt::cyan));
    }

    QSignalSpy finished(&coordinator, &Coordinator::importFinished);
    coordinator.importFolder(folder);
    coordinator.cancelImport();

    ASSERT_TRUE(waitUntil([&] { return finished.count() > 0; }, kWaitPatienceMs)) << "取消后导入没有收尾";
    const auto report = finished.last().at(0).value<MemoryImportReport>();
    EXPECT_TRUE(report.cancelled);
    EXPECT_LT(report.inserted, 5) << "取消后不该把整个文件夹都入完";

    const auto stats = coordinator.index().stats(0);
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->totalItems, report.inserted);
}

/**
 * @brief 未开启时点导入只发授权请求，不扫描也不入库
 */
TEST_F(MemoryImport, ImportFolderBeforeConsentOnlyAsks)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryConsentAsked, false).has_value());

    auto        engine = std::make_unique<ConstantOcrEngine>(QStringLiteral("本周报价表"));
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    coordinator.start();

    const auto folder = importFolder();
    static_cast<void>(makePng(QStringLiteral("imports/consent.png"), QSize{8, 8}, Qt::darkGreen));

    QSignalSpy consent(&coordinator, &Coordinator::consentRequested);
    QSignalSpy finished(&coordinator, &Coordinator::importFinished);
    coordinator.importFolder(folder);

    EXPECT_EQ(consent.count(), 1) << "未开启时导入应先请求授权";
    EXPECT_EQ(finished.count(), 0);
    EXPECT_EQ(resultCount(coordinator.index()), 0);

    coordinator.resolveConsent(true);
    ASSERT_TRUE(waitUntil([&] { return coordinator.isEnabled(); }));

    coordinator.importFolder(folder);
    ASSERT_TRUE(waitUntil([&] { return finished.count() > 0; }, kImportPatienceMs));
    const auto report = finished.last().at(0).value<MemoryImportReport>();
    EXPECT_EQ(report.inserted, 1) << "授权后应能继续导入";
    EXPECT_EQ(resultCount(coordinator.index()), 1);
}

/**
 * @brief 排除一个应用会删掉它已有的行、挡住之后的新截图，导入行不受影响
 */
TEST_F(MemoryImport, ExcludesProcessAndRemovesExistingRows)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());
    ASSERT_TRUE(settings.setValue(kMemoryConsentAsked, true).has_value());

    auto        engine = std::make_unique<ConstantOcrEngine>(QStringLiteral("本周报价表"));
    auto       *fake   = engine.get();
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    coordinator.start();

    const auto shot = makePng(QStringLiteral("shots/chrome.png"), QSize{8, 8}, Qt::red);
    coordinator.noteScreenshot(shot, QSize{8, 8}, CaptureSourceInfo{QStringLiteral("Chrome.EXE"), QStringLiteral("网页")});
    ASSERT_TRUE(waitUntil(
            [&]
            {
                const auto result = findResult(coordinator.index(), shot);
                return result.has_value() && result->ocrStatus == QStringLiteral("done");
            }));

    MemoryUpsert importEntry;
    importEntry.filePath   = root() / "imports" / "old-import.png";
    importEntry.source     = MemorySource::Import;
    importEntry.capturedAt = QDateTime::currentDateTime();
    importEntry.size       = QSize{8, 8};
    ASSERT_TRUE(coordinator.index().upsertPending(importEntry).has_value());

    QSignalSpy stats(&coordinator, &Coordinator::statsChanged);
    coordinator.setExcludedProcesses({QStringLiteral("Chrome.EXE"), QStringLiteral("chrome")});

    EXPECT_EQ(coordinator.excludedProcesses(), QStringList{QStringLiteral("chrome")}) << "排除列表应归一化并去重";
    EXPECT_EQ(settings.stringListValue(kMemoryExcludeProcesses, QStringList{}), QStringList{QStringLiteral("chrome")});
    EXPECT_FALSE(findResult(coordinator.index(), shot).has_value()) << "新排除的应用已有行应删除";
    EXPECT_TRUE(findResult(coordinator.index(), importEntry.filePath).has_value()) << "空进程的导入行不该被排除操作删除";

    ASSERT_GE(stats.count(), 1);
    EXPECT_EQ(stats.last().at(0).value<MemoryStats>().excludedProcessCount, 1);

    const int  callsBefore = fake->calls();
    const auto again       = makePng(QStringLiteral("shots/chrome-again.png"), QSize{8, 8}, Qt::blue);
    coordinator.noteScreenshot(again, QSize{8, 8}, CaptureSourceInfo{QStringLiteral("chrome.exe"), QStringLiteral("网页")});

    EXPECT_FALSE(findResult(coordinator.index(), again).has_value()) << "已排除的应用不该建立新索引";
    EXPECT_EQ(fake->calls(), callsBefore) << "已排除的截图不该进入识别";
    EXPECT_EQ(resultCount(coordinator.index()), 1);
}

/**
 * @brief 超出容量时淘汰最旧的导入行并发一次提示，页脚统计跟着更新
 */
TEST_F(MemoryImport, CapacityEvictsAndNotifies)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());
    ASSERT_TRUE(settings.setValue(kMemoryConsentAsked, true).has_value());

    auto        engine = std::make_unique<ConstantOcrEngine>(QStringLiteral("本周报价表"));
    Coordinator coordinator{settings, databasePath(), std::move(engine), 2};
    coordinator.start();

    const auto                         moment = QDateTime::currentDateTime();
    std::vector<std::filesystem::path> importPaths;
    for (int index = 0; index < 3; ++index)
    {
        MemoryUpsert entry;
        entry.filePath   = root() / "preset" / QStringLiteral("import-%1.png").arg(index).toStdWString();
        entry.source     = MemorySource::Import;
        entry.capturedAt = moment.addSecs(index);
        entry.size       = QSize{8, 8};
        ASSERT_TRUE(coordinator.index().upsertPending(entry).has_value());
        importPaths.push_back(entry.filePath);
    }

    QSignalSpy messages(&coordinator, &Coordinator::messageForUser);
    QSignalSpy stats(&coordinator, &Coordinator::statsChanged);

    const auto shot = makePng(QStringLiteral("capacity/capture.png"), QSize{8, 8}, Qt::yellow);
    coordinator.noteScreenshot(shot, QSize{8, 8}, CaptureSourceInfo{QStringLiteral("notepad.exe"), QStringLiteral("笔记")});

    const auto finalStats = coordinator.index().stats(0);
    ASSERT_TRUE(finalStats.has_value());
    EXPECT_EQ(finalStats->totalItems, 2) << "上限两条时只该留下两行";
    EXPECT_FALSE(findResult(coordinator.index(), importPaths.front()).has_value()) << "最旧的导入行应被淘汰";
    EXPECT_TRUE(findResult(coordinator.index(), importPaths.back()).has_value());
    EXPECT_TRUE(findResult(coordinator.index(), shot).has_value());

    ASSERT_EQ(messages.count(), 1) << "一次淘汰只提示一次";
    EXPECT_EQ(messages.at(0).at(1).value<ToastKind>(), ToastKind::Information);
    EXPECT_TRUE(messages.at(0).at(0).toString().contains(QStringLiteral("上限")));

    ASSERT_GE(stats.count(), 1);
    EXPECT_EQ(stats.last().at(0).value<MemoryStats>().totalItems, 2);
}

/**
 * @brief 导入超过容量时只合并提示一次，被淘汰的行不再排队读图识别
 */
TEST_F(MemoryImport, CapacityEvictionDuringImportNotifiesOnce)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());
    ASSERT_TRUE(settings.setValue(kMemoryConsentAsked, true).has_value());

    auto        engine = std::make_unique<ConstantOcrEngine>(QStringLiteral("本周报价表"));
    auto       *fake   = engine.get();
    Coordinator coordinator{settings, databasePath(), std::move(engine), 3};
    coordinator.start();

    const auto folder = importFolder();
    for (int index = 0; index < 10; ++index)
    {
        static_cast<void>(makePng(QStringLiteral("imports/capacity-%1.png").arg(index), QSize{8, 8}, Qt::cyan));
    }

    QSignalSpy messages(&coordinator, &Coordinator::messageForUser);
    QSignalSpy finished(&coordinator, &Coordinator::importFinished);
    coordinator.importFolder(folder);
    ASSERT_TRUE(waitUntil([&] { return finished.count() > 0; }, kImportPatienceMs)) << "导入没有结束";

    const auto report = finished.last().at(0).value<MemoryImportReport>();
    EXPECT_EQ(report.scanned, 10);
    EXPECT_EQ(report.inserted, 10);
    EXPECT_EQ(report.failed, 0);
    EXPECT_EQ(report.skipped, 0);

    const auto finalStats = coordinator.index().stats(0);
    ASSERT_TRUE(finalStats.has_value());
    EXPECT_EQ(finalStats->totalItems, 3) << "导入结束后总数应落在容量上限";

    ASSERT_EQ(messages.count(), 1) << "整个导入只该给一条容量提示，而不是每行一条";
    EXPECT_EQ(messages.at(0).at(1).value<ToastKind>(), ToastKind::Information);
    EXPECT_TRUE(messages.at(0).at(0).toString().contains(QStringLiteral("已达上限")));

    ASSERT_TRUE(waitUntil(
            [&]
            {
                const auto stats = coordinator.index().stats(0);
                return stats.has_value() && stats->pendingOcr == 0;
            },
            kImportPatienceMs))
            << "后台识别没有收口";
    EXPECT_EQ(fake->calls(), 3) << "被容量淘汰的行不该再进入识别";
}

/**
 * @brief 每块事务开不起来时整块按失败计数，不写入任何行，也不给容量提示
 */
TEST_F(MemoryImport, ChunkFailureCountsRowsFailedWithoutWriting)
{
    SettingsStore settings{settingsPath()};
    ASSERT_TRUE(settings.setValue(kMemoryEnabled, true).has_value());
    ASSERT_TRUE(settings.setValue(kMemoryConsentAsked, true).has_value());

    auto        engine = std::make_unique<ConstantOcrEngine>(QStringLiteral("本周报价表"));
    auto       *fake   = engine.get();
    Coordinator coordinator{settings, databasePath(), std::move(engine)};
    coordinator.start();

    const auto folder = importFolder();
    for (int index = 0; index < 3; ++index)
    {
        static_cast<void>(makePng(QStringLiteral("imports/blocked-%1.png").arg(index), QSize{8, 8}, Qt::darkCyan));
    }

    // 占住连接上的事务，让导入块的 begin 失败，模拟整块开不起来
    ASSERT_TRUE(coordinator.index().beginTransaction().has_value());

    QSignalSpy messages(&coordinator, &Coordinator::messageForUser);
    QSignalSpy finished(&coordinator, &Coordinator::importFinished);
    coordinator.importFolder(folder);
    ASSERT_TRUE(waitUntil([&] { return finished.count() > 0; }, kImportPatienceMs)) << "导入没有结束";

    const auto report = finished.last().at(0).value<MemoryImportReport>();
    EXPECT_EQ(report.scanned, 3);
    EXPECT_EQ(report.inserted, 0);
    EXPECT_EQ(report.failed, 3) << "事务开不起来时块内候选应计入失败";
    EXPECT_EQ(report.skipped, 0);
    EXPECT_EQ(fake->calls(), 0) << "没有入库的行不该进入识别";
    EXPECT_EQ(messages.count(), 0) << "没有淘汰就不该有容量提示";

    ASSERT_TRUE(coordinator.index().rollbackTransaction().has_value());
    EXPECT_EQ(resultCount(coordinator.index()), 0) << "回滚后不该留下导入行";
}
