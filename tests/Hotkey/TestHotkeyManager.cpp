#include "Hotkey/HotkeyManager.h"

#include "Platform/IHotkeyBackend.h"
#include "Platform/IPlatformIntegration.h"
#include "Platform/UnsupportedHotkeyBackend.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"

#include <QCoreApplication>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <memory>
#include <set>
#include <string>
#include <vector>

using GyanisScribe::Core::Error;
using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Core::Expected;
using GyanisScribe::Hotkey::HotkeyAction;
using GyanisScribe::Hotkey::HotkeyManager;
using GyanisScribe::Hotkey::HotkeyReloadReport;
using GyanisScribe::Platform::IHotkeyBackend;
using GyanisScribe::Platform::IPlatformIntegration;
using GyanisScribe::Platform::PlatformCapabilities;
using GyanisScribe::Platform::UnsupportedHotkeyBackend;
using GyanisScribe::Settings::SettingsStore;

namespace
{
    /// 替身照抄生产端的取值词表：登记进来的是 Qt 域修饰键 + 按键，不是自造的一套编号
    class FakeHotkeyBackend : public IHotkeyBackend
    {
    public:
        struct Registration
        {
            int                   identifier;
            Qt::KeyboardModifiers modifiers;
            Qt::Key               key;
        };

        explicit FakeHotkeyBackend(std::set<int> identifiersToReject = {}) : m_identifiersToReject(std::move(identifiersToReject))
        {
        }

        Expected<void> registerHotkey(int identifier, Qt::KeyboardModifiers modifiers, Qt::Key key) override
        {
            registrations.push_back(Registration{identifier, modifiers, key});
            if (m_identifiersToReject.find(identifier) != m_identifiersToReject.end())
            {
                return std::unexpected(Error{ErrorCode::Busy, std::format("替身按编号 {} 拒绝登记：模拟组合键已被占用", identifier)});
            }
            if (liveRegistrations.find(identifier) != liveRegistrations.end())
            {
                return std::unexpected(Error{ErrorCode::InvalidArgument, std::format("编号 {} 在本后端内已经登记过", identifier)});
            }
            liveRegistrations.insert(identifier);
            return {};
        }

        Expected<void> unregisterHotkey(int identifier) override
        {
            liveRegistrations.erase(identifier);
            return {};
        }

        void unregisterAll() override
        {
            ++unregisterAllCalls;
            liveRegistrations.clear();
            registrations.clear();
        }

        /// 模拟平台送来的触发事件
        void fire(int identifier)
        {
            emit activated(identifier);
        }

        std::vector<Registration> registrations; ///< 本轮在册的登记（unregisterAll 会清空）
        std::set<int>             liveRegistrations;
        int                       unregisterAllCalls = 0;

    private:
        std::set<int> m_identifiersToReject;
    };

    class FakePlatformIntegration : public IPlatformIntegration
    {
    public:
        explicit FakePlatformIntegration(bool supportsHotkeys = true) : m_supportsHotkeys(supportsHotkeys)
        {
        }

        PlatformCapabilities capabilities() const noexcept override
        {
            PlatformCapabilities table;
            table.globalHotkeys = m_supportsHotkeys;
            return table;
        }

        QString displayName() const override
        {
            return QStringLiteral("测试替身");
        }

        std::unique_ptr<IHotkeyBackend> createHotkeyBackend() override
        {
            if (!m_supportsHotkeys)
            {
                // 与生产同形：平台说没有这个能力，交回去的就是仓库里那一份桩，而不是替身自造的第三种行为
                m_backendMirror = nullptr;
                return std::make_unique<UnsupportedHotkeyBackend>();
            }
            auto created    = std::make_unique<FakeHotkeyBackend>(m_identifiersToReject);
            m_backendMirror = created.get();
            return created;
        }

        FakeHotkeyBackend *m_backendMirror = nullptr; ///< 最近创建的那份后端，用例经它检查登记与触发
        std::set<int>      m_identifiersToReject;
        bool               m_supportsHotkeys = true;
    };

    std::unique_ptr<QCoreApplication> application;

    class HotkeyManagerFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            int   argumentCount  = 1;
            char  argumentZero[] = "TestHotkeyManager";
            char *arguments[]    = {argumentZero, nullptr};
            application          = std::make_unique<QCoreApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            const auto caseName = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            m_iniFile           = std::filesystem::temp_directory_path() / std::format("GyanisScribe-HotkeyManager-{}.ini", caseName);
            std::error_code ignored;
            std::filesystem::remove(m_iniFile, ignored);
        }

        std::filesystem::path m_iniFile;
    };
} // namespace

