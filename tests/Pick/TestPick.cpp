#include "Pick/ColorFormat.h"
#include "Pick/ColorPickerOverlay.h"
#include "Pick/PickGeometry.h"

#include "Ui/DesignTokens.h"
#include "Ui/ThemeManager.h"
#include "Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QPoint>
#include <QRect>

#include <filesystem>
#include <format>
#include <memory>
#include <optional>

using GyanisScribe::Core::CapturedImage;
using GyanisScribe::Pick::ColorFormat;
using GyanisScribe::Pick::colorFormatAt;
using GyanisScribe::Pick::colorFormatLabel;
using GyanisScribe::Pick::colorFormatLabels;
using GyanisScribe::Pick::colorFormatOrDefault;
using GyanisScribe::Pick::colorFormatToken;
using GyanisScribe::Pick::ColorPickerOverlay;
using GyanisScribe::Pick::formatColor;
using GyanisScribe::Pick::framePixelFor;
using GyanisScribe::Pick::indexOfColorFormat;
using GyanisScribe::Pick::kMagnifierSampleEdge;
using GyanisScribe::Pick::magnifierLayoutFor;
using GyanisScribe::Pick::parseColorFormat;
using GyanisScribe::Pick::recentSwatchRects;
using GyanisScribe::Settings::SettingsStore;
using GyanisScribe::Ui::ThemeManager;

namespace
{
    std::unique_ptr<QApplication> application;

    /// 造一帧纯色底图，再在指定像素上涂一块可辨的颜色
    CapturedImage makeFrame(const QSize &imageSize, const qreal devicePixelRatio, const QPoint &markPixel, const QColor &markColor)
    {
        CapturedImage frame;
        frame.image = QImage{imageSize, QImage::Format_ARGB32};
        frame.image.fill(QColor{20, 40, 60});
        if (imageSize.isValid() && markPixel.x() < imageSize.width() && markPixel.y() < imageSize.height())
        {
            frame.image.setPixelColor(markPixel.x(), markPixel.y(), markColor);
        }
        frame.logicalSize      = {static_cast<int>(imageSize.width() / devicePixelRatio), static_cast<int>(imageSize.height() / devicePixelRatio)};
        frame.devicePixelRatio = devicePixelRatio;
        frame.screenName       = QStringLiteral("TestScreen");
        return frame;
    }

    class PickFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestPick";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            // 主题管理器要一份真配置：路径按用例名给，避免并行时互相刷掉档位
            m_iniPath = std::filesystem::temp_directory_path() / std::format("GyanisScribe-Pick-{}.ini", ::testing::UnitTest::GetInstance()->current_test_info()->name());
            std::error_code cleanup;
            std::filesystem::remove(m_iniPath, cleanup);
            m_store = std::make_unique<SettingsStore>(m_iniPath);
            m_theme = std::make_unique<ThemeManager>(*m_store);
        }

        void TearDown() override
        {
            std::error_code cleanup;
            std::filesystem::remove(m_iniPath, cleanup);
        }

        [[nodiscard]] std::unique_ptr<ColorPickerOverlay> makeOverlay(const CapturedImage &frame, const QRect &geometry, const ColorFormat format) const
        {
            return std::make_unique<ColorPickerOverlay>(frame, geometry, format, *m_theme);
        }

        std::filesystem::path          m_iniPath; ///< 本用例的设置文件路径，收尾删掉
        std::unique_ptr<SettingsStore> m_store;   ///< 本用例的配置
        std::unique_ptr<ThemeManager>  m_theme;   ///< 遮罩按它取主色画边框
    };

} // namespace

/**
 * @brief 钉住设置页下拉的「序号—写法—标记—界面名」四者互指
 * @details 期望值按下标逐个写死。下拉框按标签出题、按标记写配置，
 *          序号与写法一旦错位，用户选 HSL 存进去的却是 Hex，而界面看起来一切正常。
 */
