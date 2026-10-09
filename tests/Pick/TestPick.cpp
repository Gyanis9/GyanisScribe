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
using GyanisScribe::Pick::colorFormatToken;
using GyanisScribe::Pick::ColorPickerOverlay;
using GyanisScribe::Pick::formatColor;
using GyanisScribe::Pick::framePixelFor;
using GyanisScribe::Pick::indexOfColorFormat;
using GyanisScribe::Pick::kMagnifierSampleEdge;
using GyanisScribe::Pick::magnifierLayoutFor;
using GyanisScribe::Pick::parseColorFormat;
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
            int   argumentCount  = 1;
            char  argumentZero[] = "TestPick";
            char *arguments[]    = {argumentZero, nullptr};
            application          = std::make_unique<QApplication>(argumentCount, arguments);
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
    constexpr std::array<std::tuple<ColorFormat, std::string_view, std::string_view>, 3> expected = {
            std::tuple{ColorFormat::Hex, std::string_view{"Hex"}, std::string_view{"HEX"}},
            std::tuple{ColorFormat::Rgb, std::string_view{"Rgb"}, std::string_view{"RGB"}},
            std::tuple{ColorFormat::Hsl, std::string_view{"Hsl"}, std::string_view{"HSL"}},
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

    EXPECT_FALSE(colorFormatAt(3).has_value()) << "越界要给出空，让调用方跳过而不是写坏配置";
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
    EXPECT_EQ(colorFormatLabel(ColorFormat::Hex), QStringLiteral("HEX"));
    EXPECT_EQ(colorFormatLabel(ColorFormat::Hsl), QStringLiteral("HSL"));

    for (const auto format: {ColorFormat::Hex, ColorFormat::Rgb, ColorFormat::Hsl})
    {
        const auto parsed = parseColorFormat(colorFormatToken(format));
        ASSERT_TRUE(static_cast<bool>(parsed));
        EXPECT_EQ(*parsed, format);
    }

    // 大小写敏感：把 hex 也认下来，用户手写的 Hsl 与 HSL 就会两套并存
    const auto rejected = parseColorFormat("hex");
    ASSERT_FALSE(static_cast<bool>(rejected));
    EXPECT_NE(rejected.error().message.find("Hex"), std::string::npos);

    const QColor sample{60, 120, 200};
    EXPECT_EQ(formatColor(sample, ColorFormat::Hex), QStringLiteral("#3C78C8"));
    EXPECT_EQ(formatColor(sample, ColorFormat::Rgb), QStringLiteral("rgb(60, 120, 200)"));
    EXPECT_EQ(formatColor(sample, ColorFormat::Hsl), QStringLiteral("hsl(214, 56%, 51%)"));

    EXPECT_EQ(formatColor(QColor{0, 0, 0}, ColorFormat::Hsl), QStringLiteral("hsl(0, 0%, 0%)"));
    EXPECT_EQ(formatColor(QColor{255, 255, 255}, ColorFormat::Hsl), QStringLiteral("hsl(0, 0%, 100%)"));
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
