// 交付协调器的每轮记账：外部命令的 %file% 只认本轮落盘的那一份。
// 这里的命令一律点名一个绝不存在的程序：%file% 那一关漏了也只会起跑失败，不会真的拉起外部进程。
#include "App/DeliveryCoordinator.h"

#include "App/HistoryCoordinator.h"
#include "Base/Paths.h"
#include "Core/History/HistoryStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"

#include <QColor>
#include <QImage>
#include <QString>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>
#include <string>

using GyanisScribe::App::DeliveryCoordinator;
using GyanisScribe::App::HistoryCoordinator;
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