TEST(PickColorFormat, IndexTokenAndLabelPointAtEachOther)
{
    constexpr std::array<std::tuple<ColorFormat, std::string_view, std::string_view>, 6> expected = {
            std::tuple{ColorFormat::Hex, std::string_view{"Hex"}, std::string_view{"HEX"}},
            std::tuple{ColorFormat::Rgb, std::string_view{"Rgb"}, std::string_view{"RGB"}},
            std::tuple{ColorFormat::Hsl, std::string_view{"Hsl"}, std::string_view{"HSL"}},
            std::tuple{ColorFormat::Hsv, std::string_view{"Hsv"}, std::string_view{"HSV"}},
            std::tuple{ColorFormat::Hsi, std::string_view{"Hsi"}, std::string_view{"HSI"}},
            std::tuple{ColorFormat::Decimal, std::string_view{"Decimal"}, std::string_view{"十进制"}},
    };

    const auto labels = colorFormatLabels();
    ASSERT_EQ(static_cast<std::size_t>(labels.size()), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const auto format = colorFormatAt(static_cast<int>(index));
        ASSERT_TRUE(static_cast<bool>(format)) << index;
        EXPECT_EQ(*format, std::get<0>(expected[index])) << index;
        EXPECT_EQ(colorFormatToken(*format), std::get<1>(expected[index])) << index;
        EXPECT_EQ(labels[static_cast<int>(index)].toStdString(), std::get<2>(expected[index])) << index;
        EXPECT_EQ(indexOfColorFormat(*format), static_cast<int>(index)) << index;
    }

    EXPECT_FALSE(colorFormatAt(6).has_value()) << "越界要给出空，让调用方跳过而不是写坏配置";
    EXPECT_FALSE(colorFormatAt(-1).has_value());
}

/**
 * @brief 钉住三种写法的存储标记、界面名与换算
 * @details 期望值手算：#3C78C8 就是 (60,120,200)；HSL 由 max=200/60、min=60/255 推出
 *          色相 214°、饱和 56%、亮度 51%。配置里存 Hex 而界面显示 HEX，
 *          两者混用会让改一次大小写变成换一次配置格式。
 */