/**
 * @brief 钉住「设置里什么都没写」时的三条默认登记，取值逐条手算
 */
TEST_F(HotkeyManagerFixture, RegistersThreeDefaultsWhenSettingsAreEmpty)
{
    SettingsStore           settings(m_iniFile);
    FakePlatformIntegration integration;
    HotkeyManager           manager(settings, integration);

    const auto report = manager.reload();
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->registered, 3) << "三个动作都该登记上，失败项：" << report->failures.size();
    EXPECT_TRUE(report->failures.empty());

    const auto &registrations = integration.m_backendMirror->registrations;
    ASSERT_EQ(registrations.size(), 3U);
    // 编号 = 枚举序号 + 1，且每条的修饰键与按键按需求文档手算
    EXPECT_EQ(registrations[0].identifier, 1);
    EXPECT_EQ(registrations[0].modifiers, Qt::ControlModifier | Qt::AltModifier);
    EXPECT_EQ(registrations[0].key, Qt::Key_A);
    EXPECT_EQ(registrations[1].identifier, 2);
    EXPECT_EQ(registrations[1].key, Qt::Key_R);
    EXPECT_EQ(registrations[2].identifier, 3);
    EXPECT_EQ(registrations[2].key, Qt::Key_O);
}

/**
 * @brief 钉住设置里的组合覆盖默认值，且改一项会自己触发重登
 */
TEST_F(HotkeyManagerFixture, StoredCombinationOverridesDefault)
{
    SettingsStore           settings(m_iniFile);
    FakePlatformIntegration integration;
    HotkeyManager           manager(settings, integration);

    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kHotkeyCapture, QVariant(QStringLiteral("Ctrl+Shift+F1"))).has_value());
    // 写设置已经通过 valueChanged 触发过重登，这里不再手动 reload

    ASSERT_NE(integration.m_backendMirror, nullptr) << "改快捷键后没有建后端，说明 valueChanged 没接到 reload 上";
    const auto &registrations = integration.m_backendMirror->registrations;
    ASSERT_EQ(registrations.size(), 3U);
    EXPECT_EQ(registrations[0].identifier, 1);
    EXPECT_EQ(registrations[0].modifiers, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(registrations[0].key, Qt::Key_F1) << "存进去的组合没有生效，用户按到的还是默认的 Ctrl+Alt+A";
    EXPECT_EQ(manager.bindingText(HotkeyAction::Capture), QStringLiteral("Ctrl+Shift+F1"));
}

/**
 * @brief 钉住一项失败不牵连其余：坏值与被占用都各自记账，其余照常登记
 */
TEST_F(HotkeyManagerFixture, OneFailureDoesNotStopTheOthers)
{
    SettingsStore settings(m_iniFile);
    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kHotkeyRecord, QVariant(QStringLiteral("Alt"))).has_value());

    FakePlatformIntegration integration;
    integration.m_identifiersToReject = {3}; // 让 OCR 那一条被系统拒绝
    HotkeyManager manager(settings, integration);

    const auto report = manager.reload();
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->registered, 1) << "只有截图该登记上：坏值那条与被占用那条都要让路";
    ASSERT_EQ(report->failures.size(), 2U);

    EXPECT_EQ(report->failures[0].action, HotkeyAction::Record);
    EXPECT_EQ(report->failures[0].error.code, ErrorCode::InvalidArgument) << "设置里的「Alt」缺修饰键，属解析失败";
    EXPECT_EQ(report->failures[0].combination, QStringLiteral("Alt")) << "失败项要带上用户设的那个文本，否则无从下手";

    EXPECT_EQ(report->failures[1].action, HotkeyAction::Ocr);
    EXPECT_EQ(report->failures[1].error.code, ErrorCode::Busy) << "后端拒绝走的是被占用那一档";

    const auto &registrations = integration.m_backendMirror->registrations;
    ASSERT_EQ(registrations.size(), 2U) << "登记调用发生了两次：截图与被拒的 OCR，坏值那条不该去撞后端";
    EXPECT_EQ(registrations[0].identifier, 1);
    EXPECT_EQ(registrations[1].identifier, 3);
    EXPECT_EQ(integration.m_backendMirror->liveRegistrations.size(), 1U) << "OCR 被后端拒绝，它不该留在册上";
    EXPECT_EQ(integration.m_backendMirror->liveRegistrations.count(1), 1U);
}

/**
 * @brief 钉住重登先清场：旧登记不摘掉就会一直占着用户改掉的组合
 */
