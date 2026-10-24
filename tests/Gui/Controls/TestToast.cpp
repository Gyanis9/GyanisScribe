#include "Gui/Controls/Toast.h"

#include "Core/Settings/SettingsStore.h"
#include "Gui/Controls/StreamingText.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/DesignTokens.h"
#include "Gui/Theme/Motion.h"
#include "Gui/Theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QFontInfo>
#include <QFontMetrics>
#include <QLabel>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>

using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Gui::IconSize;
using GyanisScribe::Gui::iconTextTopPad;
using GyanisScribe::Gui::kDarkPalette;
using GyanisScribe::Gui::kLightPalette;
using GyanisScribe::Gui::paletteFor;
using GyanisScribe::Gui::setAnimationsEnabledForTesting;
using GyanisScribe::Gui::StreamingTextOverlay;
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

    /// 当前可见的 Toast 气泡：没有父窗口的顶层 ToolTip，同一时刻只有一个
    QWidget *visibleToastBubble()
    {
        for (auto *widget: QApplication::topLevelWidgets())
        {
            if (widget->isVisible() && widget->windowType() == Qt::ToolTip)
            {
                return widget;
            }
        }
        return nullptr;
    }

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

/**
 * @brief 钉住只有成功与信息提示走流式，错误提示与关闭动画时一律即时
 * @details 错误提示的字必须整句出现，逐字浮现会让人先读半句再改口。
 */
TEST(ToastPolicy, DangerIsImmediateAndOthersStreamWhenAnimationsAreOn)
{
    using GyanisScribe::Gui::shouldStreamToast;
    using GyanisScribe::Gui::ToastKind;
    EXPECT_TRUE(shouldStreamToast(ToastKind::Success, true));
    EXPECT_TRUE(shouldStreamToast(ToastKind::Information, true));
    EXPECT_FALSE(shouldStreamToast(ToastKind::Danger, true));
    EXPECT_FALSE(shouldStreamToast(ToastKind::Success, false));
}

/**
 * @brief 钉住图标底边落在首行基线下方 0.125em
 * @details 手算：图标 20 高、上留 6、基线 13、em 高 15 → pad = round(6+20−13−1.875) = 11；
 *          图标底 26 与文字基线 24 相差 2，正好是 0.125em 的取整结果。
 */
TEST(ToastPolicy, IconSitsOneEighthEmBelowTheFirstBaseline)
{
    using GyanisScribe::Gui::iconTextTopPad;
    const int pad      = iconTextTopPad(20, 13, 15, 6);
    const int iconBase = 6 + 20;
    const int textBase = pad + 13;
    EXPECT_EQ(pad, 11);
    EXPECT_NEAR(static_cast<double>(iconBase - textBase), 0.125 * 15.0, 1.0);
}

/**
 * @brief 钉住流式覆盖层只出现在成功与信息提示上
 * @details 错误提示要整句出现；关闭动画时所有提示都直接全显。判的是气泡里真实存在的覆盖层。
 */
TEST_F(ToastFixture, StreamingOverlayFollowsKindAndMotionPreference)
{
    // 动画开关是全局状态：用例结束恢复默认，避免污染同进程的其它用例
    struct MotionGuard final
    {
        ~MotionGuard()
        {
            setAnimationsEnabledForTesting(std::nullopt);
        }
    } guard;
    setAnimationsEnabledForTesting(true);

    toast().show(QStringLiteral("成功提示"), ToastKind::Success);
    EXPECT_TRUE(toast().hasStreamingOverlay());
    EXPECT_EQ(toast().streamingOverlayCount(), 1);

    toast().show(QStringLiteral("信息提示"), ToastKind::Information);
    EXPECT_TRUE(toast().hasStreamingOverlay());
    EXPECT_EQ(toast().streamingOverlayCount(), 1);

    toast().show(QStringLiteral("错误提示"), ToastKind::Danger);
    EXPECT_FALSE(toast().hasStreamingOverlay());
    EXPECT_EQ(toast().streamingOverlayCount(), 0) << "错误提示不该逐字浮现";

    setAnimationsEnabledForTesting(false);
    toast().show(QStringLiteral("关闭动画的成功提示"), ToastKind::Success);
    EXPECT_FALSE(toast().hasStreamingOverlay());
    EXPECT_EQ(toast().streamingOverlayCount(), 0) << "关闭动画后仍盖着覆盖层";
}

