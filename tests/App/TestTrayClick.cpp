// 托盘鼠标动作那两下的直测：哪一下算哪一档、档位标记与配置键互指、读坏了落回默认且点名那串原文。
// 「一下鼠标做什么」这条通路此前整条没有，所以判据先把词表与回落钉住；接线（发得出那个信号）在 TestMainWindow 里钉。
#include "App/TrayClick.h"

#include "App/TrayController.h"
#include "Platform/PlatformCapabilities.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QSystemTrayIcon>

#include <QString>

#include <cstddef>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using GyanisScribe::App::resolveTrayClick;
using GyanisScribe::App::TrayAction;
using GyanisScribe::App::trayActionToken;
using GyanisScribe::App::TrayClickChoice;
using GyanisScribe::App::trayClickChoices;
using GyanisScribe::App::TrayClickOutcome;
using GyanisScribe::App::TrayClickRole;
using GyanisScribe::App::trayClickRoleForReason;
using GyanisScribe::App::trayClickRoleInfo;
using GyanisScribe::App::trayClickRoles;
using GyanisScribe::Platform::PlatformCapabilities;
using GyanisScribe::Settings::SettingsStore;

namespace
{
    /**
     * @brief 造一份能力齐平的能力表
     * @return 六位都置齐的能力表
     * @details 逐位赋值而不是一次填齐：将来加一位能力会在这里留下「没交代」的形状，
     *          而聚合一气写满会把新字段默默留在 false，判据就成了永真
     */
    [[nodiscard]] PlatformCapabilities everyCapability()
    {
        PlatformCapabilities capabilities;
        capabilities.globalHotkeys     = true;
        capabilities.clickThrough      = true;
        capabilities.windowDiscovery   = true;
        capabilities.windowEnumeration = true;
        capabilities.uiElements        = true;
        capabilities.autostart         = true;
        return capabilities;
    }

    [[nodiscard]] std::vector<TrayClickChoice> fullChoices()
    {
        return trayClickChoices(everyCapability());
    }

    [[nodiscard]] const TrayClickChoice *findChoice(const std::vector<TrayClickChoice> &choices, const std::string_view token)
    {
        const auto found = std::ranges::find(choices, token, &TrayClickChoice::token);
        return found == choices.end() ? nullptr : &*found;
    }

    [[nodiscard]] std::string textOf(const std::string_view token)
    {
        return std::string{token};
    }

    /**
     * @brief 开一份只属于这一格用例的配置
     * @param caseName 用例名，拼进临时文件名
     * @return 归调用方所有的配置存储
     * @details 每个用例各一份：`ctest -j` 把用例当独立进程跑，共用一份 ini 会互相写坏
     */
    [[nodiscard]] std::unique_ptr<SettingsStore> makeStore(const std::string &caseName)
    {
        const auto      ini = std::filesystem::temp_directory_path() / std::format("GyanisScribe-TrayClick-{}.ini", caseName);
        std::error_code ignored;
        std::filesystem::remove(ini, ignored);
        return std::make_unique<SettingsStore>(ini);
    }
} // namespace

/**
 * @brief 钉住两档的键互不相同，且默认那一串确实躺在可选清单里
 * @details 默认值不在清单里就是两张表分了家：设置页摆不出它，用户一拨就把它拨没了，
 *          而读端回落的又是一份界面上不存在的值。
 */
TEST(TrayClickTable, RolesCarryDistinctKeysAndDefaultsTheComboCanShow)
{
    const auto roles = trayClickRoles();
    ASSERT_EQ(roles.size(), 2U) << "两下鼠标各自一档：左键与中键";
    EXPECT_NE(roles[0].key, roles[1].key) << "两档共用一个键，设置页拨一处改两处";
    EXPECT_NE(roles[0].role, roles[1].role);

    const auto choices = fullChoices();
    for (const auto &role: roles)
    {
        EXPECT_TRUE(textOf(role.key).starts_with("Tray/")) << textOf(role.key);
        EXPECT_FALSE(role.label.empty()) << textOf(role.key);

        const auto *defaultChoice = findChoice(choices, role.defaultToken);
        ASSERT_NE(defaultChoice, nullptr) << "默认串在清单里找不到：" << textOf(role.defaultToken);
        EXPECT_EQ(defaultChoice->outcome, TrayClickOutcome::RunAction) << "默认那档要真做一件事，不是悄悄什么都不做";
        ASSERT_TRUE(defaultChoice->action.has_value()) << textOf(role.defaultToken);

        // 没填过时读出来的就是默认那一档，而且不能吭声：第一次用的人不该被一条「填坏了」的日志吓到
        const auto  settings = makeStore("roles-default");
        std::string complaint;
        const auto  decision = resolveTrayClick(*settings, role.role, complaint);
        EXPECT_TRUE(complaint.empty()) << textOf(role.key) << "：没填过不该报填坏";
        EXPECT_EQ(decision.token, role.defaultToken) << textOf(role.key);
        EXPECT_EQ(decision.action, defaultChoice->action) << textOf(role.key);
    }
}

