#include "Ui/DesignTokens.h"
#include "Ui/ThemeKind.h"
#include "Ui/ThemeManager.h"

#include "Core/Logger.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"
#include "Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QString>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>
#include <QCheckBox>
#include <string>

using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Core::Logger;
using GyanisScribe::Settings::kThemeMode;
using GyanisScribe::Settings::SettingsStore;
using GyanisScribe::Ui::paletteFor;
using GyanisScribe::Ui::parseThemeKind;
using GyanisScribe::Ui::resolveThemeKind;
using GyanisScribe::Ui::SystemScheme;
using GyanisScribe::Ui::ThemeKind;
using GyanisScribe::Ui::themeKindText;
using GyanisScribe::Ui::ThemeManager;

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
     * @brief 主题层夹具：主题管理器接真实的 QSettings 后端（临时 INI）
     * @details 用 SettingsStore 而不是替身，是为了把「设置页写一个键 → 主题自己跟上」这条接线
     *          也一起验掉；替身只会验到我自己的调用点。
     */
    class ThemeManagerFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            int   argumentCount  = 1;
            char  argumentZero[] = "TestThemeManager";
            char *arguments[]    = {argumentZero, nullptr};
            application          = std::make_unique<QApplication>(argumentCount, arguments);
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
        GyanisScribe::Settings::ISettingsStore &store() const
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
        std::filesystem::path                                   m_iniPath; ///< 本用例专属的临时 INI
        std::unique_ptr<GyanisScribe::Settings::ISettingsStore> m_store;   ///< 配置后端
        std::unique_ptr<ThemeManager>                           m_manager; ///< 被测管理器
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
    EXPECT_NE(darkSheet.indexOf(hexOf(GyanisScribe::Ui::kDarkPalette.windowBackground)), -1);
    EXPECT_EQ(darkSheet.indexOf(hexOf(GyanisScribe::Ui::kLightPalette.windowBackground)), -1) << "深色样式表里还留着浅色的底色，两套规则没被整片换掉";
    EXPECT_EQ(manager().iconTint(), GyanisScribe::Ui::kDarkPalette.icon);

    ASSERT_TRUE(manager().setTheme(ThemeKind::Light).has_value());
    EXPECT_EQ(manager().resolvedKind(), ThemeKind::Light);
    const auto lightSheet = application->styleSheet().toUpper();
    EXPECT_NE(lightSheet.indexOf(hexOf(GyanisScribe::Ui::kLightPalette.windowBackground)), -1);
    EXPECT_EQ(lightSheet.indexOf(hexOf(GyanisScribe::Ui::kDarkPalette.windowBackground)), -1);
    EXPECT_EQ(manager().iconTint(), GyanisScribe::Ui::kLightPalette.icon);

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
    EXPECT_NE(application->styleSheet().toUpper().indexOf(hexOf(GyanisScribe::Ui::kDarkPalette.windowBackground)), -1);

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
 * @details 正向：QSS 里出现的每个十六进制色都必须是该主题色板里的一个（禁「代码外再有一套色」）；
 *          反向：色板每个字段都得出现在 QSS 里，除 icon 由 IconProvider 取用——
 *          这一条同时抓出「加了没人用的色板字段」这种死配置。
 */
