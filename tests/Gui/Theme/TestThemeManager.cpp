#include "Gui/Theme/CornerStyle.h"
#include "Gui/Theme/DesignTokens.h"
#include "Gui/Theme/ThemeKind.h"
#include "Gui/Theme/ThemeManager.h"
#include "Gui/Controls/Button.h"
#include "Gui/Controls/CheckBox.h"
#include "Gui/Controls/ComboBox.h"
#include "Gui/Controls/LineEdit.h"
#include "Gui/Controls/Slider.h"
#include "Gui/Controls/SpinBox.h"
#include "Gui/Controls/SurfaceWidget.h"
#include "Gui/Theme/ControlPalette.h"
#include "Core/Geometry/ContinuousCorner.h"

#include "Base/Logger.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QFont>
#include <QImage>
#include <string_view>
#include <tuple>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <set>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>

#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>
#include <QCheckBox>
#include <string>

using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Base::Logger;
using GyanisScribe::Core::kThemeMode;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Gui::applyTextRole;
using GyanisScribe::Gui::paletteFor;
using GyanisScribe::Gui::parseThemeKind;
using GyanisScribe::Gui::resolveThemeKind;
using GyanisScribe::Gui::setVariableMetricFamilyAvailableForTesting;
using GyanisScribe::Gui::SystemScheme;
using GyanisScribe::Gui::TextRole;
using GyanisScribe::Gui::TextScript;
using GyanisScribe::Gui::ThemeKind;
using GyanisScribe::Gui::themeKindText;
using GyanisScribe::Gui::ThemeManager;

namespace
{
    std::unique_ptr<QApplication> application;

    /// 每个用例带自己的 INI 路径：QSettings 后端是进程外的共享位置，共用键会让并行用例互相踩
    std::filesystem::path probeIni(std::string_view name)
    {
        // ctest 是一条用例一个进程，进程内再取线程 id 没有额外信息量，
        // 而 libstdc++ 13 不给 std::thread::id 提供 formatter（MSVC 给），跨编译器一侧会编不过
        return std::filesystem::temp_directory_path() / std::format("GyanisScribe-Theme-{}.ini", name);
    }

