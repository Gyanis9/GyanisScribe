// 交付协调器的每轮记账：外部命令的 %file% 只认本轮落盘的那一份。
// 这里的命令一律点名一个绝不存在的程序：%file% 那一关漏了也只会起跑失败，不会真的拉起外部进程。
#include "App/DeliveryCoordinator.h"

#include "App/HistoryCoordinator.h"
#include "Base/Paths.h"
#include "Core/Capture/SourceInfo.h"
#include "Core/History/HistoryStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"

#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QString>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>
#include <string>

using GyanisScribe::App::DeliveryCoordinator;
using GyanisScribe::App::HistoryCoordinator;
using GyanisScribe::Core::CaptureSourceInfo;
using GyanisScribe::Core::HistoryKind;
using GyanisScribe::Core::HistoryStore;
using GyanisScribe::Core::SettingsStore;

namespace
{
    class DeliveryCoordinatorFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
            m_root           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Delivery.{}_{}", info->test_suite_name(), info->name());
            std::filesystem::remove_all(m_root);
            std::filesystem::create_directories(m_root);

            m_settings = std::make_unique<SettingsStore>(m_root / "settings.ini");
            ASSERT_TRUE(m_settings->setValue(GyanisScribe::Core::kPathScreenshot, GyanisScribe::Base::Paths::toDisplayText(m_root / "shots")).has_value());
            m_history      = std::make_unique<HistoryStore>(m_root / "history.json");
            m_historyOwner = std::make_unique<HistoryCoordinator>(*m_history, *m_settings);
            m_delivery     = std::make_unique<DeliveryCoordinator>(*m_settings, *m_historyOwner);
        }

        void TearDown() override
        {
            m_delivery.reset();
            m_historyOwner.reset();
            m_history.reset();
            m_settings.reset();
            std::error_code ignored;
            std::filesystem::remove_all(m_root, ignored);
        }

        /// 落一张盘上真有的图：路径账与「这一轮落成过文件」都从真实保存那一步来
        [[nodiscard]] bool saveOneFrame() const
        {
            QImage frame{32, 24, QImage::Format_ARGB32_Premultiplied};
            frame.fill(QColor(36, 87, 166));
            const auto saved = m_delivery->saveToDisk(frame);
            if (!saved)
            {
                ADD_FAILURE() << "样本图都没存下：" << saved.error().message;
                return false;
            }
            return true;
        }

        std::filesystem::path                m_root;         ///< 这一格用例自己的临时目录
        std::unique_ptr<SettingsStore>       m_settings;     ///< 输出档位来源
        std::unique_ptr<HistoryStore>        m_history;      ///< 清单本体
        std::unique_ptr<HistoryCoordinator>  m_historyOwner; ///< 落盘后记账的那一族
        std::unique_ptr<DeliveryCoordinator> m_delivery;     ///< 被测对象
    };
} // namespace

/**
 * @brief 钉住每轮开场清掉上一轮的落盘路径
 * @details 不清的话，本轮没落盘时 writtenFilePath() 还指着上一张：
 *          外部命令与命令行打到标准输出的都是那份旧成品。
 */
TEST_F(DeliveryCoordinatorFixture, BeginRoundClearsThePreviousRoundsFilePath)
{
    ASSERT_TRUE(saveOneFrame());
    ASSERT_FALSE(m_delivery->writtenFilePath().isEmpty()) << "先得有一份本轮落盘的路径，后面的判据才有内容";

    m_delivery->beginRound();

    EXPECT_TRUE(m_delivery->writtenFilePath().isEmpty()) << "上一轮的成品路径被带进了新一轮";
}

/**
 * @brief 钉住本轮没落成文件时，带 %file% 的命令当场拒绝，而不是拿上一张的路径去跑
 * @details 拒绝要说明「这一轮没有落成文件」，而不是笼统地报「程序起不来」：
 *          用户按那句提示会去查路径，真正的原因是这一步根本没产出文件。
 */
TEST_F(DeliveryCoordinatorFixture, PercentFileIsRejectedWhenThisRoundSavedNothing)
{
    ASSERT_TRUE(saveOneFrame());
    m_delivery->beginRound();

    // 程序名绝不存在的另一种意义：即使 %file% 那一关没拦住，这里也只会起跑失败
    const auto result = m_delivery->runAfterCaptureCommand(QStringLiteral("GyanisScribe-This-Program-Does-Not-Exist %file%"));
    ASSERT_FALSE(result.has_value()) << "本轮没有文件却把 %file% 喂给了外部命令";
    EXPECT_EQ(result.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument) << "报的是「程序起不来」而不是「这一轮没有文件」：" << result.error().message;
    EXPECT_NE(result.error().message.find("没有落成文件"), std::string::npos) << result.error().message;
}

/**
 * @brief 钉住没有命令时不算失败：设置里那一格留空是常态，不该冒出一条错误
 */
TEST_F(DeliveryCoordinatorFixture, EmptyCommandSucceeds)
{
    m_delivery->beginRound();

    EXPECT_TRUE(m_delivery->runAfterCaptureCommand(QString{}).has_value()) << "没填命令却报了一次失败";
}

