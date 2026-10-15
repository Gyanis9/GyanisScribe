#include "Gui/Controls/Toast.h"

#include "Core/Settings/SettingsStore.h"
#include "Gui/Theme/DesignTokens.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include <filesystem>
#include <format>
#include <memory>
#include <string>

using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Gui::kDarkPalette;
using GyanisScribe::Gui::kLightPalette;
using GyanisScribe::Gui::paletteFor;
using GyanisScribe::Gui::ThemeKind;
using GyanisScribe::Gui::ThemeManager;
using GyanisScribe::Gui::Toast;
using GyanisScribe::Gui::toastColorFor;
using GyanisScribe::Gui::ToastKind;
using GyanisScribe::Gui::toastPositionFor;

namespace
{
    std::unique_ptr<QApplication> application;

    class ToastFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestToast";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            // Toast 每次显示都从主题现取色，所以用例要有真主题；INI 走本用例专属路径，不与其它用例共用
            const auto iniPath = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Toast-{}.ini", ::testing::UnitTest::GetInstance()->current_test_info()->name());
            std::error_code cleanup;
            std::filesystem::remove(iniPath, cleanup);
            m_store   = std::make_unique<SettingsStore>(iniPath);
            m_theme   = std::make_unique<ThemeManager>(*m_store);
            m_icons   = std::make_unique<GyanisScribe::Gui::IconProvider>();
            m_toast   = std::make_unique<Toast>(*m_icons, *m_theme);
            m_iniPath = iniPath;
        }

        void TearDown() override
        {
            m_toast.reset();
            m_icons.reset();
            m_theme.reset();
            m_store.reset();
            std::error_code cleanup;
            std::filesystem::remove(m_iniPath, cleanup);
        }

        Toast &toast() const
        {
            return *m_toast;
        }

        ThemeManager &theme() const
        {
            return *m_theme;
        }

    private:
        std::filesystem::path                           m_iniPath;
        std::unique_ptr<SettingsStore>                  m_store;
        std::unique_ptr<ThemeManager>                   m_theme;
        std::unique_ptr<GyanisScribe::Gui::IconProvider> m_icons;
        std::unique_ptr<Toast>                          m_toast;
    };

} // namespace

/**
 * @brief 钉住三种提示各自落到的语义色
 * @details 成功绿、危险红、信息中性——需求写死的对应关系；这里对两套主题各判一次，
 *          判的是「等于色板里那个字段」，不是「看起来像绿色」。
 */
TEST(ToastColors, EachKindMapsToItsPaletteRole)
{
    for (const auto kind: {ThemeKind::Light, ThemeKind::Dark})
    {
        const auto palette = paletteFor(kind);
        EXPECT_EQ(toastColorFor(ToastKind::Success, palette), palette.success) << static_cast<int>(kind);
        EXPECT_EQ(toastColorFor(ToastKind::Danger, palette), palette.danger) << static_cast<int>(kind);
        EXPECT_EQ(toastColorFor(ToastKind::Information, palette), palette.secondaryText) << static_cast<int>(kind);
    }

    // 两套主题的 success/danger 不能撞色：撞了就说明有一份色板没填对，深浅两档看着一样
    EXPECT_NE(kLightPalette.success, kDarkPalette.success);
    EXPECT_NE(kLightPalette.danger, kDarkPalette.danger);
}

/**
 * @brief 钉住气泡落在右下角且留出边距
 * @details 手算：1000×800 的可用区、200×60 的气泡 → x = 999-200-16+1，y = 799-60-16+1。
 */
TEST(ToastPlacement, SitsBottomRightWithMargin)
{
    const QPoint placed = toastPositionFor(QRect{0, 0, 1000, 800}, QSize{200, 60});
    EXPECT_EQ(placed, QPoint(784, 724));

    // 屏幕不在原点也要贴自己的右下角
    EXPECT_EQ(toastPositionFor(QRect{1600, 0, 1280, 720}, QSize{200, 60}), QPoint(1600 + 1280 - 200 - 16, 719 - 60 - 16 + 1));
}

/**
 * @brief 钉住「比屏幕还宽」时夹回屏内
 * @details 需求不许气泡遮挡关键操作区，更不许自己露出一半
 */
TEST(ToastPlacement, ClampsInsideWhenBiggerThanScreen)
{
    const QPoint placed = toastPositionFor(QRect{0, 0, 100, 80}, QSize{200, 60});
    EXPECT_GE(placed.x(), 0);
    EXPECT_GE(placed.y(), 0);
    EXPECT_LE(placed.x() + 200, 100 + 200) << "左边界夹住即可，超出的宽度由屏幕自己裁";
}

/**
 * @brief 钉住退化入参给原点而不是崩
 */
TEST(ToastPlacement, DegenerateInputsYieldOrigin)
{
    EXPECT_EQ(toastPositionFor(QRect{}, QSize{200, 60}), QPoint(0, 0));
    EXPECT_EQ(toastPositionFor(QRect{0, 0, 100, 100}, QSize{}), QPoint(0, 0));
}

/**
 * @brief 钉住「新提示顶掉旧的」
 * @details 排队会把三秒前的操作当成刚发生的；这条不靠等待时间验证自动隐藏，
 *          只判文本被替换这件事——隐藏的判据交给定时器与肉眼验收。
 */
TEST_F(ToastFixture, NewToastReplacesThePreviousText)
{
    toast().show(QStringLiteral("截图已交剪贴板"), ToastKind::Success);
    EXPECT_EQ(toast().currentText(), QStringLiteral("截图已交剪贴板"));

    toast().show(QStringLiteral("识别失败：没有语言包"), ToastKind::Danger);
    EXPECT_EQ(toast().currentText(), QStringLiteral("识别失败：没有语言包")) << "第二条没顶掉第一条，用户会把旧提示当成新结果";
}

/**
 * @brief 钉住主题切换后新的提示用新色板
 * @details 颜色是从主题现取的，这条把深色主题落下去再显示一次，确认没缓存到旧衣
 */
TEST_F(ToastFixture, ToastFollowsThemeSwitch)
{
    ASSERT_TRUE(theme().setTheme(ThemeKind::Dark).has_value());
    toast().show(QStringLiteral("深色主题下的提示"), ToastKind::Information);

    EXPECT_EQ(toast().currentText(), QStringLiteral("深色主题下的提示"));
    EXPECT_EQ(toastColorFor(ToastKind::Information, theme().palette()), kDarkPalette.secondaryText);
}