    /// sRGB 相对亮度，按 WCAG 的定义手写，不调库也不从被测代码借公式
    double relativeLuminance(const QColor &color)
    {
        const auto channel = [](int value)
        {
            const auto scaled = value / 255.0;
            return scaled <= 0.03928 ? scaled / 12.92 : std::pow((scaled + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) + 0.0722 * channel(color.blue());
    }

    double contrastRatio(const QColor &foreground, const QColor &background)
    {
        const auto first  = relativeLuminance(foreground);
        const auto second = relativeLuminance(background);
        return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
    }

    /// 从资源里读出样式表原文，用例据此与色板逐色比对
    QString readStyleSheet(std::string_view resource)
    {
        QFile file(QString::fromUtf8(resource.data(), static_cast<qsizetype>(resource.size())));
        if (!file.open(QIODevice::ReadOnly))
        {
            return {};
        }
        return QString::fromUtf8(file.readAll());
    }

    /**
     * @brief 抽出样式表里所有带 :focus 的选择器，逐个逗号分支拆开
     * @param sheet 样式表原文
     * @return 已简化的选择器文本；每条至少含一个 :focus
     * @details 直接搜全文会把 `QWidget#AnnotationToolbar QPushButton:focus` 里的
     *          `QPushButton:focus` 也算成控件级焦点，判据必须按选择器分支看前缀。
     */
    QStringList focusSelectorsIn(const QString &sheet)
    {
        QStringList             selectors;
        const QRegularExpression rule{QStringLiteral(R"(([^{}]*:focus[^{}]*)\{)")};
        for (auto match = rule.match(sheet); match.hasMatch(); match = rule.match(sheet, match.capturedEnd()))
        {
            for (const auto &branch: match.captured(1).split(QLatin1Char(',')))
            {
                const auto selector = branch.simplified();
                if (selector.contains(QStringLiteral(":focus")))
                {
                    selectors.append(selector);
                }
            }
        }
        return selectors;
    }

    /// 读一份仓库内的源文件：源目录由 CMake 的 GYANISSCRIBE_SOURCE_DIR 指过来，不赌 ctest 的工作目录
    std::string readSourceFile(const std::string_view relativePath)
    {
        std::ifstream input{std::filesystem::path{std::string{GYANISSCRIBE_SOURCE_DIR}} / std::string{relativePath}, std::ios::binary};
        if (!input)
        {
            return {};
        }
        return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    /// 从函数起点找到与第一个左花括号配对的右花括号：用于断言某次调用落在绘制函数体内
    std::size_t enclosingFunctionEnd(const std::string &source, const std::size_t functionStart)
    {
        const auto open = source.find('{', functionStart);
        if (open == std::string::npos)
        {
            return std::string::npos;
        }
        int depth = 0;
        for (auto index = open; index < source.size(); ++index)
        {
            if (source[index] == '{')
            {
                ++depth;
            } else if (source[index] == '}')
            {
                --depth;
                if (depth == 0)
                {
                    return index;
                }
            }
        }
        return std::string::npos;
    }

    /**
     * @brief 主题层夹具：主题管理器接真实的 QSettings 后端（临时 INI）
     * @details 用 SettingsStore 而不是替身，是为了把「设置页写一个键 → 主题自己跟上」这条接线
     *          也一起验掉；替身只会验到我自己的调用点。
     */
    class ThemeManagerFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestThemeManager";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
            QCoreApplication::setOrganizationName(QStringLiteral("Gyanis"));
            QCoreApplication::setApplicationName(QStringLiteral("GyanisScribe-Tests"));
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            m_iniPath = probeIni(testInfo());
            std::error_code cleanupError;
            std::filesystem::remove(m_iniPath, cleanupError);

            m_store   = std::make_unique<SettingsStore>(m_iniPath);
            m_manager = std::make_unique<ThemeManager>(*m_store);
            m_changes = 0;

            QObject::connect(m_manager.get(), &ThemeManager::themeChanged, [this] { ++m_changes; });
        }

        void TearDown() override
        {
            m_manager.reset();
            m_store.reset();
            std::error_code cleanupError;
            std::filesystem::remove(m_iniPath, cleanupError);
            // 样式表与调色板都是进程级的，用例之间要归零，否则下一条用例拿到上一条的残留
            application->setStyleSheet(QString{});
            application->setPalette(QPalette{});
        }

        [[nodiscard]] std::string testInfo() const
        {
            const auto *info = ::testing::UnitTest::GetInstance()->current_test_info();
            return info == nullptr ? "Unknown" : std::string{info->name()};
        }

        ThemeManager &manager() const
        {
            return *m_manager;
        }
        GyanisScribe::Core::ISettingsStore &store() const
        {
            return *m_store;
        }

        /// 主题色板的十六进制写法，样式表里用的就是同样式
        [[nodiscard]] static QString hexOf(const QColor &color)
        {
            return QColor{color}.name(QColor::HexRgb).toUpper();
        }

        int m_changes; ///< themeChanged 的计数，用例据此判「只在真变了时才发」

    private:
        std::filesystem::path                               m_iniPath; ///< 本用例专属的临时 INI
        std::unique_ptr<GyanisScribe::Core::ISettingsStore> m_store;   ///< 配置后端
        std::unique_ptr<ThemeManager>                       m_manager; ///< 被测管理器
    };

} // namespace

/**
 * @brief 钉住档位与系统偏好的解算表
 * @details 解算是纯函数，四种组合逐一钉住：用户明确选了某一档时系统回报不得覆盖，
 *          报不出偏好时兜底浅色（深色文字压白底是读不出的那种错）。
 */
TEST(ThemeKindResolution, ResolvesSystemAgainstEverySchemeAndKeepsExplicitChoice)
{
    EXPECT_EQ(resolveThemeKind(ThemeKind::System, SystemScheme::Light), ThemeKind::Light);
    EXPECT_EQ(resolveThemeKind(ThemeKind::System, SystemScheme::Dark), ThemeKind::Dark);
    EXPECT_EQ(resolveThemeKind(ThemeKind::System, SystemScheme::Unknown), ThemeKind::Light);
    EXPECT_EQ(resolveThemeKind(ThemeKind::Light, SystemScheme::Dark), ThemeKind::Light);
    EXPECT_EQ(resolveThemeKind(ThemeKind::Dark, SystemScheme::Light), ThemeKind::Dark);
    // 高对比度是用户明确选的档，系统报深色也不能把它换掉
    EXPECT_EQ(resolveThemeKind(ThemeKind::HighContrast, SystemScheme::Dark), ThemeKind::HighContrast);
    EXPECT_EQ(resolveThemeKind(ThemeKind::HighContrast, SystemScheme::Unknown), ThemeKind::HighContrast);
}

/**
 * @brief 钉住配置文本与档位互指，以及拒绝面
 * @details 三个可用取值各解析一次并回写比对；不合法的文本必须报 InvalidArgument，
 *          且文案里给出可用取值——只报「解析失败」用户不知道该填什么。
 */
TEST(ThemeKindResolution, TextAndKindPointAtEachOtherAndRejectOthers)
{
    for (const auto kind: {ThemeKind::Light, ThemeKind::Dark, ThemeKind::System, ThemeKind::HighContrast})
    {
        const auto text   = themeKindText(kind);
        const auto parsed = parseThemeKind(text);
        ASSERT_TRUE(parsed.has_value()) << std::string{text};
        EXPECT_EQ(*parsed, kind) << text;
    }

    // 独立真源：配置里写的词逐条手抄，不拿 themeKindText 的输出反推——整张词表换掉也照样绿的那种断言抓不到东西
    EXPECT_EQ(themeKindText(ThemeKind::HighContrast), "HighContrast");
    EXPECT_EQ(parseThemeKind("HighContrast").value(), ThemeKind::HighContrast);

    // 大小写不同的写法要拒：配置文件里 dark 与 Dark 只差一个字母，静默接受就会让「改了什么」无据可查
    for (const auto *wrong: {"dark", "SYSTEM", "NavyBlue", "highcontrast", "HighContrast ", ""})
    {
        const auto rejected = parseThemeKind(wrong);
        ASSERT_FALSE(rejected.has_value()) << wrong;
        EXPECT_EQ(rejected.error().code, ErrorCode::InvalidArgument) << wrong;
        EXPECT_NE(rejected.error().message.find("Light"), std::string::npos) << wrong;
        EXPECT_NE(rejected.error().message.find("System"), std::string::npos) << wrong;
        // 新档位在可用取值里也要出现，否则照着文案手填的人永远填不回来
        EXPECT_NE(rejected.error().message.find("HighContrast"), std::string::npos) << wrong;
    }
}

/**
 * @brief 钉住「第一次生效会落样式表并通知一次」
 * @details 起点通知是界面重取色的唯一时机；样式表内容按当前系统偏好解算，
 *          所以断言的是「用的就是解算出来那一套的底色」，不赌本机是深色还是浅色。
 */
TEST_F(ThemeManagerFixture, FirstApplyLandsTheResolvedStyleSheetAndAnnouncesOnce)
{
    manager().applyFromSettings();

    EXPECT_EQ(m_changes, 1) << "第一次生效没通知，界面就停在默认色上，样式表落了也没人重取色";
    const auto resolved = manager().resolvedKind();
    ASSERT_TRUE(resolved == ThemeKind::Light || resolved == ThemeKind::Dark) << "解算结果只能是浅色或深色";
    EXPECT_EQ(manager().requestedKind(), ThemeKind::System) << "没写过配置时缺省档应是跟随系统";

    const auto expectedBackground = hexOf(paletteFor(resolved).windowBackground);
    EXPECT_NE(application->styleSheet().toUpper().indexOf(expectedBackground), -1)
            << "样式表里没有 " << expectedBackground.toStdString() << " 这套底色，说明落的不是解算出来那一套";
}

/**
 * @brief 钉住切换不需要重启：样式表与图标描边色一起跟着换
 * @details 两个方向都走一遍，并断言换过去之后另一套的底色不在场——
 *          只断「新的到了」会把「旧的没冲掉」放过去。
 */
TEST_F(ThemeManagerFixture, SwitchingThemesReplacesStyleSheetAndIconTint)
{
    ASSERT_TRUE(manager().setTheme(ThemeKind::Dark).has_value());
    EXPECT_EQ(manager().resolvedKind(), ThemeKind::Dark);
    const auto darkSheet = application->styleSheet().toUpper();
    EXPECT_NE(darkSheet.indexOf(hexOf(GyanisScribe::Gui::kDarkPalette.windowBackground)), -1);
    EXPECT_EQ(darkSheet.indexOf(hexOf(GyanisScribe::Gui::kLightPalette.windowBackground)), -1) << "深色样式表里还留着浅色的底色，两套规则没被整片换掉";
    EXPECT_EQ(manager().iconTint(), GyanisScribe::Gui::kDarkPalette.icon);

    ASSERT_TRUE(manager().setTheme(ThemeKind::Light).has_value());
    EXPECT_EQ(manager().resolvedKind(), ThemeKind::Light);
    const auto lightSheet = application->styleSheet().toUpper();
    EXPECT_NE(lightSheet.indexOf(hexOf(GyanisScribe::Gui::kLightPalette.windowBackground)), -1);
    EXPECT_EQ(lightSheet.indexOf(hexOf(GyanisScribe::Gui::kDarkPalette.windowBackground)), -1);
    EXPECT_EQ(manager().iconTint(), GyanisScribe::Gui::kLightPalette.icon);

    EXPECT_EQ(m_changes, 2) << "两次真变化应当通知两次，起点由上一条用例单独钉";
    EXPECT_EQ(store().stringValue(kThemeMode, QStringLiteral("空")), QStringLiteral("Light")) << "档位没写回配置：重启后界面会回到用户没选的那一档";
}

/**
 * @brief 钉住「只有真变了才通知」
 * @details 样式表整片重设会让所有控件重排重绘，同一个档位重复设置若还发通知，
 *          界面就会被无意义的重绘拖着走。
 */
TEST_F(ThemeManagerFixture, ReapplyingTheSameThemeDoesNotAnnounceAgain)
{
    ASSERT_TRUE(manager().setTheme(ThemeKind::Light).has_value());
    const auto changesAfterFirst = m_changes;
    ASSERT_GT(changesAfterFirst, 0) << "第一次生效就该通知";

    ASSERT_TRUE(manager().setTheme(ThemeKind::Light).has_value());
    manager().applyFromSettings();
    EXPECT_EQ(m_changes, changesAfterFirst) << "重复落同一个档位又通知了一次，界面会跟着做无意义的整体重绘";
}

/**
 * @brief 钉住「写配置这个动作本身就让主题生效」
 * @details 设置页只会写键并 emit，不会反过来调主题层；这条按真实通知路径走，
 *          证明订阅接线在位——只调 applyFromSettings() 的用例验不到这一环。
 */
TEST_F(ThemeManagerFixture, SettingWriteDrivesThemeWithoutCallingApply)
{
    ASSERT_TRUE(store().setValue(kThemeMode, QStringLiteral("Dark")).has_value());

    EXPECT_EQ(manager().requestedKind(), ThemeKind::Dark) << "配置写了 Dark 而主题没跟上：valueChanged 的订阅没接上或键名不同解";
    EXPECT_EQ(manager().resolvedKind(), ThemeKind::Dark);
    EXPECT_NE(application->styleSheet().toUpper().indexOf(hexOf(GyanisScribe::Gui::kDarkPalette.windowBackground)), -1);

    // 别的键改动不该惊动主题层
    ASSERT_TRUE(store().setValue("Hotkey/Capture", QStringLiteral("Ctrl+Alt+B")).has_value());
    EXPECT_EQ(manager().requestedKind(), ThemeKind::Dark) << "无关配置项把主题档位带跑了";
}

/**
 * @brief 钉住坏取值按 System 兜底并且出声
 * @details 静默回落是另一种「看起来正常」：日志里必须写明是哪个键、落到了哪一档。
 */
TEST_F(ThemeManagerFixture, GarbageStoredValueFallsBackToSystemAndLogsWarning)
{
    const auto      logDirectory = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Theme-Log-{}", testInfo());
    std::error_code cleanupError;
    std::filesystem::remove_all(logDirectory, cleanupError);
    ASSERT_TRUE(Logger::initialize(logDirectory).has_value());

    ASSERT_TRUE(store().setValue(kThemeMode, QStringLiteral("NavyBlue")).has_value());
    manager().applyFromSettings();

    EXPECT_EQ(manager().requestedKind(), ThemeKind::System) << "坏值没能回落成跟随系统";
    EXPECT_TRUE(manager().resolvedKind() == ThemeKind::Light || manager().resolvedKind() == ThemeKind::Dark);

    Logger::shutdown();
    std::ifstream     logFile(logDirectory / "GyanisScribe.log", std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>{logFile}, std::istreambuf_iterator<char>{}};
    EXPECT_NE(contents.find("按 System 处理"), std::string::npos) << "回落没有日志，前 200 字：" << contents.substr(0, 200);
    EXPECT_NE(contents.find("Theme/Mode"), std::string::npos) << "日志没写是哪个键";

    std::filesystem::remove_all(logDirectory, cleanupError);
}

/**
 * @brief 钉住样式表与色板同解，双向都查
 * @details 正向逐主题：QSS 里出现的每个十六进制色都必须是该主题色板里的一个（禁「代码外再有一套色」）。
 *          反向全局：每个角色至少要有一个真实消费方，QSS 或 C++ 绘制代码都算。输入类、按钮、
 *          勾选框与浮层迁移到自绘之后，surface、controlFill、hover、pressed、mutedFill、accent、
 *          accentHover、borderStrong 不再进样式表，由 ControlPalette 的按钮/输入取色、
 *          Gui::SurfaceWidget / Gui::Menu / 提示代理 / Gui::ScrollBar / Toast 等 C++ 绘制代码取用；
 *          这些角色在下面逐个断言真实取色函数返回它自己，避免「名单里有、没人用」的死配置。
 *          icon、success、danger 只给绘制代码用（图标描边、Toast 语义色），出现在 QSS 里反而是问题，
 *          消费方另由 TestIconProvider 与 TestToast 钉住。色板是三档共用的一张角色表，判据是角色
 *          在整个样式层有没有消费方；逐主题会把判据改成「每份 QSS 都得用全自己色板的十五个角色」，
 *          某档刻意少用一个角色就报红，而那不是死配置。
 */
TEST_F(ThemeManagerFixture, StyleSheetsAndPalettesAgreeInBothDirections)
{
    std::set<QString> usedColors;
    for (const auto [kind, resource]: {std::pair{ThemeKind::Light, std::string_view{":/styles/Light.qss"}}, std::pair{ThemeKind::Dark, std::string_view{":/styles/Dark.qss"}},
                                       std::pair{ThemeKind::HighContrast, std::string_view{":/styles/HighContrast.qss"}}})
    {
        const auto sheet   = readStyleSheet(resource).toUpper();
        const auto palette = paletteFor(kind);
        ASSERT_FALSE(sheet.isEmpty()) << resource;

        // 只列该进样式表的十五个角色：icon/success/danger 是给绘制代码用的（图标描边、Toast 语义色），
        // 出现在 QSS 里反而是问题，下面单独反着断言
        const std::array paletteColors = {palette.windowBackground, palette.surface,    palette.text,        palette.secondaryText, palette.border, palette.accent,
                                          palette.onAccent,         palette.selection,  palette.mutedFill,   palette.controlFill,   palette.hover,  palette.pressed,
                                          palette.accentHover,      palette.accentSoft, palette.borderStrong};

        // 正向：QSS 里不许出现色板之外的色值；命中的色值进入全局使用集合
        QRegularExpression hexPattern{QStringLiteral("#[0-9A-Fa-f]{6}")};
        for (auto match = hexPattern.match(sheet); match.hasMatch(); match = hexPattern.match(sheet, match.capturedEnd()))
        {
            const auto found = std::ranges::any_of(paletteColors, [&match](const QColor &color) { return hexOf(color) == match.captured(0).toUpper(); });
            EXPECT_TRUE(found) << resource << " 里的 " << match.captured(0).toStdString() << " 不在该主题色板里";
            if (found)
            {
                usedColors.insert(match.captured(0).toUpper());
            }
        }

        // 绘制专用的三个色反过来断言不在 QSS 里：图标描边只走 IconProvider，
        // Toast 的语义色只走 toastColorFor——出现在样式表里就是两套上色路径打架。
        // 例外：与某个 QSS 角色同值的不能算——高对比度档的 icon 就是纯黑，与 text/border 同值，
        // 那里"出现在样式表里"的是文字与边框，不是图标在两套路径上各上一次色
        for (const auto &codeOnlyColor: {palette.icon, palette.success, palette.danger})
        {
            if (std::ranges::any_of(paletteColors, [&codeOnlyColor](const QColor &color) { return color == codeOnlyColor; }))
            {
                continue;
            }
            EXPECT_EQ(sheet.indexOf(hexOf(codeOnlyColor)), -1) << resource << " 里出现了只该由绘制代码用的语义色 " << hexOf(codeOnlyColor).toStdString();
        }
    }

    // 反向全局（QSS 侧）：没有自绘消费方的角色必须至少被一份样式表用着，从样式表里消失就是断色。
    // 判据先跑 QSS 这一侧，再把 C++ 消费方名单并进集合——顺序反过来，accentSoft 这类两边都算的角色
    // 会让「QSS 里没了」被名单掩盖。
    for (const auto kind: {ThemeKind::Light, ThemeKind::Dark, ThemeKind::HighContrast})
    {
        const auto &palette = paletteFor(kind);
        for (const auto &color: {palette.windowBackground, palette.text, palette.secondaryText, palette.border, palette.onAccent, palette.selection, palette.accentSoft})
        {
            EXPECT_TRUE(usedColors.contains(hexOf(color))) << "色板里的 " << hexOf(color).toStdString() << " 一份 QSS 都没用过";
        }
    }

    // 反向全局（C++ 侧）：自绘迁移之后 surface、controlFill、hover、pressed、mutedFill、accent、
    // accentHover、accentSoft、borderStrong 不再进 QSS，由 ControlPalette 的按钮/输入取色、
    // Gui::SurfaceWidget / Gui::Menu / 提示代理 / Gui::ScrollBar / Toast 等绘制代码取用。
    // 这里逐角色断言真实取色函数返回它自己；把名单直接并进 usedColors 会让 accentSoft 这类
    // 两边都算的角色掩盖掉「QSS 里已经没了」，所以 QSS 与 C++ 两侧分开判。
    for (const auto kind: {ThemeKind::Light, ThemeKind::Dark, ThemeKind::HighContrast})
    {
        const auto &palette = paletteFor(kind);
        EXPECT_EQ(GyanisScribe::Gui::buildApplicationPalette(palette).color(QPalette::Base), palette.surface) << "surface 没有原生绘制消费方：QPalette::Base 没跟着色板走";
        EXPECT_EQ(GyanisScribe::Gui::buttonColors(palette, kind, false, true, false, false, false, false).fill, palette.controlFill) << "普通按钮的静息填充没取 controlFill 角色";
        EXPECT_EQ(GyanisScribe::Gui::buttonColors(palette, kind, false, true, true, false, false, false).fill, palette.hover) << "悬停的按钮填充没取 hover 角色";
        EXPECT_EQ(GyanisScribe::Gui::buttonColors(palette, kind, false, true, false, true, false, false).fill, palette.pressed) << "按下态的按钮填充没取 pressed 角色";
        EXPECT_EQ(GyanisScribe::Gui::buttonColors(palette, kind, false, false, false, false, false, false).fill, palette.mutedFill) << "禁用按钮的填充没取 mutedFill 角色";
        EXPECT_EQ(GyanisScribe::Gui::buttonColors(palette, kind, true, true, false, false, false, false).fill, palette.accent) << "主按钮的静息填充没取 accent 角色";
        EXPECT_EQ(GyanisScribe::Gui::buttonColors(palette, kind, true, true, true, false, false, false).fill, palette.accentHover) << "主按钮悬停底没取 accentHover 角色";
        EXPECT_EQ(GyanisScribe::Gui::toolButtonColors(palette, kind, true, true, false, false, false, false).fill, palette.accentSoft) << "选中的工具按钮没取 accentSoft 角色";
        EXPECT_EQ(GyanisScribe::Gui::toolButtonColors(palette, kind, false, true, false, true, false, false).fill, palette.pressed) << "按下态的工具按钮填充没取 pressed 角色";
        const auto restInput = GyanisScribe::Gui::inputColors(palette, kind, true, false, false, false);
        EXPECT_EQ(restInput.fill, palette.controlFill) << "启用输入框的静息底没取 controlFill 角色";
        EXPECT_EQ(GyanisScribe::Gui::inputColors(palette, kind, false, false, false, false).fill, palette.mutedFill) << "禁用输入的静默底没取 mutedFill 角色";
        EXPECT_EQ(GyanisScribe::Gui::inputColors(palette, kind, true, true, false, false).fill, palette.mutedFill) << "只读输入的静默底没取 mutedFill 角色";
        EXPECT_EQ(GyanisScribe::Gui::inputColors(palette, kind, true, false, true, true).border, palette.accent) << "标注工具栏内输入框的焦点边没取 accent 角色";
        EXPECT_EQ(restInput.border, palette.borderStrong) << "输入框静息描边没取 borderStrong 角色";
        EXPECT_EQ(restInput.borderWidth, kind == ThemeKind::HighContrast ? 2 : 1) << "输入框静息描边的宽度不对";
        // 只断「有边」还不够：描边若与卡片底同色，等于没有。浅深档的 borderStrong 与 surface 至少差 16 个色阶
        const auto channelGap = [](const QColor &left, const QColor &right)
        { return std::max({std::abs(left.red() - right.red()), std::abs(left.green() - right.green()), std::abs(left.blue() - right.blue())}); };
        EXPECT_GE(channelGap(restInput.border, palette.surface), 16) << "输入框静息描边与卡片底的色差太小，输入框会融进背景";
    }
}

/**
 * @brief 钉住浅色与深色样式表里没有不透明纯黑
 * @details 微明度分层要求最重的一档也是带色相的中性色；纯黑只属于高对比度档，
 *          在浅深两档里出现就会把微明度阶梯压断。
 */
TEST_F(ThemeManagerFixture, LightAndDarkSheetsUseNoOpaqueBlack)
{
    for (const auto resource: {std::string_view{":/styles/Light.qss"}, std::string_view{":/styles/Dark.qss"}})
    {
        const auto sheet = readStyleSheet(resource).toUpper();
        EXPECT_EQ(sheet.indexOf(QStringLiteral("#000000")), -1) << resource << " 出现了不透明纯黑";
    }
}

/**
 * @brief 钉住两套主题的对比度满足 WCAG AA
 * @details 正文与次要文本按 4.5:1，主色描边与图标按非文本要素 3:1；判据写在测试里而不是「看着还行」。
 */
TEST_F(ThemeManagerFixture, SemanticColorPairsMeetWcagContrast)
{
    for (const auto kind: {ThemeKind::Light, ThemeKind::Dark, ThemeKind::HighContrast})
    {
        const auto palette = paletteFor(kind);
        const auto label   = std::string{themeKindText(kind)};

        for (const auto &background: {palette.windowBackground, palette.surface})
        {
            EXPECT_GE(contrastRatio(palette.text, background), 4.5) << label << " 正文压在这套底色上不足 AA";
            // 次要文本要过 7:1（AAA），不是 4.5：说明行与占位文字与正文同字号，弱化只能靠色，
            // 压到 AA 那一档在浅色档实测就是「要看一眼才读得出」
            EXPECT_GE(contrastRatio(palette.secondaryText, background), 7.0) << label << " 次要文本压在这套底色上不足 AAA";
            EXPECT_GE(contrastRatio(palette.accent, background), 3.0) << label << " 焦点圈/主按钮描边在这套底色上不足 3:1";
            EXPECT_GE(contrastRatio(palette.icon, background), 3.0) << label << " 图标在这套底色上不足 3:1";
        }

        EXPECT_GE(contrastRatio(palette.text, palette.selection), 4.5) << label << " 选区上的正文读不出";
        EXPECT_GE(contrastRatio(palette.onAccent, palette.accent), 4.5) << label << " 主按钮上的文字读不出";

        // 状态色不是「看着差不多」：悬停与按下这两档底上，次要文本仍要读得出，否则禁用/悬停二选一
        EXPECT_GE(contrastRatio(palette.secondaryText, palette.hover), 4.5) << label << " 悬停底上的次要文本不足 AA";
        EXPECT_GE(contrastRatio(palette.secondaryText, palette.pressed), 4.5) << label << " 按下底上的次要文本不足 AA";
        EXPECT_GE(contrastRatio(palette.secondaryText, palette.mutedFill), 4.5) << label << " 静默底上的次要文本不足 AA";
        EXPECT_GE(contrastRatio(palette.secondaryText, palette.controlFill), 4.5) << label << " 静息控件底上的次要文本不足 AA";
        // 导航当前项与勾选的工具按钮压的是 accentSoft：正文与主色描边都要在这一档底上站得住
        EXPECT_GE(contrastRatio(palette.text, palette.accentSoft), 4.5) << label << " 选中底上的正文读不出";
        EXPECT_GE(contrastRatio(palette.accent, palette.accentSoft), 3.0) << label << " 选中项的描边与它自己的底糊成一片";
        EXPECT_GE(contrastRatio(palette.onAccent, palette.accentHover), 4.5) << label << " 主按钮悬停时文字读不出";
    }

    // 高对比度档另加一档判据：正文与次要文本要满足 AAA 的 7:1——这一档存在的理由就是「AA 还不够」，
    // 只按 AA 复核的话，做一版与浅色档同样对比度的「高对比度」也能全绿
    const auto highContrast = paletteFor(ThemeKind::HighContrast);
    EXPECT_GE(contrastRatio(highContrast.text, highContrast.windowBackground), 7.0) << "高对比度档的正文只有 AA 级";
    EXPECT_GE(contrastRatio(highContrast.secondaryText, highContrast.surface), 7.0) << "次要文本在这一档不该比正文弱";
    EXPECT_GE(contrastRatio(highContrast.text, highContrast.selection), 7.0) << "选区上的正文要最强对比";

    // 这一档还得真的和浅色档长得不一样：边框加粗是需求写的可辨识差异，样式表里没落就是空档
    const auto highContrastSheet = ThemeManager::styleSheetFor(ThemeKind::HighContrast).toUpper();
    EXPECT_FALSE(highContrastSheet.isEmpty());
    EXPECT_NE(highContrastSheet.indexOf(QStringLiteral("2PX SOLID")), -1) << "高对比度档没把边框加粗，界面与浅色档没区别";
}

/**
 * @brief 钉住样式表资源缺失时的拒绝面
 * @details 读不到就返回空串并记日志，不能拿着空样式表继续「看起来已应用」——
 *          那种状态下界面用的是原生默认色，用户与日志都看不出主题没落地。
 */
TEST_F(ThemeManagerFixture, MissingStyleSheetYieldsEmptyTextNotAFakeTheme)
{
    EXPECT_FALSE(ThemeManager::styleSheetFor(ThemeKind::Light).isEmpty());
    EXPECT_FALSE(ThemeManager::styleSheetFor(ThemeKind::Dark).isEmpty());
    // System 按浅色取：解算前拿来用不该崩，也不该给个空串让界面停在无主题
    EXPECT_FALSE(ThemeManager::styleSheetFor(ThemeKind::System).isEmpty());
}

/**
 * @brief 钉住「两套主题真的画出不一样」：同一件控件按各自样式表渲染后的底色
 * @details 上面的用例核的是令牌与样式表文本同解，这一条核的是画出来的像素：样式表在应用层
 *          没接上、或某一档的资源没打进包时，文本比对仍可全绿而界面是原生灰。
 *          取的是阅读面卡片的中部：判据只依赖底色，不依赖字形与字体可用性。
 */
TEST_F(ThemeManagerFixture, WidgetsRenderThePaletteSurfacePerTheme)
{
    const auto probe = [](const ThemeKind kind)
    {
        // setStyleSheet 是 QWidget 的成员而不是静态函数，这里挂在套件持有的应用对象上
        application->setStyleSheet(ThemeManager::styleSheetFor(kind));
        // 卡片的底色改由 SurfaceWidget 按 activePalette 取：先把这个进程级档位对齐，
        // 量到的才是这一档的 surface；取完还原，别把档位留给后面的用例
        const auto previous = GyanisScribe::Gui::activeThemeKind();
        GyanisScribe::Gui::setTextThemeKind(kind);
        // 探针挑阅读面卡片而不是空按钮：按钮平时是 controlFill 填充，拿按钮取中部像素
        // 测的是另一档角色；#pageCard 的底色就是 surface 角色本身
        GyanisScribe::Gui::SurfaceWidget card{GyanisScribe::Core::CornerRole::Card, GyanisScribe::Gui::SurfaceFillRole::Surface};
        card.setObjectName(QStringLiteral("pageCard"));
        card.resize(120, 32);
        const QPixmap pixmap = card.grab();
        GyanisScribe::Gui::setTextThemeKind(previous);
        EXPECT_FALSE(pixmap.isNull()) << "这一档没画出任何像素：" << static_cast<int>(kind);
        return pixmap.toImage().pixelColor(20, 16);
    };

    const auto light = probe(ThemeKind::Light);
    const auto dark  = probe(ThemeKind::Dark);
    // 样式表挂在应用对象上，不收回去就会跟着影响后面的用例
    application->setStyleSheet(QString{});

    EXPECT_EQ(light, paletteFor(ThemeKind::Light).surface) << "浅色档没画出它自己的面板底色";
    EXPECT_EQ(dark, paletteFor(ThemeKind::Dark).surface) << "深色档没画出它自己的面板底色";
    EXPECT_NE(light, dark) << "两档画出同一个底色，说明样式表根本没生效";
}

/**
 * @brief 钉住次级按钮平时就与卡片底分得开
 * @details 去描边之后按钮只剩 controlFill 与卡片 surface 的微明度差；这一档若被抹平，
 *          「导出配置」「导出色板」「检查更新」这类次级按钮就只剩一串文字。
 *          取按钮左内侧（横向距左边缘 10px、纵向居中）与卡片角落各一像素，既要比颜色不等，
 *          也要比明度差得看得出来。取样点留在左侧内边距里、避开水平居中的文字；往中间挪会落到字形上。
 */
TEST_F(ThemeManagerFixture, SecondaryButtonsStayDistinguishableAtRest)
{
    const auto probe = [](const ThemeKind kind)
    {
        application->setStyleSheet(ThemeManager::styleSheetFor(kind));
        // 宿主与按钮都改由自绘类取色：档位先对齐，取完还原
        const auto previous = GyanisScribe::Gui::activeThemeKind();
        GyanisScribe::Gui::setTextThemeKind(kind);
        GyanisScribe::Gui::SurfaceWidget host{GyanisScribe::Core::CornerRole::Card, GyanisScribe::Gui::SurfaceFillRole::Surface};
        host.setObjectName(QStringLiteral("pageCard"));
        host.resize(200, 80);
        GyanisScribe::Gui::Button button(QStringLiteral("导出配置…"), &host);
        button.setGeometry(30, 20, 140, 30);
        host.show();
        QCoreApplication::processEvents();
        const auto image = host.grab().toImage();
        GyanisScribe::Gui::setTextThemeKind(previous);
        application->setStyleSheet(QString{});
        // 卡片取样点留在左缘中部：Card 档 20px 圆角把 (5,5) 切在形状外，那里已经不是 surface
        return std::pair{image.pixelColor(40, 35), image.pixelColor(5, 40)};
    };

    for (const auto kind: {ThemeKind::Light, ThemeKind::Dark})
    {
        const auto [button, card] = probe(kind);
        EXPECT_NE(button, card) << static_cast<int>(kind) << " 静息按钮与卡片同色，只剩一串文字";
        EXPECT_GT(std::abs(relativeLuminance(button) - relativeLuminance(card)), 0.002) << static_cast<int>(kind) << " 明度差小到看不出来";
    }
}

/**
 * @brief 钉住控件总高按档定死，不随去边框漂移
 * @details 浅色与深色靠 1px 透明边框占位：按钮 30、输入类 32。
 *          高对比度档保留 32/34/38：2px 描边是无障碍契约的一部分，不随另两档归一化，
 *          输入类因此比浅深档各多 2px；数字框另有上下各 2px 的自有框，落在 38。
 */
TEST_F(ThemeManagerFixture, ControlHeightsStayPinnedPerTheme)
{
    for (const auto kind: {ThemeKind::Light, ThemeKind::Dark, ThemeKind::HighContrast})
    {
        application->setStyleSheet(ThemeManager::styleSheetFor(kind));
        GyanisScribe::Gui::LineEdit edit;
        GyanisScribe::Gui::Button   button{QStringLiteral("确定")};
        GyanisScribe::Gui::SpinBox  spin;
        GyanisScribe::Gui::ComboBox combo;
        for (QWidget *widget: std::initializer_list<QWidget *>{&edit, &button, &spin, &combo})
        {
            widget->ensurePolished();
        }
        const auto highContrast = kind == ThemeKind::HighContrast;
        EXPECT_EQ(edit.sizeHint().height(), highContrast ? 34 : 32) << static_cast<int>(kind) << " 输入框总高变了";
        EXPECT_EQ(spin.sizeHint().height(), highContrast ? 38 : 32) << static_cast<int>(kind) << " 数字框总高变了";
        EXPECT_EQ(combo.sizeHint().height(), highContrast ? 34 : 32) << static_cast<int>(kind) << " 下拉框总高变了";
        EXPECT_EQ(button.sizeHint().height(), highContrast ? 32 : 30) << static_cast<int>(kind) << " 按钮总高变了";
    }
    application->setStyleSheet(QString{});
}

/**
 * @brief 钉住菜单字号与菜单项内边距
 * @details 判据只读三份 QSS 的规则文本：字体由代码按 kMenuTextPointSize 落，
 *          内边距必须三份同构，否则「菜单变小」只在当前主题上成立。
 */
TEST_F(ThemeManagerFixture, MenusUseTheCompactMetrics)
{
    EXPECT_EQ(GyanisScribe::Gui::kMenuTextPointSize, 9);
    for (const auto resource: {std::string_view{":/styles/Light.qss"}, std::string_view{":/styles/Dark.qss"}, std::string_view{":/styles/HighContrast.qss"}})
    {
        const auto sheet = readStyleSheet(resource);
        EXPECT_NE(sheet.indexOf(QStringLiteral("padding: 4px 18px 4px 8px")), -1) << resource;
        EXPECT_NE(sheet.indexOf(QStringLiteral("margin: 2px 6px")), -1) << resource;
    }
}

/**
 * @brief 钉住下拉框的箭头与自绘微调框同一套 chevron
 * @details 只改 QComboBox 的边框、不接 ::down-arrow 时，原生方角箭头会留在圆角框里；
 *          三档各自的图片资源也必须真的打进 qrc。
 */
TEST_F(ThemeManagerFixture, ComboDropDownUsesTheThemeChevron)
{
    const auto cases = {std::tuple{ThemeKind::Light, std::string_view{":/styles/ComboChevronLight.svg"}},
                        std::tuple{ThemeKind::Dark, std::string_view{":/styles/ComboChevronDark.svg"}},
                        std::tuple{ThemeKind::HighContrast, std::string_view{":/styles/ComboChevronHighContrast.svg"}}};
    for (const auto &[kind, asset]: cases)
    {
        const auto sheet = ThemeManager::styleSheetFor(kind);
        EXPECT_NE(sheet.indexOf(QString::fromUtf8(asset.data(), static_cast<qsizetype>(asset.size()))), -1) << "样式表没引用这一档的 chevron";
        EXPECT_TRUE(QFile{QString::fromUtf8(asset.data(), static_cast<qsizetype>(asset.size()))}.exists()) << "chevron 资源没打进 qrc";
    }
}

/**
 * @brief 钉住三份主题都不再引用静态勾图
 * @details 勾选指示器与勾都由 Gui::CheckBox 自绘；某一档把静态 SVG 挂回 ::indicator:checked 时，
 *          它会叠在自绘勾上，而另外两档看不出问题。判据按样式表原文搜资源名。
 */
TEST_F(ThemeManagerFixture, EveryThemeDropsTheStaticCheckImage)
{
    for (const auto resource: {std::string_view{":/styles/Light.qss"}, std::string_view{":/styles/Dark.qss"}, std::string_view{":/styles/HighContrast.qss"}})
    {
        const auto sheet = readStyleSheet(resource);
        ASSERT_FALSE(sheet.isEmpty()) << resource;
        EXPECT_EQ(sheet.indexOf(QStringLiteral("CheckmarkOnAccent")), -1) << resource << " 又挂回了静态勾图";
    }
}

/**
 * @brief 钉住原生对话窗里的输入框仍有自己的底
 * @details 输入类的透明底规则只限定到带 squircle 属性的自绘类；取色、文本输入与文件对话框
 *          里的 QLineEdit 仍匹配类型选择器时，会变成透明底加透明边、没有任何填充。
 *          这里在宿主红底上放一个原生 QLineEdit：中心必须是 QPalette::Base 对应的 surface，
 *          宿主红底露出来就说明透明规则又落回了全局。
 */
TEST_F(ThemeManagerFixture, StockQtInputsKeepTheirNativeFill)
{
    ASSERT_TRUE(manager().setTheme(ThemeKind::Light).has_value());
    QWidget host;
    host.resize(200, 60);
    host.setAutoFillBackground(true);
    host.setPalette(QPalette{QColor{255, 0, 0}});
    auto *edit = new QLineEdit{&host};
    edit->setGeometry(20, 15, 160, 30);
    host.show();
    QCoreApplication::processEvents();

    const auto image = host.grab().toImage();
    EXPECT_EQ(image.pixelColor(edit->geometry().center()), GyanisScribe::Gui::kLightPalette.surface) << "原生输入框丢了 QPalette::Base 填充，露出了宿主底色";
}

/**
 * @brief 钉住系统对话框的原生按钮保留完整外观
 * @details 消息框、输入框与取色器里的按钮是 Qt 自己造的 QPushButton，没有 squircle 属性；
 *          按钮规则若退回类型选择器，它们会套上透明边框与内边距，却没有 Gui::Button 的自绘填充，
 *          界面上只剩文字与图标。三份样式表下都放一个原生按钮：左右内侧必须是不透明填充，
 *          且不能露出宿主红底。
 */
TEST_F(ThemeManagerFixture, StockQtDialogButtonsKeepTheirNativeChrome)
{
    for (const auto kind: {ThemeKind::Light, ThemeKind::Dark, ThemeKind::HighContrast})
    {
        application->setStyleSheet(ThemeManager::styleSheetFor(kind));
        QWidget host;
        host.resize(200, 60);
        host.setAutoFillBackground(true);
        host.setPalette(QPalette{QColor{255, 0, 0}});
        auto *button = new QPushButton{QStringLiteral("确定"), &host};
        button->setGeometry(20, 15, 160, 30);
        host.show();
        QCoreApplication::processEvents();
        button->clearFocus();
        button->update();
        QCoreApplication::processEvents();

        const QImage image = host.grab().toImage();
        const QRect  area{button->geometry()};
        for (const QPoint probe: {QPoint{area.left() + 10, area.center().y()}, QPoint{area.right() - 10, area.center().y()}})
        {
            const QColor color = image.pixelColor(probe);
            EXPECT_EQ(color.alpha(), 255) << static_cast<int>(kind) << " 档下原生按钮的填充被抹掉了";
            EXPECT_NE(color, QColor(255, 0, 0)) << static_cast<int>(kind) << " 档下原生按钮露出了宿主底色，只剩文字与图标";
        }
        application->setStyleSheet(QString{});
    }
}

/**
 * @brief 钉住换主题时应用调色板跟着换
 * @details 样式表覆盖不到勾选的勾、微调框箭头、占位文字与禁用文字——这些走调色板。
 *          只落样式表时文本比对全绿而界面是浅色原生件，所以这里按角色逐个核到令牌上，
 *          并反向确认换档之后另一套的底色不在了。
 */
TEST_F(ThemeManagerFixture, ApplicationPaletteFollowsTheResolvedTheme)
{
    ASSERT_TRUE(manager().setTheme(ThemeKind::Dark).has_value());
    const auto dark = application->palette();
    EXPECT_EQ(dark.color(QPalette::Window), GyanisScribe::Gui::kDarkPalette.windowBackground);
    EXPECT_EQ(dark.color(QPalette::Base), GyanisScribe::Gui::kDarkPalette.surface);
    EXPECT_EQ(dark.color(QPalette::Text), GyanisScribe::Gui::kDarkPalette.text);
    EXPECT_EQ(dark.color(QPalette::PlaceholderText), GyanisScribe::Gui::kDarkPalette.secondaryText);
    EXPECT_EQ(dark.color(QPalette::Highlight), GyanisScribe::Gui::kDarkPalette.accentSoft);
    EXPECT_EQ(dark.color(QPalette::Disabled, QPalette::Text), GyanisScribe::Gui::kDarkPalette.secondaryText);

    ASSERT_TRUE(manager().setTheme(ThemeKind::Light).has_value());
    const auto light = application->palette();
    EXPECT_EQ(light.color(QPalette::Window), GyanisScribe::Gui::kLightPalette.windowBackground);
    EXPECT_NE(light.color(QPalette::Window), GyanisScribe::Gui::kDarkPalette.windowBackground) << "换档之后深色那套的底还在调色板里";
    EXPECT_EQ(light.color(QPalette::Highlight), GyanisScribe::Gui::kLightPalette.accentSoft);
}

/**
 * @brief 钉住「换主题时已经建好的控件也跟着换色」
 * @details 应用级 setPalette 只给之后新建的控件定默认值：设置页里换档号称即时生效，
 *          而没落这一步时标签文字仍停在旧主题的色上——换成深色档就是「看不见字」，
 *          样式表、令牌、用例却全都绿（2026-10-10 是把窗口画成位图才看见的）。
 *          判据挂在父子两个控件上：真实界面里的文字都是子控件，靠继承拿色。
 */
TEST_F(ThemeManagerFixture, ExistingWidgetsRepaintWithTheNewThemeText)
{
    ASSERT_TRUE(manager().setTheme(ThemeKind::Light).has_value());

    QWidget window;
    QLabel  heading{QStringLiteral("录屏"), &window};
    auto   *editor = new QLineEdit{&window};
    editor->setPlaceholderText(QStringLiteral("占位"));
    ASSERT_EQ(heading.palette().color(QPalette::WindowText), GyanisScribe::Gui::kLightPalette.text);

    ASSERT_TRUE(manager().setTheme(ThemeKind::Dark).has_value());

    EXPECT_EQ(heading.palette().color(QPalette::WindowText), GyanisScribe::Gui::kDarkPalette.text) << "已存在的标签留着旧主题的正文色，深色档下等于看不见字";
    EXPECT_EQ(editor->palette().color(QPalette::PlaceholderText), GyanisScribe::Gui::kDarkPalette.secondaryText) << "占位文字留着旧档的灰";
}

/**
 * @brief 钉住勾选框与线宽滑块的键盘焦点看得见
 * @details 高对比度档不装焦点环覆盖层：勾选框的焦点由 Gui::CheckBox 给指示器自绘 2px 主色边，
 *          滑块的焦点由 Gui::Slider 自绘反转（手柄换底色芯 + 正文色圈）。两处判据不赌具体坐标：
 *          勾选框在聚焦前后各抓一次图，数主色像素，聚焦后必须多出来；
 *          滑块数的是手柄矩形里的主色，聚焦时必须掉下去。
 */
TEST_F(ThemeManagerFixture, CheckboxAndSliderShowAKeyboardFocusRing)
{
    ASSERT_TRUE(manager().setTheme(ThemeKind::HighContrast).has_value());
    const auto accent = QColor(0x0B, 0x57, 0xD0);

    const auto accentPixels = [&accent](QWidget &widget)
    {
        const auto shot  = widget.grab().toImage();
        int        count = 0;
        for (int y = 0; y < shot.height(); ++y)
        {
            for (int x = 0; x < shot.width(); ++x)
            {
                const auto pixel = shot.pixelColor(x, y);
                if (std::abs(pixel.red() - accent.red()) <= 16 && std::abs(pixel.green() - accent.green()) <= 16 && std::abs(pixel.blue() - accent.blue()) <= 16)
                {
                    ++count;
                }
            }
        }
        return count;
    };

    QWidget host;
    auto   *check  = new GyanisScribe::Gui::CheckBox{&host};
    auto   *slider = new GyanisScribe::Gui::Slider{Qt::Horizontal, &host};
    check->setText(QStringLiteral("check"));
    slider->setRange(0, 100);
    slider->setValue(100); // 手柄推到右端，槽的左段整条露出来
    check->move(0, 0);
    slider->move(0, 30);
    slider->resize(200, 30);
    host.resize(220, 80);
    host.show();
    QCoreApplication::processEvents();
    // show 之后第一个可聚焦控件（勾选框）会自动拿到焦点：先把焦点清掉，before 才是真「未聚焦」
    check->clearFocus();
    slider->clearFocus();
    QCoreApplication::processEvents();

    // 手柄几何按 Gui::Slider 的绘制口径取：边长 18，位置由 style()->sliderPositionFromValue 在
    // 控件宽减手柄边长的区间上算。焦点反转是「主色实心」翻成「底色芯 + 正文色圈」，
    // 判据落在手柄自己的矩形里数主色：实心时上百，翻过来之后归零。
    constexpr int kHandleEdge = 18;
    const auto    handleRect  = [](GyanisScribe::Gui::Slider &widget)
    {
        const int span     = std::max(0, widget.width() - kHandleEdge);
        const int position = widget.style()->sliderPositionFromValue(widget.minimum(), widget.maximum(), widget.value(), span, false);
        return QRect{position, widget.height() / 2 - kHandleEdge / 2, kHandleEdge, kHandleEdge};
    };
    const auto handleAccentPixels = [&accent, &handleRect](GyanisScribe::Gui::Slider &widget)
    {
        const QRect handle = handleRect(widget);
        const auto  shot   = widget.grab().toImage();
        int         count  = 0;
        for (int y = handle.top(); y <= handle.bottom(); ++y)
        {
            for (int x = handle.left(); x <= handle.right(); ++x)
            {
                const auto pixel = shot.pixelColor(x, y);
                if (std::abs(pixel.red() - accent.red()) <= 16 && std::abs(pixel.green() - accent.green()) <= 16 && std::abs(pixel.blue() - accent.blue()) <= 16)
                {
                    ++count;
                }
            }
        }
        return count;
    };

    const int checkBefore  = accentPixels(*check);
    const int sliderBefore = handleAccentPixels(*slider);

    check->setFocus(Qt::TabFocusReason);
    QCoreApplication::processEvents();
    EXPECT_GT(accentPixels(*check), checkBefore + 10) << "勾选框聚焦后没有主色焦点框";
    slider->setFocus(Qt::TabFocusReason);
    QCoreApplication::processEvents();
    EXPECT_GT(sliderBefore, 50) << "未聚焦时手柄本该是主色实心，判据的前提没构造出来";
    EXPECT_LT(handleAccentPixels(*slider), 10) << "滑块聚焦后手柄外观没变，键盘焦点看不出来";
}

/**
 * @brief 钉住三份样式表的控件级焦点都不再由 QSS 承担
 * @details 按钮、输入类与勾选框的可见焦点边由 Gui::Button / Gui::ToolButton / Gui::LineEdit /
 *          Gui::CheckBox 等按 ControlPalette 及主题色自绘，浅深档另有 FocusRingController 的覆盖层；
 *          样式表里若还留着 QPushButton:focus、QLineEdit:focus 一类规则，会在自绘边之外再压一圈。
 *          判据按选择器分支看，不能只搜全文；三份样式表都必须一条 :focus 规则都不剩。
 */
TEST_F(ThemeManagerFixture, AllSheetsDropTheControlLevelFocusOutlines)
{
    const auto cases = {std::string_view{":/styles/Light.qss"}, std::string_view{":/styles/Dark.qss"}, std::string_view{":/styles/HighContrast.qss"}};
    for (const auto &resource: cases)
    {
        const auto sheet     = readStyleSheet(resource);
        const auto selectors = focusSelectorsIn(sheet);
        ASSERT_FALSE(sheet.isEmpty()) << resource;
        // 滑块手柄与勾选框指示器的焦点都改由自绘代码表达，三份样式表都不再保留 :focus 规则
        EXPECT_TRUE(selectors.isEmpty()) << resource << " 还留着 :focus 规则：" << selectors.join(QStringLiteral("; ")).toStdString();

        const auto branches = [&selectors](const QString &token)
        {
            QStringList matched;
            for (const auto &selector: selectors)
            {
                if (selector.contains(token))
                {
                    matched.append(selector);
                }
            }
            return matched;
        };

        // 按钮、输入类与勾选框的焦点边都改由控件自绘：任何一份样式表都不该再有这些分支
        for (const auto *token: {"QPushButton:focus", "QToolButton:focus", "QLineEdit:focus", "QComboBox:focus", "QSpinBox:focus", "QTextEdit#annotationTextEditor:focus",
                                 "QCheckBox::indicator:focus", "QSlider::handle:horizontal:focus"})
        {
            const auto matched = branches(QString::fromLatin1(token));
            EXPECT_TRUE(matched.isEmpty()) << resource << " 还留着控件级焦点描边：" << matched.join(QStringLiteral("; ")).toStdString();
        }

        // 高对比度档仍保留控件级 2px 描边占位，由 SemanticColorPairsMeetWcagContrast 的 2PX SOLID 判据钉住
    }
}

/**
 * @brief 钉住焦点环按控件类型取连续曲率形状
 * @details 覆盖层的内外两环都由 Core::continuousCornerPath 生成，不再用 drawRoundedRect；
 *          勾选框取 Indicator 档、工具按钮取 Compact 档、其余控件取 Control 档。
 *          角部的差别只有一两个像素，像素用例分辨不出「普通圆角」与「连续曲率」，
 *          这一条按源码级接线守住形状来源与三档映射。
 */
TEST_F(ThemeManagerFixture, FocusRingUsesThePerControlContinuousCornerShape)
{
    const auto source = readSourceFile("src/Gui/Controls/FocusRing.cpp");
    ASSERT_FALSE(source.empty()) << "读不到焦点环源文件";
    EXPECT_EQ(source.find("drawRoundedRect"), std::string::npos) << "焦点环还在用普通圆角";
    EXPECT_NE(source.find("continuousCornerPath"), std::string::npos) << "焦点环没走连续曲率路径";
    for (const auto *role: {"CornerRole::Indicator", "CornerRole::Compact", "CornerRole::Control"})
    {
        EXPECT_NE(source.find(role), std::string::npos) << "焦点环没按 " << role << " 取形状";
    }
}

/**
 * @brief 钉住勾选框指示器走连续曲率自绘
 * @details 指示器的底与描边由 Gui::CheckBox 在 paintEvent 里自绘，形状取 Indicator 档，
 *          样式表只留尺寸占位。角部差异只有一两个像素，像素用例分辨不出普通圆角与连续曲率，
 *          这一条按源码级接线守住绘制入口与形状来源。
 */
TEST_F(ThemeManagerFixture, CheckBoxPaintsTheIndicatorWithTheContinuousCornerPath)
{
    const auto source = readSourceFile("src/Gui/Controls/CheckBox.cpp");
    ASSERT_FALSE(source.empty()) << "读不到勾选框源文件";
    EXPECT_EQ(source.find("drawRoundedRect"), std::string::npos) << "勾选框还在用普通圆角";

    const auto paintStart = source.find("void CheckBox::paintEvent");
    ASSERT_NE(paintStart, std::string::npos) << "勾选框源文件里找不到 paintEvent";
    const auto paintEnd = enclosingFunctionEnd(source, paintStart);
    ASSERT_NE(paintEnd, std::string::npos) << "paintEvent 的括号没有配对";

    const auto paintCall = source.find("paintContinuousCorner", paintStart);
    ASSERT_NE(paintCall, std::string::npos) << "指示器没有走连续曲率绘制";
    EXPECT_LT(paintCall, paintEnd) << "连续曲率绘制不在 paintEvent 体内";

    const auto role = source.find("CornerRole::Indicator", paintStart);
    ASSERT_NE(role, std::string::npos) << "指示器没按 Indicator 档取形状";
    EXPECT_LT(role, paintEnd) << "Indicator 档不在 paintEvent 体内";
}

/**
 * @brief 钉住换主题时 ThemeManager 会重放已存在控件的文字角色
 * @details 只测 applyTextRole 本身漏掉的是 allWidgets 这条接线：控件在浅色档建好、换到深色档后
 *          若不再重算，暗色纯 ASCII 读数会停在默认字族与 600，可变字族的降重永远不生效。
 *          可变字族的可用性由测试钩子钉死，不依赖跑测机器装没装该字族。
 */
TEST_F(ThemeManagerFixture, ThemeSwitchReplaysAsciiMetricRoleForExistingWidgets)
{
    struct VariableFamilyGuard final
    {
        ~VariableFamilyGuard()
        {
            setVariableMetricFamilyAvailableForTesting(std::nullopt);
        }
    } guard;
    setVariableMetricFamilyAvailableForTesting(true);

    ASSERT_TRUE(manager().setTheme(ThemeKind::Light).has_value());
    QLabel label;
    auto   baseFont = label.font();
    baseFont.setWeight(QFont::Normal);
    label.setFont(baseFont);
    const auto baseFamily = label.font().family();

    applyTextRole(label, TextRole::Metric, TextScript::AsciiOnly);
    EXPECT_EQ(static_cast<int>(label.font().weight()), 600) << "浅色档的读数该停在 DemiBold";
    EXPECT_EQ(label.font().family(), baseFamily);

    ASSERT_TRUE(manager().setTheme(ThemeKind::Dark).has_value());
    EXPECT_EQ(static_cast<int>(label.font().weight()), 560) << "换到深色档没有重放文字角色，可变字族的降重没生效";
    EXPECT_EQ(label.font().family(), QStringLiteral("Segoe UI Variable Text")) << "换到深色档没有换成可变字族";

    ASSERT_TRUE(manager().setTheme(ThemeKind::Light).has_value());
    EXPECT_EQ(static_cast<int>(label.font().weight()), 600) << "切回浅色档没有把字重还原成 600";
    EXPECT_EQ(label.font().family(), baseFamily) << "切回浅色档还挂着深色档的可变字族";
}

/**
 * @brief 钉住可见描边宽度按主题分档
 * @details 只有高对比度档加粗到 2px；其余两档是 1px，可见描边由自绘代码按这个值画。
 */
TEST_F(ThemeManagerFixture, HighContrastBorderWidthIsTwo)
{
    EXPECT_EQ(GyanisScribe::Gui::themeBorderWidth(ThemeKind::Light), 1);
    EXPECT_EQ(GyanisScribe::Gui::themeBorderWidth(ThemeKind::Dark), 1);
    EXPECT_EQ(GyanisScribe::Gui::themeBorderWidth(ThemeKind::HighContrast), 2);
}

/**
 * @brief 钉住三处自绘读数在绘制函数里确实走了 tabularFont
 * @details 遮罩读数、取色信息条与贴图颜色读数没有 QLabel 可断言字体：字体直接设在 QPainter 上。
 *          这里按源码级接线守住那一行调用，并限定它落在绘制函数体内，防止调用被挪到永不执行的分支。
 *          控件级读数（长截图、OCR 摘要、历史列表、录屏时长与倒计时）另由各自用例逐件断言。
 */
TEST_F(ThemeManagerFixture, PaintedReadoutsLockTabularFiguresInsideThePaintFunctions)
{
    struct Probe
    {
        std::string_view file;
        std::string_view function;
        std::string_view call;
    };
    const std::array probes = {
            Probe{"src/Gui/Capture/CaptureOverlay.cpp", "void CaptureOverlay::paintMagnifier", "tabularFont(stripFont, TextScript::Mixed)"},
            Probe{"src/Gui/Capture/CaptureOverlay.cpp", "void CaptureOverlay::paintEvent", "tabularFont(hintFont, TextScript::Mixed)"},
            Probe{"src/Gui/Pick/ColorPickerOverlay.cpp", "void ColorPickerOverlay::paintEvent", "tabularFont(painter.font(), TextScript::AsciiOnly)"},
            Probe{"src/Gui/Pin/PinWindow.cpp", "void PinWindow::paintColorReadout", "tabularFont(font, TextScript::AsciiOnly)"},
    };

    for (const auto &probe: probes)
    {
        const auto source = readSourceFile(probe.file);
        ASSERT_FALSE(source.empty()) << "读不到源文件：" << std::string{probe.file};

        const auto functionStart = source.find(probe.function);
        ASSERT_NE(functionStart, std::string::npos) << std::string{probe.file} << " 里找不到绘制函数 " << std::string{probe.function};
        const auto functionEnd = enclosingFunctionEnd(source, functionStart);
        ASSERT_NE(functionEnd, std::string::npos) << std::string{probe.file} << " 的绘制函数括号没有配对";

        const auto call = source.find(probe.call, functionStart);
        ASSERT_NE(call, std::string::npos) << std::string{probe.file} << " 的绘制函数里没有 " << std::string{probe.call};
        EXPECT_LT(call, functionEnd) << std::string{probe.file} << " 的 tabularFont 调用不在绘制函数体内";
    }
}