TEST(PickColorFormat, TokensLabelsAndHandComputedTexts)
{
    EXPECT_EQ(colorFormatToken(ColorFormat::Hex), "Hex");
    EXPECT_EQ(colorFormatToken(ColorFormat::Rgb), "Rgb");
    EXPECT_EQ(colorFormatToken(ColorFormat::Hsl), "Hsl");
    EXPECT_EQ(colorFormatToken(ColorFormat::Hsv), "Hsv");
    EXPECT_EQ(colorFormatToken(ColorFormat::Hsi), "Hsi");
    EXPECT_EQ(colorFormatToken(ColorFormat::Decimal), "Decimal");
    EXPECT_EQ(colorFormatLabel(ColorFormat::Hex), QStringLiteral("HEX"));
    EXPECT_EQ(colorFormatLabel(ColorFormat::Hsl), QStringLiteral("HSL"));
    EXPECT_EQ(colorFormatLabel(ColorFormat::Hsv), QStringLiteral("HSV"));
    EXPECT_EQ(colorFormatLabel(ColorFormat::Hsi), QStringLiteral("HSI"));
    EXPECT_EQ(colorFormatLabel(ColorFormat::Decimal), QStringLiteral("十进制"));

    for (const auto format: {ColorFormat::Hex, ColorFormat::Rgb, ColorFormat::Hsl, ColorFormat::Hsv, ColorFormat::Hsi, ColorFormat::Decimal})
    {
        const auto parsed = parseColorFormat(colorFormatToken(format));
        ASSERT_TRUE(static_cast<bool>(parsed));
        EXPECT_EQ(*parsed, format);
    }

    // 大小写敏感：把 hex 也认下来，用户手写的 Hsl 与 HSL 就会两套并存
    const auto rejected = parseColorFormat("hex");
    ASSERT_FALSE(static_cast<bool>(rejected));
    EXPECT_NE(rejected.error().message.find("Hex"), std::string::npos);

    // 手算第二组：#3C78C8 的 HSV 是 max=200、chroma=140 → v=78%、s=140/200=70%；
    // HSI 的 i=(60+120+200)/3=126.7 → 50%、s=1-60/126.7=53%
    const QColor sample{60, 120, 200};
    EXPECT_EQ(formatColor(sample, ColorFormat::Hex), QStringLiteral("#3C78C8"));
    EXPECT_EQ(formatColor(sample, ColorFormat::Rgb), QStringLiteral("rgb(60, 120, 200)"));
    EXPECT_EQ(formatColor(sample, ColorFormat::Hsl), QStringLiteral("hsl(214, 56%, 51%)"));
    EXPECT_EQ(formatColor(sample, ColorFormat::Hsv), QStringLiteral("hsv(214, 70%, 78%)"));
    EXPECT_EQ(formatColor(sample, ColorFormat::Hsi), QStringLiteral("hsi(214, 53%, 50%)"));
    EXPECT_EQ(formatColor(sample, ColorFormat::Decimal), QStringLiteral("60, 120, 200"));

    EXPECT_EQ(formatColor(QColor{0, 0, 0}, ColorFormat::Hsl), QStringLiteral("hsl(0, 0%, 0%)"));
    EXPECT_EQ(formatColor(QColor{255, 255, 255}, ColorFormat::Hsl), QStringLiteral("hsl(0, 0%, 100%)"));
    EXPECT_EQ(formatColor(QColor{0, 0, 0}, ColorFormat::Hsv), QStringLiteral("hsv(0, 0%, 0%)")) << "纯黑的无彩色相按 0 记，饱和 0";
    EXPECT_EQ(formatColor(QColor{0, 0, 0}, ColorFormat::Hsi), QStringLiteral("hsi(0, 0%, 0%)")) << "I 为 0 时饱和按 0 记，不许除零";
    EXPECT_EQ(formatColor(QColor{255, 255, 255}, ColorFormat::Hsi), QStringLiteral("hsi(0, 0%, 100%)"));
    EXPECT_EQ(formatColor(QColor{255, 0, 0}, ColorFormat::Hsi), QStringLiteral("hsi(0, 100%, 33%)")) << "纯红 i=85/255→33%，s=1-0/i=100%";
}

/**
 * @brief 钉住「读设置里的写法」这一处的回落：坏值按 HEX 顶上，并把原因原样交回
 * @details 两个取色入口（屏幕遮罩与贴图）共用这一处：各自抄一份回落，一边改了默认写法
 *          另一边不会跟着动，用户在两处取到的写法就会不一样。
 */
TEST(PickColorFormat, StoredTokenFallsBackToHexWithAComplaint)
{
    std::string complaint;
    EXPECT_EQ(colorFormatOrDefault("Hsl", complaint), ColorFormat::Hsl);
    EXPECT_TRUE(complaint.empty()) << "认得出来的标记不该带出原因：" << complaint;

    EXPECT_EQ(colorFormatOrDefault("hex", complaint), ColorFormat::Hex) << "坏值要按 HEX 顶上，而不是让取色没法进行";
    EXPECT_FALSE(complaint.empty()) << "坏值没有把原因交回：界面上就没法告诉用户设置里那一格坏了";
    EXPECT_NE(complaint.find("Hex"), std::string::npos) << "原因里要点出可用取值：" << complaint;

    // 再来一次：上一次的原因要清掉，不然调用方会把陈年旧账报给用户
    EXPECT_EQ(colorFormatOrDefault("Rgb", complaint), ColorFormat::Rgb);
    EXPECT_TRUE(complaint.empty()) << "成功后原因没清空，调用方会拿上一次那条去弹提示";
}

/**
 * @brief 钉住放大镜的取样框、格子大小与翻边
 * @details 期望值全部手算：1000x800 屏、光标 (500,400)、取 9 格、显示 180
 *          → 取样 (496,396,9,9)、每格 20、摆在 (518,418,180,180)。
 *          右下角那组则必须翻到光标左上方，否则放大镜整块跑出可用区，用户看到的是空白。
 */
