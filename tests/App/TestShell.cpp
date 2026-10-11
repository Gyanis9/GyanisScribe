#include "App/MainWindow.h"
#include "App/TrayController.h"

#include "Platform/PlatformCapabilities.h"
#include "Settings/SettingsKeys.h"
#include "Ui/IconProvider.h"

#include <gtest/gtest.h>

#include <QString>

#include <array>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using GyanisScribe::App::aboutText;
using GyanisScribe::App::indexOfMainWindowPage;
using GyanisScribe::App::indexOfThemeKind;
using GyanisScribe::App::MainWindowPage;
using GyanisScribe::App::pageChoices;
using GyanisScribe::App::themeChoices;
using GyanisScribe::App::themeKindAt;
using GyanisScribe::App::TrayAction;
using GyanisScribe::App::trayMenuEntries;
using GyanisScribe::App::TrayMenuEntry;
using GyanisScribe::App::visibleTrayMenuEntries;
using GyanisScribe::Platform::PlatformCapabilities;
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
 * @brief 钉住托盘菜单表：每一项动作各一次，分隔线不带动作
 * @details 菜单是用户找不到主窗口时唯一的入口，少一项或重一项都直接影响可用；
 *          图标名还要求真的在资源里，否则菜单上会出现一个空白项。
 */
TEST(TrayMenuTable, ListsEachActionOnceWithRealIcons)
{
    // 期望清单就是项数的真源：不加显式大小，漏一项或多一项都由这一处对出来
    constexpr std::array expectedActions{TrayAction::CaptureRegion,     TrayAction::CaptureFullScreen, TrayAction::CaptureLastRegion, TrayAction::CaptureWindow,
                                         TrayAction::ToggleRecord,      TrayAction::Recognize,         TrayAction::RecognizeFile,     TrayAction::PickColor,
                                         TrayAction::CloseAllPins,      TrayAction::OpenSettings,      TrayAction::OpenAbout,         TrayAction::Quit,
                                         TrayAction::CaptureLongRegion, TrayAction::ClearPinsThrough,  TrayAction::TogglePinsVisible, TrayAction::PasteClipboard,
                                         TrayAction::NextPinGroup};

    const auto actions = actionsInTable();
    EXPECT_EQ(actions.size(), expectedActions.size()) << "表里的动作项数与这份清单不同解";

    for (const auto action: expectedActions)
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

    // 「截取当前窗口」不许与「框选截图」共用同一枚图标：两行在菜单里长得一样，用户只能逐字读
    for (const auto &entry: trayMenuEntries())
    {
        if (entry.action && *entry.action == TrayAction::CaptureWindow)
        {
            EXPECT_EQ(std::string{entry.iconName}, "CaptureWindow");
        }
    }
}

/**
 * @brief 钉住「按能力过滤菜单」这一层：没有取前台窗口能力的平台不放那一项
 * @details 判据两个方向都给：能力在位时那一项必须出现（否则是白过滤），不在位时必须消失；
 *          还要求只有这一项受能力影响——别的能力位不该顺手把截图、录屏那些项一起摘掉。
 */
TEST(TrayMenuCapabilities, DropsWindowCaptureOnlyWhenThePlatformCannotDiscoverWindows)
{
    PlatformCapabilities withDiscovery;
    withDiscovery.windowDiscovery = true;
    withDiscovery.clickThrough    = true; // 齐平 = 两条按能力过滤的项都在，第 105 行那句才判得动
    PlatformCapabilities withoutDiscovery;

    const auto hasWindowItem = [](const auto &entries)
    { return std::ranges::any_of(entries, [](const TrayMenuEntry &entry) { return entry.action == TrayAction::CaptureWindow; }); };

    const auto granted = visibleTrayMenuEntries(withDiscovery);
    const auto denied  = visibleTrayMenuEntries(withoutDiscovery);

    EXPECT_TRUE(hasWindowItem(granted)) << "平台给得出前台窗口，菜单却没有那一项";
    EXPECT_FALSE(hasWindowItem(denied)) << "平台给不出前台窗口，菜单里却留着一条按了报错的项";
    EXPECT_EQ(granted.size(), trayMenuEntries().size()) << "能力齐平时过滤不该改动任何一项";
    // 全零的这一档少两项：按能力过滤的就是这两条（前台窗口、贴图出口），别的不许跟着掉
    EXPECT_EQ(denied.size(), granted.size() - 2) << "受能力影响的只有这两条，别的一项都不许跟着被摘";
}

