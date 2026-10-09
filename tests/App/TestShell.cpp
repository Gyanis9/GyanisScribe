#include "App/MainWindow.h"
#include "App/TrayController.h"

#include "Settings/SettingsKeys.h"
#include "Ui/IconProvider.h"

#include <gtest/gtest.h>

#include <QString>

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using GyanisScribe::App::aboutText;
using GyanisScribe::App::indexOfThemeKind;
using GyanisScribe::App::MainWindowPage;
using GyanisScribe::App::themeChoices;
using GyanisScribe::App::themeKindAt;
using GyanisScribe::App::TrayAction;
using GyanisScribe::App::trayMenuEntries;
using GyanisScribe::App::TrayMenuEntry;
using GyanisScribe::Ui::IconProvider;
using GyanisScribe::Ui::ThemeKind;

namespace
{
    /// 表里出现过的动作，用来查「同一个动作被列两次」
    std::vector<TrayAction> actionsInTable()
    {
        std::vector<TrayAction> actions;
        for (const auto &entry: trayMenuEntries())
        {
            if (entry.action)
            {
                actions.push_back(*entry.action);
            }
        }
        return actions;
    }

} // namespace

/**
 * @brief 钉住托盘菜单表：八项动作各一次，分隔线不带动作
 * @details 菜单是用户找不到主窗口时唯一的入口，少一项或重一项都直接影响可用；
 *          图标名还要求真的在资源里，否则菜单上会出现一个空白项。
 */
TEST(TrayMenuTable, ListsEachActionOnceWithRealIcons)
{
    const auto actions = actionsInTable();
    EXPECT_EQ(actions.size(), 8u) << "表里的动作项数与 TrayAction 的枚举数不同解";

    for (const auto action: {TrayAction::CaptureRegion, TrayAction::CaptureFullScreen, TrayAction::CaptureLastRegion, TrayAction::ToggleRecord, TrayAction::Recognize,
                             TrayAction::OpenSettings, TrayAction::OpenAbout, TrayAction::Quit})
    {
        const auto matches = std::ranges::count(actions, action);
        EXPECT_EQ(matches, 1) << static_cast<int>(action);
    }

    const auto available = IconProvider::availableIconNames();
    for (const auto &entry: trayMenuEntries())
    {
        if (!entry.action)
        {
            continue;
        }
        EXPECT_FALSE(entry.label.isEmpty()) << "有一项没有文字，读屏与鼠标都只能靠猜";
        EXPECT_TRUE(available.contains(QString::fromUtf8(std::string{entry.iconName}.c_str()))) << "菜单项点名了一枚资源里没有的图标：" << std::string{entry.iconName};
    }
}

/**
 * @brief 钉住「快捷键提示只挂在真实存在的配置键上」
 * @details 提示文字要从配置现读，挂在一个没人写的键上就会永远显示默认值，
 *          用户改了设置也看不到变化。
 */
TEST(TrayMenuTable, HotkeyHintsBindToRealSettingKeys)
{
    const std::vector<std::string_view> knownKeys = {GyanisScribe::Settings::kHotkeyCapture, GyanisScribe::Settings::kHotkeyRecord, GyanisScribe::Settings::kHotkeyOcr};

    int hinted = 0;
    for (const auto &entry: trayMenuEntries())
    {
        if (entry.hotkeyKey.empty())
        {
            continue;
        }
        ++hinted;
        EXPECT_TRUE(std::ranges::find(knownKeys, entry.hotkeyKey) != knownKeys.end()) << std::string{entry.hotkeyKey};
    }
    EXPECT_EQ(hinted, 3) << "三个全局热键之外不该再有别的提示项";
}

/**
 * @brief 钉住主题下拉的「序号—档位—标签」三者在同一张表上
 * @details 只测 themeKindAt 与 indexOfThemeKind 互指的话，把整张表重排也照样全绿，
 *          而用户看到的是标签：选「深色」却应用另一档就是这里漏出来的。
 *          期望值按下标逐个写死，不取自被测表。
 */
TEST(MainWindowMapping, ThemeChoicesAndIndicesPointAtEachOther)
{
    constexpr std::array<std::pair<ThemeKind, std::string_view>, 3> expectedChoices = {
            std::pair{ThemeKind::Light, std::string_view{"浅色"}},
            std::pair{ThemeKind::Dark, std::string_view{"深色"}},
            std::pair{ThemeKind::System, std::string_view{"跟随系统"}},
    };

    const auto choices = themeChoices();
    ASSERT_EQ(choices.size(), expectedChoices.size()) << "下拉框的行数与标签都要跟着表走，别在控件里另抄一份";
    for (std::size_t index = 0; index < expectedChoices.size(); ++index)
    {
        EXPECT_EQ(choices[index].kind, expectedChoices[index].first) << index;
        EXPECT_EQ(choices[index].label, expectedChoices[index].second) << index;
        EXPECT_EQ(themeKindAt(static_cast<int>(index)), expectedChoices[index].first) << index;
        EXPECT_EQ(indexOfThemeKind(expectedChoices[index].first), static_cast<int>(index)) << index;
    }

    EXPECT_FALSE(themeKindAt(3).has_value());
    EXPECT_FALSE(themeKindAt(-1).has_value());
    EXPECT_EQ(indexOfThemeKind(static_cast<ThemeKind>(99)), -1) << "越界档位要给出 -1，让调用方跳过而不是写到 index 0";
}

/**
 * @brief 钉住关于页的版本号只有一个来源
 * @details 正文里不许再写死一个版本号：真源改了这里就会对不上，而那正是用户会截图来问的东西。
 */
TEST(MainWindowMapping, AboutTextCarriesOnlyTheGivenVersion)
{
    const auto text = aboutText(QStringLiteral("9.9.9"));
    EXPECT_NE(text.indexOf(QStringLiteral("9.9.9")), -1);
    EXPECT_EQ(text.indexOf(QStringLiteral("0.0.1")), -1) << "正文里写死了第二个版本号";
    EXPECT_NE(text.indexOf(QStringLiteral("ThirdPartyLicenses")), -1) << "要指到第三方许可清单";
}
