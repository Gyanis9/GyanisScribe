#include "Core/Share/ShareRenderer.h"
#include "Core/Share/ShareStyle.h"

#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QRect>
#include <QSize>

#include <gtest/gtest.h>

#include <memory>

using namespace GyanisScribe::Core;

namespace
{
    [[nodiscard]] QImage makeSolidImage(const QSize size, const QColor color)
    {
        QImage image{size, QImage::Format_ARGB32_Premultiplied};
        image.fill(color);
        return image;
    }

    [[nodiscard]] ShareStyle flatStyle()
    {
        ShareStyle style;
        style.backgroundTop      = QColor{0x00, 0xFF, 0x00};
        style.backgroundBottom   = QColor{0x00, 0xFF, 0x00};
        style.gradient           = false;
        style.paddingPixels      = 10;
        style.cornerRadiusPixels = 0;
        style.border             = false;
        style.shadow             = false;
        style.shadowPixels       = 0;
        style.editorChrome       = false;
        style.watermarkText.clear();
        return style;
    }

    std::unique_ptr<QGuiApplication> application;

    /// 标题与水印都要画字，字体后端要求应用对象就位：没有它 QPainter::drawText 会一直等字体初始化，用例根本跑不完
    class ShareRendererEnvironment : public ::testing::Environment
    {
    public:
        void SetUp() override
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestShareRenderer";
            static char *arguments[]    = {argumentZero, nullptr};
            qputenv("QT_QPA_PLATFORM", "offscreen");
            application = std::make_unique<QGuiApplication>(argumentCount, arguments);
        }

        void TearDown() override
        {
            application.reset();
        }
    };
} // namespace

[[maybe_unused]] const auto *const kShareRendererEnvironment = ::testing::AddGlobalTestEnvironment(new ShareRendererEnvironment);

/// 纯色背景 + 直角卡片：画布 = 图 + 两侧内边距，卡片外是背景、卡片内是原图
TEST(ShareRenderer, DrawsSolidBackgroundAndCardGeometry)
{
    const auto image    = makeSolidImage(QSize{100, 50}, QColor{255, 0, 0});
    const auto rendered = renderShareImage(image, flatStyle());
    ASSERT_TRUE(rendered.has_value()) << rendered.error().message;
    EXPECT_EQ(rendered->size(), QSize(120, 70));
    EXPECT_EQ(rendered->pixelColor(0, 0), QColor(0, 255, 0));
    EXPECT_EQ(rendered->pixelColor(10, 10), QColor(255, 0, 0));
    EXPECT_EQ(rendered->devicePixelRatio(), 1.0);
}

/// 渐变从背景上色走到下色
TEST(ShareRenderer, GradientGoesTopToBottom)
{
    auto style             = flatStyle();
    style.gradient         = true;
    style.backgroundTop    = QColor{0, 0, 255};
    style.backgroundBottom = QColor{255, 0, 0};
    const auto rendered    = renderShareImage(makeSolidImage(QSize{20, 20}, QColor{0, 255, 0}), style);
    ASSERT_TRUE(rendered.has_value()) << rendered.error().message;
    EXPECT_GT(rendered->pixelColor(0, 0).blue(), rendered->pixelColor(0, 0).red());
    EXPECT_GT(rendered->pixelColor(0, rendered->height() - 1).red(), rendered->pixelColor(0, rendered->height() - 1).blue());
}

/// 圆角把卡片角上的原图切掉，露出背景
TEST(ShareRenderer, RoundsCardCorners)
{
    auto style               = flatStyle();
    style.paddingPixels      = 20;
    style.cornerRadiusPixels = 20;
    const auto rendered      = renderShareImage(makeSolidImage(QSize{100, 100}, QColor{255, 0, 0}), style);
    ASSERT_TRUE(rendered.has_value()) << rendered.error().message;
    EXPECT_EQ(rendered->size(), QSize(140, 140));
    const auto corner = rendered->pixelColor(20, 20);
    EXPECT_GT(corner.green(), corner.red()) << "圆角外的角格应是背景";
    EXPECT_EQ(rendered->pixelColor(70, 70), QColor(255, 0, 0));
}