/**
 * @brief 钉住落盘时把本轮的截图来源带进历史
 * @details 记忆库只从 HistoryCoordinator::recorded 收条目；来源在这一跳丢了，
 *          索引里就只剩路径与尺寸，按应用筛查的入口全部失效。
 */
TEST_F(DeliveryCoordinatorFixture, SaveCarriesTheRoundsSourceIntoHistory)
{
    const CaptureSourceInfo source{QStringLiteral("notepad.exe"), QStringLiteral("未命名 - 记事本")};
    int                     recordedCount = 0;
    HistoryKind             recordedKind{};
    CaptureSourceInfo       recordedSource;
    QObject::connect(m_historyOwner.get(), &HistoryCoordinator::recorded, m_historyOwner.get(),
                     [&](const std::filesystem::path &, const HistoryKind kind, const QSize &, const CaptureSourceInfo &captured)
                     {
                         ++recordedCount;
                         recordedKind   = kind;
                         recordedSource = captured;
                     });

    m_delivery->beginRound(source);
    ASSERT_TRUE(saveOneFrame());

    EXPECT_EQ(recordedCount, 1) << "落盘后没有报 recorded";
    EXPECT_EQ(recordedKind, HistoryKind::Screenshot);
    EXPECT_EQ(recordedSource.processName, source.processName) << "落盘时把本轮的进程名丢了";
    EXPECT_EQ(recordedSource.windowTitle, source.windowTitle) << "落盘时把本轮的窗口标题丢了";
}

/**
 * @brief 钉住不带来源开新一轮会把上一轮的来源清掉
 * @details 录屏与回溯走默认空来源；不清就会把上一轮的进程名记到这一轮的成品上。
 */
TEST_F(DeliveryCoordinatorFixture, BeginRoundWithoutSourceClearsThePreviousRoundsSource)
{
    CaptureSourceInfo recordedSource;
    QObject::connect(m_historyOwner.get(), &HistoryCoordinator::recorded, m_historyOwner.get(),
                     [&](const std::filesystem::path &, const HistoryKind, const QSize &, const CaptureSourceInfo &captured) { recordedSource = captured; });

    m_delivery->beginRound(CaptureSourceInfo{QStringLiteral("notepad.exe"), QStringLiteral("未命名 - 记事本")});
    ASSERT_TRUE(saveOneFrame());
    m_delivery->beginRound();
    ASSERT_TRUE(saveOneFrame());

    EXPECT_TRUE(recordedSource.processName.isEmpty()) << "上一轮的进程名被带进了这一轮";
    EXPECT_TRUE(recordedSource.windowTitle.isEmpty()) << "上一轮的窗口标题被带进了这一轮";
}

/**
 * @brief 钉住落盘文件名从本轮来源取应用、标题与屏幕
 * @details 模板里的三格上下文变量只有从本轮来源带进来才有值；进程名按去掉扩展名后的主名进名字，
 *          标题与屏幕名各自清洗后原样拼上。名字读错了，用户按文件名再找图就找不回来。
 */
TEST_F(DeliveryCoordinatorFixture, SaveUsesTheRoundsContextInTheFileName)
{
    ASSERT_TRUE(m_settings->setValue(GyanisScribe::Core::kSaveNameTemplate, QStringLiteral("%app%_%title%_%monitor%")).has_value());
    QImage frame{32, 24, QImage::Format_ARGB32_Premultiplied};
    frame.fill(QColor(36, 87, 166));
    m_delivery->beginRound(CaptureSourceInfo{QStringLiteral("Code.exe"), QStringLiteral("main.cpp"), QStringLiteral("Screen1"), QDateTime{QDate{2026, 11, 9}, QTime{8, 30, 15}}});

    const auto saved = m_delivery->saveToDisk(frame);

    ASSERT_TRUE(saved.has_value()) << saved.error().message;
    EXPECT_EQ(*saved, QStringLiteral("Code_main.cpp_Screen1.png")) << "落盘名字没带上本轮的应用、标题与屏幕";
}

/**
 * @brief 钉住来源为空时文件名回落到占位词
 * @details 录屏、回溯与取不到来源的截图都走空来源；三格变量要有稳定回落，
 *          保存不能因为拿不到应用名就失败或留下空段。
 */
TEST_F(DeliveryCoordinatorFixture, SaveWithoutSourceFallsBackToPlaceholderNames)
{
    ASSERT_TRUE(m_settings->setValue(GyanisScribe::Core::kSaveNameTemplate, QStringLiteral("%app%_%title%_%monitor%")).has_value());
    QImage frame{32, 24, QImage::Format_ARGB32_Premultiplied};
    frame.fill(QColor(36, 87, 166));
    m_delivery->beginRound();

    const auto saved = m_delivery->saveToDisk(frame);

    ASSERT_TRUE(saved.has_value()) << saved.error().message;
    EXPECT_EQ(*saved, QStringLiteral("app_window_screen.png")) << "空来源没有回落到占位词";
}