TEST(PickGeometry, MagnifierSamplesCentredAndFlipsAtEdges)
{
    const QRect screen{0, 0, 1000, 800};

    const auto middle = magnifierLayoutFor({500, 400}, screen, screen, kMagnifierSampleEdge, 180);
    EXPECT_EQ(middle.sourceRect, QRect(496, 396, 9, 9));
    EXPECT_EQ(middle.cellSize, 20);
    EXPECT_EQ(middle.viewRect, QRect(518, 418, 180, 180));

    const auto corner = magnifierLayoutFor({950, 750}, screen, screen, kMagnifierSampleEdge, 180);
    EXPECT_EQ(corner.sourceRect, QRect(946, 746, 9, 9));
    EXPECT_EQ(corner.viewRect, QRect(752, 552, 180, 180));

    // 贴着最右下角：取样框整体夹进屏内，不回绕到屏幕另一头
    const auto clamped = magnifierLayoutFor({999, 799}, screen, screen, kMagnifierSampleEdge, 180);
    EXPECT_EQ(clamped.sourceRect, QRect(991, 791, 9, 9));
    EXPECT_TRUE(screen.contains(clamped.sourceRect)) << "取样框必须整块在屏内";

    const auto indivisible = magnifierLayoutFor({100, 100}, screen, screen, 7, 100);
    EXPECT_EQ(indivisible.cellSize, 14) << "除不尽时按整数格算，不留半格";
    EXPECT_EQ(indivisible.viewRect.size(), QSize(98, 98));

    EXPECT_TRUE(magnifierLayoutFor({10, 10}, screen, screen, 0, 180).sourceRect.isEmpty());
    EXPECT_TRUE(magnifierLayoutFor({10, 10}, screen, screen, 9, 0).sourceRect.isEmpty());
    EXPECT_TRUE(magnifierLayoutFor({10, 10}, QRect{}, QRect{}, 9, 180).sourceRect.isEmpty());
}

/**
 * @brief 钉住屏幕坐标到抓帧像素的换算，含高 DPI 与副屏
 * @details 1.5 倍屏上 100 逻辑像素是 150 设备像素：只减屏幕原点不乘缩放比，
 *          取到的颜色会整体偏到左上角，而这在放大镜里看着完全正常。
 */
TEST(PickGeometry, FramePixelAccountsForScaleAndScreenOrigin)
{
    EXPECT_EQ(framePixelFor({10, 20}, {0, 0, 1000, 800}, 1.0, {1000, 800}), QPoint(10, 20));
    EXPECT_EQ(framePixelFor({100, 200}, {0, 0, 1000, 800}, 1.5, {1500, 1200}), QPoint(150, 300));

    // 副屏原点不是 (0,0)：先减掉原点再乘缩放比
    EXPECT_EQ(framePixelFor({1920 + 50, 60}, {1920, 0, 1080, 1080}, 1.0, {1080, 1080}), QPoint(50, 60));

    // 越界夹到最后一格，而不是回绕或报「参数非法」
    EXPECT_EQ(framePixelFor({3000, 3000}, {1920, 0, 1080, 1080}, 1.0, {1080, 1080}), QPoint(1079, 1079));
    EXPECT_EQ(framePixelFor({1910, -20}, {1920, 0, 1080, 1080}, 1.0, {1080, 1080}), QPoint(0, 0));

    EXPECT_TRUE(framePixelFor({10, 10}, {0, 0, 100, 100}, 0.0, {100, 100}).isNull());
    EXPECT_TRUE(framePixelFor({10, 10}, {0, 0, 100, 100}, 1.0, QSize{}).isNull());
}

/**
 * @brief 钉住遮罩按屏幕坐标取色，且写法与 colorPicked 同源
 * @details 点下去抄到的颜色必须就是放大镜中心那一格：偏移一格在高 DPI 下就是十几像素的差。
 */
