// 界面按键清册的取值判据：默认那颗、用户换过的那颗、以及填歪了的那一颗。
// 这条链的下游是「按屏幕上写的那颗键能不能切到工具」，所以三档都要钉住，不能只测快乐路径。
#include "Ui/AppShortcuts.h"

#include "Hotkey/HotkeyCombination.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QString>

#include <algorithm>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using GyanisScribe::Hotkey::HotkeyCombination;
    using GyanisScribe::Settings::SettingsStore;
    using GyanisScribe::Ui::appShortcutEntries;
    using GyanisScribe::Ui::AppShortcutEntry;
    using GyanisScribe::Ui::appShortcutEntry;
    using GyanisScribe::Ui::appShortcutFamilyName;
    using GyanisScribe::Ui::appShortcutKey;
    using GyanisScribe::Ui::appShortcutText;
    using GyanisScribe::Ui::appShortcutValue;
    using GyanisScribe::Ui::isAppShortcutHit;

    /**
     * @brief 一份用例独占的配置文件
     * @details ctest 并行时每个用例是独立进程，共用一份 INI 就会读到别人写进去的那一颗键。
     */
    class AppShortcutFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestAppShortcuts";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QCoreApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
            m_iniPath        = std::filesystem::temp_directory_path() / std::format("GyanisScribe-AppShortcuts-{}-{}.ini", info->test_suite_name(), info->name());
            std::error_code ignored;
            std::filesystem::remove(m_iniPath, ignored);
            m_settings = std::make_unique<SettingsStore>(m_iniPath);
        }

        void TearDown() override
        {
            m_settings.reset();
            std::error_code ignored;
            std::filesystem::remove(m_iniPath, ignored);
        }

        /// 往清册里某一档写一颗键
        [[nodiscard]] bool storeKey(const std::string_view token, const QString &value) const
        {
            return static_cast<bool>(m_settings->setValue(appShortcutKey(token).toStdString(), value));
        }

        std::filesystem::path          m_iniPath;
        std::unique_ptr<SettingsStore> m_settings;

        inline static std::unique_ptr<QCoreApplication> application; ///< QSettings 要的那一个应用对象
    };

    TEST_F(AppShortcutFixture, ConfiguredKeyReplacesTheDefault)
    {
        const auto *pen = appShortcutEntry("Annotation/Pen");
        ASSERT_NE(pen, nullptr) << "清册里没有画笔这一档，这条判据就没接线";
        ASSERT_TRUE(storeKey(pen->token, QStringLiteral("K"))) << "写入失败";

        EXPECT_EQ(appShortcutValue(*m_settings, *pen).toText(), QStringLiteral("K")) << "换过的键没生效";
        EXPECT_EQ(appShortcutText(*m_settings, *pen), QStringLiteral("K")) << "提示与分派吃的是两处读数，分家了就会出现「按了没反应」";
        EXPECT_EQ(appShortcutValue(*m_settings, *pen).key(), Qt::Key_K);
    }

    TEST_F(AppShortcutFixture, UnsetRowResolvesToItsDefaultKey)
    {
        for (const auto &entry: appShortcutEntries())
        {
            EXPECT_EQ(appShortcutValue(*m_settings, entry).toText(), QString::fromUtf8(entry.defaultKey.data(), static_cast<int>(entry.defaultKey.size())))
                    << std::string{entry.token};
        }
    }

    TEST_F(AppShortcutFixture, UnreadableStoredTextFallsBackToTheDefaultWithoutBeingRewritten)
    {
        const auto *arrow = appShortcutEntry("Annotation/Arrow");
        ASSERT_NE(arrow, nullptr);
        ASSERT_TRUE(storeKey(arrow->token, QStringLiteral("Ctrl+"))) << "写入失败";

        // 读不出的那一串按默认那颗走，不替用户猜一颗近似的键
        EXPECT_EQ(appShortcutValue(*m_settings, *arrow).toText(), QStringLiteral("A"));
        // 但配置里那一段原文不动：界面要还能显示用户到底敲了什么，改起来才有靶子
        EXPECT_EQ(m_settings->stringValue(appShortcutKey(arrow->token).toStdString(), QString{}), QStringLiteral("Ctrl+"));
    }

    /**
     * @brief 钉住清册本身是自洽的：默认那颗能当界面按键、同族内不撞、标识不重复，而且键名落在 Ui/Shortcut/ 那一族
     * @details 这条是「表写错了会在运行期表现为某档按键没反应」的前置闸门：默认那颗读不出、
     *          同窗两档共用一颗键、或标识撞了，都该在建表这一步就红。
     *          唯一性按族分组判：不同窗口撞字母是常态（遮罩的 V 复制尺寸、贴图的 V 上下翻转），
     *          整体判唯一会把这种正常形状当错误——那正是上一版这条判据红掉的原因。
     */
    TEST_F(AppShortcutFixture, RegistryRowsAreBindableUniqueAndKeyedUnderThePrefix)
    {
        ASSERT_FALSE(appShortcutEntries().empty()) << "清册空着，下面每一段循环都是空跑";

        std::vector<std::string> tokens;
        for (const auto &entry: appShortcutEntries())
        {
            const auto parsed = HotkeyCombination::parse(entry.defaultKey);
            ASSERT_TRUE(parsed.has_value()) << std::string{entry.token} << " 的默认键读不出：" << parsed.error().message;
            EXPECT_TRUE(parsed->checkUsableInWindow().has_value()) << std::string{entry.token} << " 的默认键不能当界面按键";
            // 默认那颗都是裸键：它们不该顺手成为一条全局热键，否则在别的程序里也会抢键
            EXPECT_FALSE(parsed->checkUsableGlobally().has_value()) << std::string{entry.token} << " 的默认键带修饰键，那是全局档的事";

            // 族名空 = 设置页那一栏没有小标题，读的人分不清这颗键归哪个窗
            EXPECT_FALSE(appShortcutFamilyName(entry.token).empty()) << std::string{entry.token} << " 的标识前缀没有对应的界面名";
            EXPECT_EQ(appShortcutKey(entry.token), QStringLiteral("Ui/Shortcut/") + QString::fromUtf8(entry.token.data(), static_cast<int>(entry.token.size())))
                    << "键名要落在 kAppShortcutPrefix 那一族里，设置页与订阅都按这个前缀找";

            tokens.push_back(std::string{entry.token});
        }
        std::ranges::sort(tokens);
        EXPECT_EQ(std::ranges::adjacent_find(tokens), tokens.end()) << "标识重复会让两档读到同一个配置键";

        // 同族之内一颗键只能归一档：按下它时有两档认领就是「谁都不该赢」的那件事
        for (const auto &entry: appShortcutEntries())
        {
            int shared = 0;
            for (const auto &other: appShortcutEntries())
            {
                if (other.defaultKey == entry.defaultKey && appShortcutFamilyName(other.token) == appShortcutFamilyName(entry.token))
                {
                    ++shared;
                }
            }
            EXPECT_EQ(shared, 1) << std::string{entry.token} << " 与同族的另一档共用了默认那颗键";
        }
    }

    /**
     * @brief 钉住带反向档的那两档：反向档跟着用户换过的那颗键走，而不是钉死在原来那个字母上
     * @details 贴图「转正」与遮罩「挑最近选区」各有一个按住 Shift 的反向挡。判三件事：
     *          默认那颗认裸按也认 Shift、不认带 Ctrl 的；换成带 Ctrl 的组合后正向与反向都搬过去；
     *          以及不申请反向挡时 Shift 那一下不算命中（标注那十三档就没有反向挡）。
     */
    TEST_F(AppShortcutFixture, ShiftVariantTravelsWithTheBoundKey)
    {
        const auto *rotate = appShortcutEntry("Pin/Rotate");
        ASSERT_NE(rotate, nullptr);

        EXPECT_TRUE(isAppShortcutHit(*m_settings, *rotate, Qt::Key_R, Qt::NoModifier, true)) << "默认那颗的裸按该命中";
        EXPECT_TRUE(isAppShortcutHit(*m_settings, *rotate, Qt::Key_R, Qt::ShiftModifier, true)) << "反向挡就是同一颗键多按一个 Shift";
        EXPECT_FALSE(isAppShortcutHit(*m_settings, *rotate, Qt::Key_R, Qt::ShiftModifier)) << "不申请反向挡时 Shift 那一下不算命中";
        EXPECT_FALSE(isAppShortcutHit(*m_settings, *rotate, Qt::Key_R, Qt::ControlModifier, true)) << "带 Ctrl 的那一下归组合键，不归这一档";

        ASSERT_TRUE(storeKey(rotate->token, QStringLiteral("Ctrl+G"))) << "写入失败";
        EXPECT_TRUE(isAppShortcutHit(*m_settings, *rotate, Qt::Key_G, Qt::ControlModifier, true)) << "换过之后正向挡要跟着搬";
        EXPECT_TRUE(isAppShortcutHit(*m_settings, *rotate, Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier, true)) << "反向挡也跟着搬：那颗组合再加一个 Shift";
        EXPECT_FALSE(isAppShortcutHit(*m_settings, *rotate, Qt::Key_R, Qt::NoModifier, true)) << "旧那颗键不再归这一档";
    }
} // namespace
