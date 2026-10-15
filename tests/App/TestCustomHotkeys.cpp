// 可绑功能那批词表的直测：标识与配置键对得上、托盘标签只有一份出处、收集时按「填没填」收。
// 词表住在这里，热键层只认标识：判据钉的就是「加一个功能不用改两处」。
#include "App/CustomHotkeys.h"

#include "App/TrayController.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QString>
#include <QVariant>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

using GyanisScribe::App::bindableActionLabel;
using GyanisScribe::App::bindableActions;
using GyanisScribe::App::collectCustomBindings;
using GyanisScribe::App::customHotkeyKey;
using GyanisScribe::App::TrayAction;
using GyanisScribe::App::trayActionForToken;
using GyanisScribe::Core::SettingsStore;

namespace
{
    [[nodiscard]] QString tokenText(const std::string_view token)
    {
        return QString::fromUtf8(token.data(), static_cast<qsizetype>(token.size()));
    }
} // namespace

/**
 * @brief 钉住每个可绑功能都认得回自己，且标签取自托盘那张表
 */
TEST(CustomHotkeyTable, EveryBindableActionRoundTripsAndCarriesOneLabel)
{
    ASSERT_FALSE(bindableActions().empty());
    for (const auto &entry: bindableActions())
    {
        const auto token = tokenText(entry.token);
        EXPECT_EQ(trayActionForToken(token), entry.action) << token.toStdString();
        EXPECT_TRUE(customHotkeyKey(entry.token).startsWith(QStringLiteral("Hotkey/Custom/"))) << token.toStdString();
        EXPECT_FALSE(bindableActionLabel(entry.action).isEmpty()) << "托盘表上找不到这一项，界面上就会摆出一个空名字：" << token.toStdString();
    }
}

/**
 * @brief 钉住不该再绑的那些没进可绑表
 * @details 固定那八个各有自己的行与键，再列一遍会出现「同一件事两处都能改」；
 *          退出程序与打开设置/关于不给了绑：一条全局热键悄悄把程序带走，比没绑更糟。
 */
TEST(CustomHotkeyTable, FixedAndDangerousActionsStayUnbindable)
{
    const std::array forbidden{TrayAction::CaptureRegion,     TrayAction::ToggleRecord, TrayAction::Recognize, TrayAction::PickColor,    TrayAction::CaptureLongRegion,
                               TrayAction::TogglePinsVisible, TrayAction::NextPinGroup, TrayAction::Quit,      TrayAction::OpenSettings, TrayAction::OpenAbout};

    for (const auto action: forbidden)
    {
        const auto found = std::ranges::any_of(bindableActions(), [action](const auto &entry) { return entry.action == action; });
        EXPECT_FALSE(found) << static_cast<int>(action);
    }
}

/**
 * @brief 钉住收集只收「填了的那几格」，并把组合文本trim好交给登记
 * @details 空文本是「不绑」这一档而不是一个坏值：登记它会让 reload 报一条用户看不懂的失败。
 */
TEST(CustomHotkeyCollection, OnlyFilledKeysBecomeBindings)
{
    const auto      ini = std::filesystem::temp_directory_path() / "GyanisScribe-CustomHotkeys-collect.ini";
    std::error_code ignored;
    std::filesystem::remove(ini, ignored);
    SettingsStore settings(ini);

    ASSERT_TRUE(settings.setValue(customHotkeyKey("ManagePinGroups").toStdString(), QVariant(QStringLiteral("  Ctrl+Alt+M  "))).has_value());
    ASSERT_TRUE(settings.setValue(customHotkeyKey("BlankCanvas").toStdString(), QVariant(QStringLiteral("   "))).has_value());

    const auto bindings = collectCustomBindings(settings);
    ASSERT_EQ(bindings.size(), 1u) << "空格那一格被当成了绑定，或填了的那格被漏掉";
    EXPECT_EQ(bindings[0].token, QStringLiteral("ManagePinGroups"));
    EXPECT_EQ(bindings[0].combination, QStringLiteral("Ctrl+Alt+M")) << "首尾空白没剪：登记那一层不认这种文本";

    settings.sync();
    std::filesystem::remove(ini, ignored);
}

/**
 * @brief 钉住一个都不填时交回空表：登记那一步就该原样跳过
 */
TEST(CustomHotkeyCollection, NothingFilledYieldsNoBindings)
{
    const auto      ini = std::filesystem::temp_directory_path() / "GyanisScribe-CustomHotkeys-empty.ini";
    std::error_code ignored;
    std::filesystem::remove(ini, ignored);
    SettingsStore settings(ini);

    EXPECT_TRUE(collectCustomBindings(settings).empty());
    std::filesystem::remove(ini, ignored);
}

/**
 * @brief 钉住认不出的标识一律回空，而不是挑一个近似的
 * @details 配置里可能留着旧版本的功能名；替用户挑一个是把按键派给了另一个功能。
 */
TEST(CustomHotkeyTable, UnknownTokensResolveToNothing)
{
    EXPECT_FALSE(trayActionForToken(QStringLiteral("CaptureEverything")).has_value());
    EXPECT_FALSE(trayActionForToken(QString{}).has_value());
}
