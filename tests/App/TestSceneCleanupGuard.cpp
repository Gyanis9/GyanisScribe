// 清场守卫的直测：标记与平台动作要一起进退，恢复失败时标记必须留住。
// 假集成只演 setSceneCleared 这一项，把每次开关与次序记下来；配置用真的 SettingsStore 落在临时目录。

#include "App/SceneCleanupGuard.h"

#include "Base/Text.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"
#include "Platform/IPlatformIntegration.h"
#include "Platform/PlatformCapabilities.h"

#include <gtest/gtest.h>

#include <QString>

#include <algorithm>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace GyanisScribe
{
    namespace
    {
        /**
         * @brief 只演清场这一项的集成替身
         * @details 开关按调用次序留在 calls 里，两次失败档各自能点名；
         *          其余纯虚方法照最小档回绝，本组用例不经过它们。
         */
        class SceneClearIntegrationStub final : public Platform::IPlatformIntegration
        {
        public:
            [[nodiscard]] Platform::PlatformCapabilities capabilities() const noexcept override
            {
                return {};
            }

            [[nodiscard]] QString displayName() const override
            {
                return QStringLiteral("清场替身");
            }

            [[nodiscard]] std::unique_ptr<Platform::IHotkeyBackend> createHotkeyBackend() override
            {
                return nullptr;
            }

            [[nodiscard]] Base::Expected<Platform::ForegroundWindow> foregroundWindow() const override
            {
                return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported, "替身不演前台窗口这一项"});
            }

            [[nodiscard]] Base::Expected<bool> autostartEnabled() const override
            {
                return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported, "替身不演开机启动这一项"});
            }

            [[nodiscard]] Base::Expected<void> setAutostart(const bool enabled) override
            {
                static_cast<void>(enabled);
                return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported, "替身不演开机启动这一项"});
            }

            [[nodiscard]] Base::Expected<std::vector<Platform::ScreenWindow>> screenWindows() const override
            {
                return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported, "替身不演窗口清单这一项"});
            }

            [[nodiscard]] Base::Expected<std::vector<QRect>> elementChainAtPoint(const QPoint &globalLogicalPoint, const std::vector<quintptr> &excludeWindowHandles) const override
            {
                static_cast<void>(globalLogicalPoint);
                static_cast<void>(excludeWindowHandles);
                return std::unexpected(Base::Error{Base::ErrorCode::PlatformUnsupported, "替身不演界面元素这一项"});
            }

            /**
             * @brief 记下开关；按点名的那一档交回失败
             * @param cleared true 为清场，false 为恢复
             * @return 该档没点名失败时空载荷；点名了则回带中文原因的失败
             */
            [[nodiscard]] Base::Expected<void> setSceneCleared(const bool cleared) override
            {
                calls.push_back(cleared);
                if (cleared && failOnClear)
                {
                    return std::unexpected(Base::Error{Base::ErrorCode::Busy, "替身：清场没有生效"});
                }
                if (!cleared && failOnRestore)
                {
                    return std::unexpected(Base::Error{Base::ErrorCode::Busy, "替身：恢复没有生效"});
                }
                return {};
            }

            std::vector<bool> calls;                 ///< 每次开关，按调用次序
            bool              failOnClear   = false; ///< 清场这一档是否交回失败
            bool              failOnRestore = false; ///< 恢复这一档是否交回失败
        };

        /**
         * @brief 守卫用例的装置：每格一份临时目录里的配置
         */
        class SceneCleanupGuardTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
                m_root           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-SceneCleanupGuard.{}_{}", info->test_suite_name(), info->name());
                std::filesystem::remove_all(m_root);
                std::filesystem::create_directories(m_root);
                m_settings = std::make_unique<Core::SettingsStore>(m_root / "settings.ini");
            }

            void TearDown() override
            {
                m_settings.reset();
                std::error_code ignored;
                std::filesystem::remove_all(m_root, ignored);
            }

            /// 当前标记的文本；没写过是空串
            [[nodiscard]] QString marker() const
            {
                return m_settings->stringValue(Core::kSceneClearMarker, QString{});
            }

            std::filesystem::path                m_root;     ///< 这一格用例自己的临时目录
            std::unique_ptr<Core::SettingsStore> m_settings; ///< 守卫写入标记的那一份配置
        };

        /**
         * @brief 清场成功要同时留下标记；恢复成功后标记归零
         * @details 标记是崩溃后补恢复的唯一线索，写晚了或忘清都会让下次启动的判断失真。
         */
        TEST_F(SceneCleanupGuardTest, ClearWritesMarkerAndRestoreRemovesIt)
        {
            SceneClearIntegrationStub integration;
            App::SceneCleanupGuard    guard{integration, *m_settings};

            ASSERT_TRUE(guard.clear().has_value()) << "清场替身没点名失败，clear() 不该回错误";
            EXPECT_TRUE(guard.isCleared());
            ASSERT_EQ(integration.calls.size(), std::size_t{1}) << "一次 clear() 只该调一次平台接口";
            EXPECT_TRUE(integration.calls[0]) << "清场该给平台传 true";
            EXPECT_FALSE(marker().isEmpty()) << "清场成功却没写标记：崩溃后没人知道要恢复";

            ASSERT_TRUE(guard.restore().has_value());
            EXPECT_FALSE(guard.isCleared());
            ASSERT_EQ(integration.calls.size(), std::size_t{2});
            EXPECT_FALSE(integration.calls[1]) << "恢复该给平台传 false";
            EXPECT_TRUE(marker().isEmpty()) << "恢复成功却没清标记：下次启动会重复恢复";
        }

        /**
         * @brief 恢复幂等：第二次直接成功，平台只收到一次 false
         * @details 收场处与析构各调一次 restore，重复调用不能把平台动作再翻一遍。
         */
        TEST_F(SceneCleanupGuardTest, RestoreIsIdempotent)
        {
            SceneClearIntegrationStub integration;
            App::SceneCleanupGuard    guard{integration, *m_settings};

            ASSERT_TRUE(guard.clear().has_value());
            ASSERT_TRUE(guard.restore().has_value());
            ASSERT_TRUE(guard.restore().has_value()) << "第二次恢复该因未清场直接成功";
            ASSERT_EQ(integration.calls.size(), std::size_t{2}) << "第二次恢复又调了平台接口";
            EXPECT_EQ(std::ranges::count(integration.calls, false), 1) << "平台收到的 false 不止一次";
            EXPECT_TRUE(marker().isEmpty());
        }

        /**
         * @brief 清场失败不写标记，并补发一次恢复把半边动作撤掉
         * @details 平台可能已经动了一半（藏了图标、发了显示桌面），失败路径不能把桌面留在半截。
         */
        TEST_F(SceneCleanupGuardTest, FailedClearDoesNotWriteMarkerAndUndoesThePlatformAction)
        {
            SceneClearIntegrationStub integration;
            integration.failOnClear = true;
            App::SceneCleanupGuard guard{integration, *m_settings};

            const auto cleared = guard.clear();
            ASSERT_FALSE(cleared.has_value()) << "平台回了失败，clear() 不该当成清场成功";
            EXPECT_EQ(cleared.error().message, "替身：清场没有生效") << "原错误该原样交回";
            EXPECT_FALSE(guard.isCleared());
            EXPECT_TRUE(marker().isEmpty()) << "清场失败却写了标记：下次启动会去恢复一个没发生的清场";
            ASSERT_EQ(integration.calls.size(), std::size_t{2});
            EXPECT_TRUE(integration.calls[0]);
            EXPECT_FALSE(integration.calls[1]) << "清场失败没有补发一次恢复：半边动作留在了桌面上";
        }

        /**
         * @brief 只有残留标记的临时守卫恢复失败后，析构不再补一次平台恢复
         * @details 启动兜底用的就是这种守卫：它显式 restore() 过，失败时平台可能已经恢复成功、
         *          只是标记没写掉；析构再发一次会把恢复翻成「全部最小化」，甚至清掉留给下次启动的标记。
         */
        TEST_F(SceneCleanupGuardTest, DestructorDoesNotRetryAStaleMarker)
        {
            SceneClearIntegrationStub integration;
            integration.failOnRestore = true;
            ASSERT_TRUE(m_settings->setValue(Core::kSceneClearMarker, QStringLiteral("2026-11-12T10:00:00.000")).has_value());

            {
                App::SceneCleanupGuard guard{integration, *m_settings};
                const auto             restored = guard.restore();
                ASSERT_FALSE(restored.has_value()) << "前提：这一档恢复要失败，析构是否重试才判得出来";
                ASSERT_EQ(integration.calls.size(), std::size_t{1}) << "显式恢复该恰好调一次平台";
            }

            EXPECT_EQ(integration.calls.size(), std::size_t{1}) << "只有标记残留的守卫在析构里又调了一次平台恢复";
            EXPECT_FALSE(marker().isEmpty()) << "析构把要留给下次启动的标记清掉了";
        }

        /**
         * @brief 恢复失败要留住标记，留给下次启动再试
         * @details 标记一清，崩溃恢复就再也不知道桌面还处于清场态。
         */
        TEST_F(SceneCleanupGuardTest, FailedRestoreKeepsMarkerForNextLaunch)
        {
            SceneClearIntegrationStub integration;
            integration.failOnRestore = true;
            App::SceneCleanupGuard guard{integration, *m_settings};

            ASSERT_TRUE(guard.clear().has_value());
            const auto restored = guard.restore();
            ASSERT_FALSE(restored.has_value()) << "平台回了失败，restore() 不该当成恢复成功";
            EXPECT_EQ(restored.error().message, "替身：恢复没有生效");
            EXPECT_TRUE(guard.isCleared()) << "恢复失败却把内存状态清成未清场，下一轮不会再试";
            EXPECT_FALSE(marker().isEmpty()) << "恢复失败把标记清了：下次启动不会再补恢复";
            ASSERT_EQ(integration.calls.size(), std::size_t{2});
            EXPECT_FALSE(integration.calls[1]);
        }

    } // namespace
} // namespace GyanisScribe