/// 代码窗口标题栏占 44 像素，原图从它下面开始
TEST(ShareRenderer, DrawsEditorChromeAboveTheImage)
{
    auto style              = flatStyle();
    style.paddingPixels     = 0;
    style.editorChrome      = true;
    style.editorChromeColor = QColor{0x1C, 0x1F, 0x26};
    style.editorTitle       = QStringLiteral("snippet.cpp");
    const auto rendered     = renderShareImage(makeSolidImage(QSize{100, 50}, QColor{255, 0, 0}), style);
    ASSERT_TRUE(rendered.has_value()) << rendered.error().message;
    EXPECT_EQ(rendered->size(), QSize(100, 94));
    EXPECT_EQ(rendered->pixelColor(50, 10), QColor(0x1C, 0x1F, 0x26));
    EXPECT_EQ(rendered->pixelColor(50, 60), QColor(255, 0, 0));
}

/// 水印只在有文字时出现，且落在指定角落
TEST(ShareRenderer, DrawsWatermarkOnlyWhenRequested)
{
    auto style             = flatStyle();
    style.paddingPixels    = 20;
    style.watermarkCorner  = ShareWatermarkCorner::BottomRight;
    style.watermarkOpacity = 255;
    const auto plain       = renderShareImage(makeSolidImage(QSize{200, 100}, QColor{255, 0, 0}), style);
    ASSERT_TRUE(plain.has_value()) << plain.error().message;
    EXPECT_EQ(plain->pixelColor(200, 100), QColor(255, 0, 0));

    style.watermarkText = QStringLiteral("TEST");
    const auto marked   = renderShareImage(makeSolidImage(QSize{200, 100}, QColor(255, 0, 0)), style);
    ASSERT_TRUE(marked.has_value()) << marked.error().message;
    auto found = false;
    for (auto y = 80; y < 120 && !found; ++y)
    {
        for (auto x = 120; x < 220; ++x)
        {
            if (marked->pixelColor(x, y) != QColor(255, 0, 0))
            {
                found = true;
                break;
            }
        }
    }
    EXPECT_TRUE(found) << "水印开着却一个像素都没画";
}

/// 预览按最大宽度等比缩小；导出走原尺寸
TEST(ShareRenderer, PreviewScalesToMaximumDimension)
{
    auto style               = flatStyle();
    style.paddingPixels      = 40;
    style.shadow             = true;
    style.shadowPixels       = 20;
    style.cornerRadiusPixels = 20;
    const auto image         = makeSolidImage(QSize{2000, 1000}, QColor{255, 0, 0});
    const auto preview       = renderShareImage(image, style, 400);
    ASSERT_TRUE(preview.has_value()) << preview.error().message;
    EXPECT_EQ(preview->width(), 400);
    EXPECT_LT(preview->height(), 400);
    const auto full = renderShareImage(image, style, 0);
    ASSERT_TRUE(full.has_value()) << full.error().message;
    EXPECT_EQ(full->size(), shareCanvasSize(image.size(), style));
}

/// 空图、越界样式、超宽画布都拒绝
TEST(ShareRenderer, RejectsInvalidInputs)
{
    EXPECT_FALSE(renderShareImage(QImage{}, ShareStyle{}).has_value());
    auto bad          = flatStyle();
    bad.paddingPixels = -1;
    EXPECT_FALSE(renderShareImage(makeSolidImage(QSize{10, 10}, Qt::red), bad).has_value());
    EXPECT_FALSE(renderShareImage(makeSolidImage(QSize{32760, 1}, Qt::red), ShareStyle{}).has_value()) << "画布宽超过 32768 应拒绝，而不是分不出图才报错";
}

/// 渲染不动入参
TEST(ShareRenderer, DoesNotMutateTheSource)
{
    const auto image = makeSolidImage(QSize{40, 30}, QColor{1, 2, 3});
    const auto copy  = image.copy();
    ASSERT_TRUE(renderShareImage(image, flatStyle()).has_value());
    EXPECT_EQ(image, copy);
}