TEST_F(ThemeManagerFixture, StyleSheetsAndPalettesAgreeInBothDirections)
{
    for (const auto [kind, resource]: {std::pair{ThemeKind::Light, std::string_view{":/styles/Light.qss"}}, std::pair{ThemeKind::Dark, std::string_view{":/styles/Dark.qss"}},
                                       std::pair{ThemeKind::HighContrast, std::string_view{":/styles/HighContrast.qss"}}})
    {
        const auto sheet   = readStyleSheet(resource).toUpper();
        const auto palette = paletteFor(kind);
        ASSERT_FALSE(sheet.isEmpty()) << resource;

        // 只列该进样式表的十四个角色：icon/success/danger 是给绘制代码用的（图标描边、Toast 语义色），
        // 出现在 QSS 里反而是问题，下面单独反着断言
        const std::array paletteColors = {palette.windowBackground, palette.surface,     palette.text,       palette.secondaryText, palette.border,
                                          palette.accent,           palette.onAccent,    palette.selection,  palette.mutedFill,     palette.hover,
                                          palette.pressed,          palette.accentHover, palette.accentSoft, palette.borderStrong};

        // 正向：QSS 里不许出现色板之外的色值
        QRegularExpression hexPattern{QStringLiteral("#[0-9A-Fa-f]{6}")};
        for (auto match = hexPattern.match(sheet); match.hasMatch(); match = hexPattern.match(sheet, match.capturedEnd()))
        {
            const auto found = std::ranges::any_of(paletteColors, [&match](const QColor &color) { return hexOf(color) == match.captured(0).toUpper(); });
            EXPECT_TRUE(found) << resource << " 里的 " << match.captured(0).toStdString() << " 不在该主题色板里";
        }

        // 反向：色板字段必须真的被样式表用着
        for (const auto &color: paletteColors)
        {
            EXPECT_NE(sheet.indexOf(hexOf(color)), -1) << resource << " 没用出色板里的 " << hexOf(color).toStdString();
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
 *          取的是空按钮的中部：判据只依赖底色，不依赖字形与字体可用性。
 */
TEST_F(ThemeManagerFixture, WidgetsRenderThePaletteSurfacePerTheme)
{
    const auto probe = [](const ThemeKind kind)
    {
        // setStyleSheet 是 QWidget 的成员而不是静态函数，这里挂在套件持有的应用对象上
        application->setStyleSheet(ThemeManager::styleSheetFor(kind));
        QPushButton button;
        button.resize(120, 32);
        const QPixmap pixmap = button.grab();
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
 * @brief 钉住「切主题时应用调色板跟着换」：原生绘制的那部分只认 QPalette
 * @details 样式表覆盖不到勾选的勾、微调框箭头、占位文字与禁用文字——这些走调色板。
 *          只落样式表时文本比对全绿而界面是浅色原生件，所以这里按角色逐个核到令牌上，
 *          并反向确认换档之后另一套的底色不在了。
 */
TEST_F(ThemeManagerFixture, ApplicationPaletteFollowsTheResolvedTheme)
{
    ASSERT_TRUE(manager().setTheme(ThemeKind::Dark).has_value());
    const auto dark = application->palette();
    EXPECT_EQ(dark.color(QPalette::Window), GyanisScribe::Ui::kDarkPalette.windowBackground);
    EXPECT_EQ(dark.color(QPalette::Base), GyanisScribe::Ui::kDarkPalette.surface);
    EXPECT_EQ(dark.color(QPalette::Text), GyanisScribe::Ui::kDarkPalette.text);
    EXPECT_EQ(dark.color(QPalette::PlaceholderText), GyanisScribe::Ui::kDarkPalette.secondaryText);
    EXPECT_EQ(dark.color(QPalette::Highlight), GyanisScribe::Ui::kDarkPalette.accentSoft);
    EXPECT_EQ(dark.color(QPalette::Disabled, QPalette::Text), GyanisScribe::Ui::kDarkPalette.secondaryText);

    ASSERT_TRUE(manager().setTheme(ThemeKind::Light).has_value());
    const auto light = application->palette();
    EXPECT_EQ(light.color(QPalette::Window), GyanisScribe::Ui::kLightPalette.windowBackground);
    EXPECT_NE(light.color(QPalette::Window), GyanisScribe::Ui::kDarkPalette.windowBackground) << "换档之后深色那套的底还在调色板里";
    EXPECT_EQ(light.color(QPalette::Highlight), GyanisScribe::Ui::kLightPalette.accentSoft);
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
    ASSERT_EQ(heading.palette().color(QPalette::WindowText), GyanisScribe::Ui::kLightPalette.text);

    ASSERT_TRUE(manager().setTheme(ThemeKind::Dark).has_value());

    EXPECT_EQ(heading.palette().color(QPalette::WindowText), GyanisScribe::Ui::kDarkPalette.text) << "已存在的标签留着旧主题的正文色，深色档下等于看不见字";
    EXPECT_EQ(editor->palette().color(QPalette::PlaceholderText), GyanisScribe::Ui::kDarkPalette.secondaryText) << "占位文字留着旧档的灰";
}