/**
 * @brief 钉住哪一下算哪一档：只有左键与中键算，右键、双击与未知都不算
 * @details 右键那一下归平台自己弹菜单，双击在第一下松开时就报过单击——把它们算成一档会让
 *          一次动作发两遍。空返回也不是「什么都不做」：那几种压根不归这两档管。
 */
TEST(TrayClickTable, RoleMappingTakesOnlyTheLeftAndMiddleClicks)
{
    EXPECT_EQ(trayClickRoleForReason(QSystemTrayIcon::Trigger), TrayClickRole::Left);
    EXPECT_EQ(trayClickRoleForReason(QSystemTrayIcon::MiddleClick), TrayClickRole::Middle);

    EXPECT_FALSE(trayClickRoleForReason(QSystemTrayIcon::Context).has_value()) << "右键由平台自己弹菜单，不该再算一档";
    EXPECT_FALSE(trayClickRoleForReason(QSystemTrayIcon::DoubleClick).has_value()) << "双击在第一下松开时已经算过一次单击";
    EXPECT_FALSE(trayClickRoleForReason(QSystemTrayIcon::Unknown).has_value()) << "不是鼠标的一串不该落到某一档";
}

/**
 * @brief 钉住清单的头两格是两格哨兵，其后每个动作的标记与标签都取自托盘那张表
 * @details 退出不能出现在清单里：手一滑就把程序带走，是比没绑更糟的入口，与全局热键那张表同一判据。
 *          标签另抄一遍的话，菜单改了名字而这一格还留着旧的，用户在两处看到两个名字。
 */
TEST(TrayClickTable, ChoicesOpenWithSentinelsAndNeverOfferQuit)
{
    const auto choices = fullChoices();
    ASSERT_GE(choices.size(), 3U);

    EXPECT_EQ(textOf(choices[0].token), "none");
    EXPECT_EQ(choices[0].outcome, TrayClickOutcome::Nothing);
    EXPECT_FALSE(choices[0].action.has_value());
    EXPECT_EQ(textOf(choices[1].token), "menu");
    EXPECT_EQ(choices[1].outcome, TrayClickOutcome::ShowMenu);
    EXPECT_FALSE(choices[1].action.has_value());

    const auto entries = GyanisScribe::App::trayMenuEntries();
    for (std::size_t index = 2; index < choices.size(); ++index)
    {
        const auto &choice = choices[index];
        ASSERT_TRUE(choice.action.has_value()) << textOf(choice.token);
        EXPECT_NE(*choice.action, TrayAction::Quit) << "退出不能给绑";
        EXPECT_EQ(choice.outcome, TrayClickOutcome::RunAction);
        EXPECT_EQ(choice.token, trayActionToken(*choice.action)) << "标记与枚举名分了家";
        EXPECT_FALSE(choice.token.empty()) << "标记是空的：这一档在配置里没人读得回来，界面上却摆得出去";
        EXPECT_FALSE(choice.label.isEmpty()) << textOf(choice.token);

        const auto labelled = std::ranges::find_if(entries, [&choice](const auto &entry) { return entry.action == choice.action; });
        ASSERT_NE(labelled, entries.end()) << "清单里冒出一个菜单上没有的动作：" << textOf(choice.token);
        EXPECT_EQ(choice.label, labelled->label) << textOf(choice.token) << " 的标签与菜单上那一行不同解";
    }

    // 菜单上的动作逐个都该在清单里（除退出）：漏一个就是那一档在设置页上永远摆不出去
    std::vector<std::string_view> tokens;
    for (const auto &entry: entries)
    {
        if (!entry.action || *entry.action == TrayAction::Quit)
        {
            continue;
        }
        const auto token = trayActionToken(*entry.action);
        EXPECT_FALSE(token.empty()) << "词表里少了一支：这一档在设置页摆得出去、配置里却没人读得回来";
        EXPECT_NE(findChoice(choices, token), nullptr) << textOf(token);
        tokens.push_back(token);
    }
    EXPECT_EQ(tokens.size(), choices.size() - 2U) << "清单里的动作数与菜单上那些对不上";
    std::ranges::sort(tokens);
    EXPECT_EQ(std::ranges::adjacent_find(tokens), tokens.end()) << "两个动作共用一串标记，配置里就分不出谁是谁";
}

/**
 * @brief 钉住能力缺口的那两档不在这台机器的清单里
 * @details 「截取当前窗口」与「取消鼠标穿透」在本平台做不出来：给选了就是让用户配一条永远按不响的动作。
 *          读端仍认得那串标记，所以从别的机器抄来的配置不会被判成坏值，只是本机清单里不给摆。
 */