/**
 * @brief 钉住「取消全部贴图穿透」这一条只在平台做得到穿透时才出现
 * @details 没这能力的平台上按它永远只会回一句「没有贴图开着鼠标穿透」，
 *          那是一条按下去不会有用的菜单项；两个方向都要判，白过滤与漏过滤是两种不同的坏法。
 */
TEST(TrayMenuCapabilities, DropsThroughEscapeOnlyWhenThePlatformCannotGoThrough)
{
    PlatformCapabilities withThrough;
    withThrough.clickThrough    = true;
    withThrough.windowDiscovery = true;
    PlatformCapabilities withoutThrough;
    withoutThrough.windowDiscovery = true;

    const auto hasThroughItem = [](const auto &entries)
    { return std::ranges::any_of(entries, [](const TrayMenuEntry &entry) { return entry.action == TrayAction::ClearPinsThrough; }); };

    const auto granted = visibleTrayMenuEntries(withThrough);
    const auto denied  = visibleTrayMenuEntries(withoutThrough);

    EXPECT_TRUE(hasThroughItem(granted)) << "平台做得到穿透，菜单里却没有这条出口";
    EXPECT_FALSE(hasThroughItem(denied)) << "平台做不到，留着它就是给用户一条按了没用的项";
    EXPECT_EQ(denied.size(), granted.size() - 1) << "过滤这条不该顺带摘掉别的项目";
}
/**
 * @brief 钉住托盘悬停提示里的贴图张数
 * @details 贴图窗口常被别的窗口压住，托盘是唯一还能看出「屏幕上挂着几张」的地方。
 *          PinManager::pinCountChanged 的通道存在由 Style.SourceConventions 管，
 *          这里管的是那句话算得对不对（含 0 与负数两格）。
 */
TEST(TrayTooltip, ShowsThePinCountOnlyWhenPinsExist)
{
    EXPECT_EQ(GyanisScribe::App::trayTooltip(0, -1, false), QStringLiteral("智录"));
    EXPECT_EQ(GyanisScribe::App::trayTooltip(-1, -1, false), QStringLiteral("智录")) << "负数不该写成「贴图 -1 张」";

    const auto three = GyanisScribe::App::trayTooltip(3, -1, false);
    EXPECT_NE(three.indexOf(QStringLiteral("智录")), -1) << three.toStdString();
    EXPECT_NE(three.indexOf(QStringLiteral("3")), -1) << three.toStdString();
    EXPECT_NE(three.indexOf(QStringLiteral("贴图")), -1) << three.toStdString();
}

/**
 * @brief 钉住托盘那一行在录屏时换成进度，并把暂停与在录分开
 * @details 控制条可以收进托盘，收起后这一行就是「还在录吗、录了多久」的唯一读数处：
 *          两档写成同一句，用户不知道帧还在不在走，停手的时机也就错了。
 */
TEST(TrayTooltip, ShowsRecordingProgressAndSeparatesPaused)
{
    const auto recording = GyanisScribe::App::trayTooltip(2, 75, false);
    EXPECT_NE(recording.indexOf(QStringLiteral("录制")), -1) << recording.toStdString();
    EXPECT_NE(recording.indexOf(QStringLiteral("01:15")), -1) << "时长要按时钟文本给：75 直接写出来看不出比 90 短";

    const auto paused = GyanisScribe::App::trayTooltip(2, 75, true);
    EXPECT_NE(paused.indexOf(QStringLiteral("暂停")), -1) << paused.toStdString();
    EXPECT_NE(recording, paused) << "暂停与在录在托盘上是同一句读数";

    EXPECT_EQ(GyanisScribe::App::trayTooltip(2, -1, false), GyanisScribe::App::trayTooltip(2, -5, false)) << "负数一律按「没在录」处理，不该随数值变";
    EXPECT_EQ(GyanisScribe::App::trayTooltip(0, -1, true), QStringLiteral("智录")) << "没在录时暂停位不该改读数";
}

/**
 * @brief 钉住「快捷键提示只挂在真实存在的配置键上」
 * @details 提示文字要从配置现读，挂在一个没人写的键上就会永远显示默认值，
 *          用户改了设置也看不到变化。
 */