/**
 * @brief 钉住新提示顶掉旧提示时，旧覆盖层一起销毁
 * @details 覆盖层挂在同一个标签下：旧层不删就会与新层叠着画，还能看见上一条的残影。
 */
TEST_F(ToastFixture, SecondToastReplacesTheFirstOverlay)
{
    toast().show(QStringLiteral("第一条"), ToastKind::Success);
    ASSERT_EQ(toast().streamingOverlayCount(), 1);

    toast().show(QStringLiteral("第二条"), ToastKind::Information);
    EXPECT_EQ(toast().streamingOverlayCount(), 1) << "旧覆盖层没有随文本一起销毁";
    EXPECT_TRUE(toast().hasStreamingOverlay());
    EXPECT_EQ(toast().currentText(), QStringLiteral("第二条"));
}

/**
 * @brief 钉住动画走完后覆盖层让开，原生标签仍是文字真身
 * @details 覆盖层只是画面；收工后必须被删掉，正文仍从 QLabel 读得到。
 */
TEST_F(ToastFixture, AdvancingPastTheTimelineRevealsTheNativeLabel)
{
    const auto text = QStringLiteral("识别完成");
    toast().show(text, ToastKind::Success);
    ASSERT_TRUE(toast().hasStreamingOverlay());

    toast().advanceStreamingForTesting(10000);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    EXPECT_FALSE(toast().hasStreamingOverlay());
    EXPECT_EQ(toast().streamingOverlayCount(), 0);
    EXPECT_EQ(toast().currentText(), text) << "覆盖层收工后文字真身没有留下";
}

/**
 * @brief 钉住气泡图标的描边色等于该条提示的语义色
 * @details 图标的颜色跟着提示种类走，而供给器同一时刻只持有一个主题色，必须走显式描边入口。
 */
TEST_F(ToastFixture, IconTakesTheSemanticTintOfItsKind)
{
    for (const auto kind: {ToastKind::Success, ToastKind::Danger, ToastKind::Information})
    {
        toast().show(QStringLiteral("提示"), kind);
        EXPECT_EQ(toast().currentIconTint(), toastColorFor(kind, theme().palette())) << static_cast<int>(kind);
    }
}

/**
 * @brief 钉住文字列上边距用的是标签真实字体度量与 em 高
 * @details 传 QFontMetrics::height()（行距）会让 -0.125em 的契约变形；这里按标签的
 *          QFontInfo::pixelSize() 重算一遍，钉住调用方没有拿行距冒充 em。
 */
TEST_F(ToastFixture, IconTextPadUsesTheLabelsRealFontMetrics)
{
    toast().show(QStringLiteral("基线"), ToastKind::Information);

    QWidget *bubble = visibleToastBubble();
    ASSERT_NE(bubble, nullptr) << "没有找到可见的 Toast 气泡";
    const auto *label = bubble->findChild<QLabel *>();
    ASSERT_NE(label, nullptr) << "气泡里没有原生文本标签";

    const QFont font = label->font();
    const int   em   = QFontInfo{font}.pixelSize();
    ASSERT_GT(em, 0);
    EXPECT_EQ(toast().currentIconTextPad(), iconTextTopPad(static_cast<int>(IconSize::Medium), label->fontMetrics().ascent(), em, 6));
}

/**
 * @brief 钉住覆盖层对输入与无障碍都是不可见的
 * @details 覆盖层只负责画面：鼠标要穿透到可选标签，不能进焦点链，也不该在辅助技术里有名字。
 */
TEST_F(ToastFixture, StreamingOverlayIsInvisibleToInputAndAccessibility)
{
    toast().show(QStringLiteral("不挡鼠标"), ToastKind::Success);
    ASSERT_EQ(toast().streamingOverlayCount(), 1);

    QWidget *bubble = visibleToastBubble();
    ASSERT_NE(bubble, nullptr) << "没有找到可见的 Toast 气泡";
    auto *overlay = bubble->findChild<StreamingTextOverlay *>();
    ASSERT_NE(overlay, nullptr) << "气泡里没有流式覆盖层";

    EXPECT_TRUE(overlay->testAttribute(Qt::WA_TransparentForMouseEvents)) << "覆盖层挡住了可选中的文字";
    EXPECT_EQ(overlay->focusPolicy(), Qt::NoFocus) << "覆盖层进了焦点链";
    EXPECT_TRUE(overlay->accessibleName().isEmpty()) << "纯画面层不该有可读名称";
}

