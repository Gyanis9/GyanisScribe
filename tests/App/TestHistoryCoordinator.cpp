// 历史协调器的信号契约：清单每次变动只报一次事实，记忆库据此跟读；索引不在这一层做。
// 判据用计数器 lambda 而不引 QSignalSpy：TestShell 的部署清单里没有 Qt6Test 那一份 DLL。

#include "App/HistoryCoordinator.h"

#include "Core/Capture/SourceInfo.h"
#include "Core/History/HistoryStore.h"
#include "Core/Settings/SettingsStore.h"

#include <QSize>
#include <QString>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>

using GyanisScribe::App::HistoryCoordinator;
using GyanisScribe::Core::CaptureSourceInfo;
using GyanisScribe::Core::HistoryKind;
using GyanisScribe::Core::HistoryStore;
using GyanisScribe::Core::SettingsStore;

namespace
{
    class HistoryCoordinatorFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
            m_root           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-HistorySignal.{}_{}", info->test_suite_name(), info->name());
            std::filesystem::remove_all(m_root);
            std::filesystem::create_directories(m_root);

            m_settings    = std::make_unique<SettingsStore>(m_root / "settings.ini");
            m_history     = std::make_unique<HistoryStore>(m_root / "history.json");
            m_coordinator = std::make_unique<HistoryCoordinator>(*m_history, *m_settings);

            // 同一线程上的直连：record 返回时计数已经落定，不需要等事件循环
            QObject::connect(m_coordinator.get(), &HistoryCoordinator::recorded, m_coordinator.get(),
                             [this](const std::filesystem::path &filePath, const HistoryKind kind, const QSize &size, const CaptureSourceInfo &source)
                             {
                                 ++m_recordedCount;
                                 m_recordedPath   = filePath;
                                 m_recordedKind   = kind;
                                 m_recordedSize   = size;
                                 m_recordedSource = source;
                             });
            QObject::connect(m_coordinator.get(), &HistoryCoordinator::removed, m_coordinator.get(),
                             [this](const std::filesystem::path &filePath)
                             {
                                 ++m_removedCount;
                                 m_removedPath = filePath;
                             });
            QObject::connect(m_coordinator.get(), &HistoryCoordinator::cleared, m_coordinator.get(), [this] { ++m_clearedCount; });
        }

        void TearDown() override
        {
            m_coordinator.reset();
            m_history.reset();
            m_settings.reset();
            std::error_code ignored;
            std::filesystem::remove_all(m_root, ignored);
        }

        std::filesystem::path               m_root;              ///< 这一格用例自己的临时目录
        std::unique_ptr<SettingsStore>      m_settings;          ///< 保留策略来源
        std::unique_ptr<HistoryStore>       m_history;           ///< 清单本体
        std::unique_ptr<HistoryCoordinator> m_coordinator;       ///< 被测对象
        int                                 m_recordedCount = 0; ///< recorded 的条数
        std::filesystem::path               m_recordedPath;      ///< recorded 带来的路径
        HistoryKind                         m_recordedKind{};    ///< recorded 带来的类别
        QSize                               m_recordedSize;      ///< recorded 带来的尺寸
        CaptureSourceInfo                   m_recordedSource;    ///< recorded 带来的来源
        int                                 m_removedCount = 0;  ///< removed 的条数
        std::filesystem::path               m_removedPath;       ///< removed 带来的路径
        int                                 m_clearedCount = 0;  ///< cleared 的条数
    };
} // namespace

/**
 * @brief 钉住 record 把截图来源原样带进 recorded
 * @details 记忆库靠这条信号记下应用与窗口标题；字段在传递中被清掉或改名，
 *          索引里就只剩一条没有来源的记录。
 */
TEST_F(HistoryCoordinatorFixture, RecordEmitsTheGivenSource)
{
    const auto              path = m_root / "shots" / "one.png";
    const QSize             size{640, 480};
    const CaptureSourceInfo source{QStringLiteral("notepad.exe"), QStringLiteral("未命名 - 记事本")};

    m_coordinator->record(path, HistoryKind::Screenshot, size, source);

    EXPECT_EQ(m_recordedCount, 1) << "一次记录只该报一声 recorded";
    EXPECT_EQ(m_recordedPath, path);
    EXPECT_EQ(m_recordedKind, HistoryKind::Screenshot);
    EXPECT_EQ(m_recordedSize, size);
    EXPECT_EQ(m_recordedSource.processName, source.processName) << "进程名在传递中改了值";
    EXPECT_EQ(m_recordedSource.windowTitle, source.windowTitle) << "窗口标题在传递中改了值";
}

/**
 * @brief 钉住 removed 只在清单真的删掉该条之后发出
 * @details 记忆库按路径删行；失败路径也报 removed 会让索引与历史各说各的。
 */
TEST_F(HistoryCoordinatorFixture, RemoveEmitsOnlyWhenTheEntryWasRemoved)
{
    const auto path = m_root / "shots" / "two.png";
    m_coordinator->record(path, HistoryKind::Screenshot, QSize{320, 200}, CaptureSourceInfo{});

    m_coordinator->remove(path);
    EXPECT_EQ(m_removedCount, 1) << "删掉了记录却没有报 removed";
    EXPECT_EQ(m_removedPath, path);

    m_coordinator->remove(path);
    EXPECT_EQ(m_removedCount, 1) << "清单里已经没这条记录，失败路径却再报了一声 removed";
}

/**
 * @brief 钉住 clear 在清单清空后报一声 cleared
 * @details 记忆库靠它清掉全部截图来源的行；没有这一声，历史页清空之后索引还留着旧条目。
 */
TEST_F(HistoryCoordinatorFixture, ClearEmitsCleared)
{
    m_coordinator->record(m_root / "shots" / "three.png", HistoryKind::Screenshot, QSize{100, 80}, CaptureSourceInfo{});

    m_coordinator->clear();

    EXPECT_EQ(m_clearedCount, 1) << "清单已清空却没有报 cleared";
}

/**
 * @brief 钉住录屏记录不带来源也能编译并照常报 recorded
 * @details 录屏记账沿用默认空来源；这一格是「既有调用点不改仍编得过」的编译期判据。
 */
TEST_F(HistoryCoordinatorFixture, RecordingRecordKeepsAnEmptySource)
{
    m_coordinator->record(m_root / "clips" / "one.mp4", HistoryKind::Recording, QSize{1920, 1080});

    EXPECT_EQ(m_recordedCount, 1);
    EXPECT_EQ(m_recordedKind, HistoryKind::Recording);
    EXPECT_TRUE(m_recordedSource.processName.isEmpty()) << "录屏记录不该凭空带上进程名";
    EXPECT_TRUE(m_recordedSource.windowTitle.isEmpty()) << "录屏记录不该凭空带上窗口标题";
}

/**
 * @brief 钉住清单写不进去时不发 recorded
 * @details 记忆库按 recorded 收条目；成品没进历史却报了记录，索引里就会出现一条盘上没有的成品。
 */
TEST_F(HistoryCoordinatorFixture, RecordFailureDoesNotEmitRecorded)
{
    {
        std::ofstream broken{m_root / "history.json", std::ios::binary | std::ios::trunc};
        broken << "not a history manifest";
    }

    m_coordinator->record(m_root / "shots" / "four.png", HistoryKind::Screenshot, QSize{200, 100}, CaptureSourceInfo{});

    EXPECT_EQ(m_recordedCount, 0) << "清单没写进去却报了 recorded";
}