TEST(TrayMenuTable, HotkeyHintsBindToRealSettingKeys)
{
    const std::vector<std::string_view> knownKeys = {
            GyanisScribe::Settings::kHotkeyCapture,     GyanisScribe::Settings::kHotkeyRecord,     GyanisScribe::Settings::kHotkeyOcr,         GyanisScribe::Settings::kHotkeyPick,
            GyanisScribe::Settings::kHotkeyCaptureLong, GyanisScribe::Settings::kHotkeyTogglePins, GyanisScribe::Settings::kHotkeyNextPinGroup};

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
    EXPECT_EQ(hinted, 7) << "全局热键有七项，托盘上的提示项也只能是这七项：多出来那条永远显示默认组合";
}

/**
 * @brief 钉住主题下拉的「序号—档位—标签」三者在同一张表上
 * @details 只测 themeKindAt 与 indexOfThemeKind 互指的话，把整张表重排也照样全绿，
 *          而用户看到的是标签：选「深色」却应用另一档就是这里漏出来的。
 *          期望值按下标逐个写死，不取自被测表。
 */
TEST(MainWindowMapping, ThemeChoicesAndIndicesPointAtEachOther)
{
    constexpr std::array<std::pair<ThemeKind, std::string_view>, 4> expectedChoices = {
            std::pair{ThemeKind::Light, std::string_view{"浅色"}},
            std::pair{ThemeKind::Dark, std::string_view{"深色"}},
            std::pair{ThemeKind::System, std::string_view{"跟随系统"}},
            std::pair{ThemeKind::HighContrast, std::string_view{"高对比度"}},
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

    // 序号 3 现在是高对比度那一行，越界要到 4 才算「不在表里」
    EXPECT_FALSE(themeKindAt(4).has_value());
    EXPECT_FALSE(themeKindAt(-1).has_value());
    EXPECT_EQ(indexOfThemeKind(static_cast<ThemeKind>(99)), -1) << "越界档位要给出 -1，让调用方跳过而不是写到 index 0";
}

/**
 * @brief 钉住主窗口的「序号—页—标签—图标」四者在同一张表上
 * @details 托盘菜单按页跳转、导航行按同一张表出题：分开放就会出现「菜单写着历史、点开是设置」。
 *          图标名也在这里核：导航行的图标来自表里那一列，拼错了界面上就是一个空位。
 *          期望值按下标逐个写死，不取自被测表。
 */
TEST(MainWindowMapping, PageChoicesBindIndexPageLabelAndIcon)
{
    constexpr std::array<std::tuple<MainWindowPage, std::string_view, std::string_view>, 10> expectedPages = {
            std::tuple{MainWindowPage::General, std::string_view{"通用"}, std::string_view{"Settings"}},
            std::tuple{MainWindowPage::Capture, std::string_view{"截图"}, std::string_view{"Capture"}},
            std::tuple{MainWindowPage::Annotation, std::string_view{"标注"}, std::string_view{"Annotation"}},
            std::tuple{MainWindowPage::Pin, std::string_view{"贴图"}, std::string_view{"Pin"}},
            std::tuple{MainWindowPage::Record, std::string_view{"录屏"}, std::string_view{"Record"}},
            std::tuple{MainWindowPage::Ocr, std::string_view{"文字识别"}, std::string_view{"Ocr"}},
            std::tuple{MainWindowPage::Hotkey, std::string_view{"快捷键"}, std::string_view{"Hotkey"}},
            std::tuple{MainWindowPage::Theme, std::string_view{"主题"}, std::string_view{"Theme"}},
            std::tuple{MainWindowPage::About, std::string_view{"关于"}, std::string_view{"About"}},
            std::tuple{MainWindowPage::History, std::string_view{"历史"}, std::string_view{"History"}},
    };

    const auto pages = pageChoices();
    ASSERT_EQ(pages.size(), expectedPages.size()) << "加一页要同时进这张表，导航行也从这里出题";
    const auto available = IconProvider::availableIconNames();
    for (std::size_t index = 0; index < expectedPages.size(); ++index)
    {
        EXPECT_EQ(pages[index].page, std::get<0>(expectedPages[index])) << index;
        EXPECT_EQ(pages[index].label, std::get<1>(expectedPages[index])) << index;
        EXPECT_EQ(indexOfMainWindowPage(std::get<0>(expectedPages[index])), static_cast<int>(index)) << index;
        EXPECT_TRUE(available.contains(QString::fromUtf8(std::string{pages[index].icon}.c_str()))) << "导航行点名了一枚资源里没有的图标：" << std::string{pages[index].icon};
    }

    EXPECT_EQ(indexOfMainWindowPage(static_cast<MainWindowPage>(42)), -1) << "没登记的页要给出 -1，让 open() 跳过而不是切到第 0 页";
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