TEST_F(HotkeyManagerFixture, ReloadUnregistersEverythingBeforeReRegistering)
{
    SettingsStore           settings(m_iniFile);
    FakePlatformIntegration integration;
    HotkeyManager           manager(settings, integration);

    ASSERT_TRUE(manager.reload().has_value());
    ASSERT_TRUE(manager.reload().has_value());

    ASSERT_NE(integration.m_backendMirror, nullptr);
    EXPECT_GE(integration.m_backendMirror->unregisterAllCalls, 2) << "两轮重登至少两次清场";
    EXPECT_EQ(integration.m_backendMirror->registrations.size(), 3U) << "重登后在册的还是三条，不是六条";
}

/**
 * @brief 钉住触发的编号→动作映射与不在册编号的忽略
 */
TEST_F(HotkeyManagerFixture, TriggeredIdentifierMapsBackToAction)
{
    SettingsStore           settings(m_iniFile);
    FakePlatformIntegration integration;
    HotkeyManager           manager(settings, integration);
    ASSERT_TRUE(manager.reload().has_value());

    std::vector<HotkeyAction> triggered;
    QObject::connect(&manager, &HotkeyManager::actionTriggered, [&triggered](HotkeyAction action) { triggered.push_back(action); });

    integration.m_backendMirror->fire(1);
    integration.m_backendMirror->fire(2);
    integration.m_backendMirror->fire(3);
    ASSERT_EQ(triggered.size(), 3U);
    EXPECT_EQ(triggered[0], HotkeyAction::Capture);
    EXPECT_EQ(triggered[1], HotkeyAction::Record);
    EXPECT_EQ(triggered[2], HotkeyAction::Ocr);

    // 不在册的编号：不能瞎指派一个动作，也不能一个都不发
    integration.m_backendMirror->fire(99);
    EXPECT_EQ(triggered.size(), 3U) << "没登记过的编号被映射成了某个动作，会触发错的功能";
}

/**
 * @brief 钉住非热键的设置项改动不触发重登
 */
TEST_F(HotkeyManagerFixture, UnrelatedSettingChangeDoesNotReload)
{
    SettingsStore           settings(m_iniFile);
    FakePlatformIntegration integration;
    HotkeyManager           manager(settings, integration);
    ASSERT_TRUE(manager.reload().has_value());

    const auto callsBefore = integration.m_backendMirror->unregisterAllCalls;
    ASSERT_TRUE(settings.setValue("Paths/Screenshot", QVariant(QStringLiteral("D:/Shots"))).has_value());
    EXPECT_EQ(integration.m_backendMirror->unregisterAllCalls, callsBefore) << "改了截图目录也去重登热键，白占一次登记窗口";

    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kHotkeyOcr, QVariant(QStringLiteral("Ctrl+Alt+O"))).has_value());
    EXPECT_GT(integration.m_backendMirror->unregisterAllCalls, callsBefore) << "改了热键却没重登，新组合不会生效";
}

/**
 * @brief 钉住提示文本的回落：设置里的组合坏掉时界面不许显示一个按不出来的组合
 */
TEST_F(HotkeyManagerFixture, BindingTextFallsBackWhenStoredCombinationIsBroken)
{
    SettingsStore           settings(m_iniFile);
    FakePlatformIntegration integration;
    HotkeyManager           manager(settings, integration);

    ASSERT_TRUE(settings.setValue(GyanisScribe::Settings::kHotkeyCapture, QVariant(QStringLiteral("Ctrl+?"))).has_value());
    EXPECT_EQ(manager.bindingText(HotkeyAction::Capture), QStringLiteral("Ctrl+Alt+A")) << "坏值要回落到默认组合的显示文本";
    EXPECT_EQ(manager.bindingText(HotkeyAction::Record), QStringLiteral("Ctrl+Alt+R")) << "没被改动的动作不受影响";
}

/**
 * @brief 钉住平台没有该能力时不崩、且每条都记为不支持
 */
TEST_F(HotkeyManagerFixture, ReportsEveryActionUnsupportedWhenPlatformLacksHotkeys)
{
    SettingsStore           settings(m_iniFile);
    FakePlatformIntegration integration(/*supportsHotkeys = */ false);
    HotkeyManager           manager(settings, integration);

    const auto report = manager.reload();
    ASSERT_TRUE(report.has_value()) << "平台不支持不是管理器本身的失败，要按项交回";
    EXPECT_EQ(report->registered, 0);
    EXPECT_EQ(report->failures.size(), 3U);
    for (const auto &failure: report->failures)
    {
        EXPECT_EQ(failure.error.code, ErrorCode::PlatformUnsupported) << "每一项都该说清是本平台没有这个能力";
    }
}