TEST_F(PickFixture, OverlayPicksThePixelUnderTheCursor)
{
    const auto mark    = QColor{200, 30, 40};
    const auto frame   = makeFrame({200, 200}, 2.0, {100, 100}, mark);
    auto       overlay = makeOverlay(frame, {0, 0, 100, 100}, ColorFormat::Hex);

    EXPECT_EQ(overlay->colorAt({50, 50}), mark) << "100,100 是 2 倍屏上 (50,50) 那块像素";
    EXPECT_EQ(overlay->colorAt({0, 0}), QColor(20, 40, 60));

    // currentText 说的是「光标那格」的文本：构造时用的是真实鼠标位置，用例必须先把光标挪过去
    QMouseEvent move{QEvent::MouseMove, QPointF{50.0, 50.0}, QPointF{50.0, 50.0}, Qt::NoButton, Qt::NoButton, Qt::NoModifier};
    QApplication::sendEvent(overlay.get(), &move);
    EXPECT_EQ(overlay->currentText(), QStringLiteral("#C81E28")) << "文本按当前写法排好";
}

/**
 * @brief 钉住左键取色、Esc 与右键放弃
 * @details 遮罩盖住整屏：它不响应 Esc 就等于把用户的屏幕锁住，必须两条出口都有。
 */