TEST(TrayClickTable, CapabilityFilterDropsWhatThisMachineCannotDo)
{
    auto limited            = everyCapability();
    limited.windowDiscovery = false;
    limited.clickThrough    = false;

    const auto both = trayClickChoices(limited);
    EXPECT_EQ(findChoice(both, trayActionToken(TrayAction::CaptureWindow)), nullptr) << "平台给不出前台窗口，这一档不该摆出去";
    EXPECT_EQ(findChoice(both, trayActionToken(TrayAction::ClearPinsThrough)), nullptr) << "没有穿透能力，这一档点了永远只回一句空话";

    const auto full = fullChoices();
    EXPECT_NE(findChoice(full, trayActionToken(TrayAction::CaptureWindow)), nullptr);
    EXPECT_NE(findChoice(full, trayActionToken(TrayAction::ClearPinsThrough)), nullptr);

    // 能力位只减不增：两档都缺时清单应正好短两项
    EXPECT_EQ(full.size(), both.size() + 2U);
}

/**
 * @brief 钉住填进去的标记读得回来，而空文本按「没填」走且不吭声
 * @details 空与坏是两件事：清空这一格等于没配过，落回默认且不吭声；填了一串认不出的要给出原因，
 *          否则用户只知道「我填的没生效」而不知道哪一段错了。
 */
TEST(TrayClickResolve, TokensRoundTripWhileAnEmptyValueStaysSilent)
{
    const auto settings = makeStore("tokens");

    struct Case
    {
        const char               *token;
        TrayClickOutcome          outcome;
        std::optional<TrayAction> action;
    };

    const std::vector<Case> cases{
            Case{"none", TrayClickOutcome::Nothing, std::nullopt},
            Case{"menu", TrayClickOutcome::ShowMenu, std::nullopt},
            Case{"Recognize", TrayClickOutcome::RunAction, TrayAction::Recognize},
            Case{"TogglePinsVisible", TrayClickOutcome::RunAction, TrayAction::TogglePinsVisible},
    };

    for (const auto &one: cases)
    {
        ASSERT_TRUE(settings->setValue(GyanisScribe::Settings::kTrayLeftClick, QString::fromUtf8(one.token)).has_value()) << one.token;
        std::string complaint;
        const auto  decision = resolveTrayClick(*settings, TrayClickRole::Left, complaint);
        EXPECT_EQ(decision.outcome, one.outcome) << one.token;
        EXPECT_EQ(decision.action, one.action) << one.token;
        EXPECT_TRUE(complaint.empty()) << one.token << " 是可用标记，不该报填坏";
    }

    // 清空这一格：按「没填」算，落回左键那一档的默认，且不当成填坏
    ASSERT_TRUE(settings->setValue(GyanisScribe::Settings::kTrayLeftClick, QString{}).has_value());
    std::string complaint;
    const auto  emptied = resolveTrayClick(*settings, TrayClickRole::Left, complaint);
    EXPECT_TRUE(complaint.empty()) << "清空按「没填」算，不该报填坏";
    EXPECT_EQ(emptied.token, trayClickRoleInfo(TrayClickRole::Left).defaultToken) << "没填过时该摆默认那一档";
}

/**
 * @brief 钉住填坏了落回各自那一档的默认并点名那串原文，而不是悄悄夹成一个近似的动作
 * @details 夹成「看着最像的那一档」会把用户填错的东西读成一个他根本没选过的动作；
 *          报出原因他才知道自己写的是哪一串。两档各回各的默认，不共用一个回落值。
 */
TEST(TrayClickResolve, BadTokenFallsBackToThatRolesDefaultAndNamesIt)
{
    const auto settings = makeStore("bad");
    ASSERT_TRUE(settings->setValue(GyanisScribe::Settings::kTrayLeftClick, QStringLiteral("Screenshot")).has_value());
    ASSERT_TRUE(settings->setValue(GyanisScribe::Settings::kTrayMiddleClick, QStringLiteral("  ")).has_value());

    std::string leftComplaint;
    const auto  left = resolveTrayClick(*settings, TrayClickRole::Left, leftComplaint);
    EXPECT_EQ(left.outcome, TrayClickOutcome::RunAction);
    EXPECT_EQ(left.action, TrayAction::CaptureRegion) << "坏值要落回左键那一档的默认，而不是别处";
    EXPECT_NE(leftComplaint.find("Screenshot"), std::string::npos) << "文案要点名那串坏值：" << leftComplaint;
    EXPECT_NE(leftComplaint.find("menu"), std::string::npos) << "也要给出可用的形状：" << leftComplaint;

    std::string middleComplaint;
    const auto  middle = resolveTrayClick(*settings, TrayClickRole::Middle, middleComplaint);
    EXPECT_EQ(middle.action, TrayAction::TogglePinsVisible) << "中键的默认与左键不同，回落要各回各的";
    EXPECT_TRUE(middleComplaint.empty()) << "只有空格的文本按「没填」算，不该报填坏";
}