TEST_F(PickFixture, LeftClickPicksWhileEscapeAndRightClickCancel)
{
    const auto frame   = makeFrame({100, 100}, 1.0, {30, 40}, QColor{10, 200, 20});
    auto       overlay = makeOverlay(frame, {0, 0, 100, 100}, ColorFormat::Rgb);

    int     pickedCount    = 0;
    int     cancelledCount = 0;
    QColor  pickedColor;
    QString pickedText;
    auto   *pointer = overlay.get();
    QObject::connect(pointer, &ColorPickerOverlay::colorPicked, pointer,
                     [&pickedCount, &pickedColor, &pickedText](const QColor &color, const QString &text)
                     {
                         ++pickedCount;
                         pickedColor = color;
                         pickedText  = text;
                     });
    QObject::connect(pointer, &ColorPickerOverlay::cancelled, pointer, [&cancelledCount] { ++cancelledCount; });

    // 光标先移到 (30,40)：移动事件决定放大镜中心，也决定点下去取哪一格
    const QPointF local{30.0, 40.0};
    const QPointF global{30.0, 40.0};
    QMouseEvent   move{QEvent::MouseMove, local, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier};
    QApplication::sendEvent(pointer, &move);
    EXPECT_EQ(pointer->cursor(), QPoint(30, 40));

    QMouseEvent click{QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(pointer, &click);
    EXPECT_EQ(pickedCount, 1);
    EXPECT_EQ(pickedColor, QColor(10, 200, 20));
    EXPECT_EQ(pickedText, QStringLiteral("rgb(10, 200, 20)"));

    QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
    QApplication::sendEvent(pointer, &escape);
    EXPECT_EQ(cancelledCount, 1) << "Esc 也要能退出遮罩";

    QMouseEvent rightClick{QEvent::MouseButtonPress, local, global, Qt::RightButton, Qt::RightButton, Qt::NoModifier};
    QApplication::sendEvent(pointer, &rightClick);
    EXPECT_EQ(cancelledCount, 2) << "右键等同放弃：用户按下去时不该以为取到了色";
    EXPECT_EQ(pickedCount, 1) << "右键不该顺带复制一个颜色";
}

/**
 * @brief 钉住最近取色色块行的几何：居中、等距、贴底，摆不下就按可摆数量截断
 * @details 期望值手算：1000×800 屏摆 3 块（边长 28、间距 6、两侧各留 28）
 *          → 行宽 96、左起 452、顶边 743。命中测试与绘制共用这一处，各自算一遍就会点不中。
 */
TEST(PickGeometry, RecentSwatchesCenterAtTheBottom)
{
    const auto three = recentSwatchRects(QRect{0, 0, 1000, 800}, 3);
    ASSERT_EQ(three.size(), 3U);
    EXPECT_EQ(three[0], QRect(452, 743, 28, 28));
    EXPECT_EQ(three[1], QRect(486, 743, 28, 28));
    EXPECT_EQ(three[2], QRect(520, 743, 28, 28));

    // 副屏原点不是 (0,0)：整行跟着屏走
    const auto two = recentSwatchRects(QRect{1920, 0, 800, 600}, 2);
    ASSERT_EQ(two.size(), 2U);
    EXPECT_EQ(two[0], QRect(2289, 543, 28, 28));
    EXPECT_EQ(two[1], QRect(2323, 543, 28, 28));

    // 太窄的屏：能摆几块摆几块，一块都摆不下就交空
    const auto one = recentSwatchRects(QRect{0, 0, 90, 200}, 3);
    ASSERT_EQ(one.size(), 1U) << "90 宽只摆得下一块";
    EXPECT_EQ(one[0], QRect(31, 143, 28, 28));
    EXPECT_TRUE(recentSwatchRects(QRect{0, 0, 60, 200}, 3).empty()) << "60 宽一块都摆不下，不许画到屏外";

    EXPECT_TRUE(recentSwatchRects(QRect{0, 0, 1000, 800}, 0).empty());
    EXPECT_TRUE(recentSwatchRects(QRect{}, 3).empty());
}

/**
 * @brief 钉住色块行的取回：点色块取的是那一块的颜色，数字键同理，都不是取它底下的像素
 * @details 200×200 屏摆 2 块 → (69,143) 与 (103,143) 两块 28×28。点第一块拿到的必须是
 *          记录里的第一个颜色（红），而那个坐标底下的帧像素是底色——两件事必须分得开。
 */
TEST_F(PickFixture, RecentSwatchesPickTheStoredColorNotThePixelUnderneath)
{
    const auto frame   = makeFrame({200, 200}, 1.0, {83, 157}, QColor{10, 200, 20});
    auto       overlay = makeOverlay(frame, {0, 0, 200, 200}, ColorFormat::Hex);
    overlay->setRecentColors({QColor{255, 0, 0}, QColor{0, 0, 255}});

    std::vector<QColor>  picked;
    std::vector<QString> texts;
    auto                *pointer = overlay.get();
    QObject::connect(pointer, &ColorPickerOverlay::colorPicked, pointer,
                     [&picked, &texts](const QColor &color, const QString &text)
                     {
                         picked.push_back(color);
                         texts.push_back(text);
                     });

    // 第一块的中心点在 (83,157)：底下的帧像素是 (10,200,20)，取回的必须是记录里的红
    const QPointF center{83.0, 157.0};
    QMouseEvent   click{QEvent::MouseButtonPress, center, center, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
    QApplication::sendEvent(pointer, &click);
    ASSERT_EQ(picked.size(), 1U);
    EXPECT_EQ(picked[0], QColor(255, 0, 0)) << "取到的是底下的像素色，不是色块记录的颜色";
    EXPECT_EQ(texts[0], QStringLiteral("#FF0000"));

    // 数字键 2 取第二块（蓝）；3 没有对应块，不产出
    QKeyEvent second{QEvent::KeyPress, Qt::Key_2, Qt::NoModifier};
    QApplication::sendEvent(pointer, &second);
    ASSERT_EQ(picked.size(), 2U);
    EXPECT_EQ(picked[1], QColor(0, 0, 255));

    QKeyEvent third{QEvent::KeyPress, Qt::Key_3, Qt::NoModifier};
    QApplication::sendEvent(pointer, &third);
    EXPECT_EQ(picked.size(), 2U) << "没有第三块，数字键 3 不该产出颜色";
}

/**
 * @brief 钉住遮罩能画出一整屏且不崩
 * @details 放大镜、网格与信息条都在 paintEvent 里现算：坐标算错会画到屏外或直接不出现，
 *          grab() 是能在无界面环境下把这条抓住的最省事判据。
 */
TEST_F(PickFixture, OverlayPaintsTheWholeScreenWithoutCrashing)
{
    const auto frame   = makeFrame({200, 200}, 1.0, {10, 10}, QColor{255, 0, 0});
    auto       overlay = makeOverlay(frame, {0, 0, 200, 200}, ColorFormat::Hex);

    const QPixmap pixmap = overlay->grab();
    EXPECT_EQ(pixmap.size(), QSize(200, 200));
}
