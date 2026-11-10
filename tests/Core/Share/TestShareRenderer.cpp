#include "Core/Share/ShareRenderer.h"
#include "Core/Share/ShareStyle.h"

#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

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

    /// 矩形内是否有一处不是 expected 的像素
    [[nodiscard]] bool containsNonColor(const QImage &image, const QRect &rect, const QColor &expected)
    {
        const auto area = rect.intersected(image.rect());
        for (auto y = area.top(); y <= area.bottom(); ++y)
        {
            for (auto x = area.left(); x <= area.right(); ++x)
            {
                if (image.pixelColor(x, y) != expected)
                {
                    return true;
                }
            }
        }
        return false;
    }

    /// 像素中心是否落在圆角矩形外超过 margin 像素：圆角矩形等于内缩矩形与半径相同圆盘的闵可夫斯基和
    [[nodiscard]] bool outsideRoundedCard(const QPoint &point, const QRect &card, const double radius, const double margin)
    {
        const auto shape    = QRectF{card};
        const auto core     = shape.adjusted(radius, radius, -radius, -radius);
        const auto center   = QPointF{point.x() + 0.5, point.y() + 0.5};
        const auto nearestX = std::clamp(center.x(), core.left(), core.right());
        const auto nearestY = std::clamp(center.y(), core.top(), core.bottom());
        return std::hypot(center.x() - nearestX, center.y() - nearestY) > radius + margin;
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

/// 代码窗口标题栏占 44 像素，原图从它下面开始；三枚圆点与标题各就各位
TEST(ShareRenderer, DrawsEditorChromeAboveTheImage)
{
    auto style              = flatStyle();
    style.paddingPixels     = 0;
    style.editorChrome      = true;
    style.editorChromeColor = QColor{0x1C, 0x1F, 0x26};
    style.editorTitle       = QStringLiteral("snippet.cpp");
    const auto rendered     = renderShareImage(makeSolidImage(QSize{400, 100}, QColor{255, 0, 0}), style);
    ASSERT_TRUE(rendered.has_value()) << rendered.error().message;
    EXPECT_EQ(rendered->size(), QSize(400, 144));
    EXPECT_EQ(rendered->pixelColor(200, 10), QColor(0x1C, 0x1F, 0x26));
    EXPECT_EQ(rendered->pixelColor(200, 60), QColor(255, 0, 0));
    EXPECT_EQ(rendered->pixelColor(21, 22), QColor(0xFF, 0x5F, 0x57));
    EXPECT_EQ(rendered->pixelColor(39, 22), QColor(0xFE, 0xBC, 0x2E));
    EXPECT_EQ(rendered->pixelColor(57, 22), QColor(0x28, 0xC8, 0x40));
    EXPECT_TRUE(containsNonColor(*rendered, QRect{74, 8, 176, 28}, QColor{0x1C, 0x1F, 0x26})) << "标题文字没画进标题栏";
}

/// 水印只在有文字时出现，且落在指定角落
TEST(ShareRenderer, DrawsWatermarkOnlyWhenRequested)
{
    auto style             = flatStyle();
    style.paddingPixels    = 20;
    style.watermarkCorner  = ShareWatermarkCorner::BottomRight;
    style.watermarkOpacity = 255;
    const auto image       = makeSolidImage(QSize{200, 100}, QColor{255, 0, 0});
    const auto plain       = renderShareImage(image, style);
    ASSERT_TRUE(plain.has_value()) << plain.error().message;
    EXPECT_EQ(plain->pixelColor(200, 100), QColor(255, 0, 0));
    EXPECT_FALSE(containsNonColor(*plain, QRect{20, 20, 200, 100}, QColor(255, 0, 0))) << "没写水印却画了东西";

    style.watermarkText = QStringLiteral("TEST");
    const auto marked   = renderShareImage(image, style);
    ASSERT_TRUE(marked.has_value()) << marked.error().message;
    EXPECT_TRUE(containsNonColor(*marked, QRect{120, 70, 100, 50}, QColor(255, 0, 0))) << "右下角没有水印";
    EXPECT_FALSE(containsNonColor(*marked, QRect{20, 70, 100, 50}, QColor(255, 0, 0))) << "左下角不该有水印";
}

/// 水印不透明度为 0 时一个像素都不画，255 时出现
TEST(ShareRenderer, WatermarkFollowsOpacity)
{
    auto style             = flatStyle();
    style.paddingPixels    = 20;
    style.watermarkText    = QStringLiteral("TEST");
    style.watermarkOpacity = 0;
    const auto image       = makeSolidImage(QSize{200, 100}, QColor{255, 0, 0});
    const auto invisible   = renderShareImage(image, style);
    ASSERT_TRUE(invisible.has_value()) << invisible.error().message;
    EXPECT_FALSE(containsNonColor(*invisible, QRect{20, 20, 200, 100}, QColor(255, 0, 0))) << "透明度为 0 还画出了水印";

    style.watermarkOpacity = 255;
    const auto visible     = renderShareImage(image, style);
    ASSERT_TRUE(visible.has_value()) << visible.error().message;
    EXPECT_TRUE(containsNonColor(*visible, QRect{20, 20, 200, 100}, QColor(255, 0, 0))) << "透明度为 255 却没有水印";
}

/// 超宽水印按卡片可用宽度省略号截断：不越出卡片，也不顶到卡片左沿
TEST(ShareRenderer, WatermarkElidesLongTextInsideTheCard)
{
    auto style             = flatStyle();
    style.paddingPixels    = 20;
    style.watermarkCorner  = ShareWatermarkCorner::BottomRight;
    style.watermarkOpacity = 255;
    style.watermarkText    = QStringLiteral("W").repeated(kMaximumShareWatermarkLength);
    const auto rendered    = renderShareImage(makeSolidImage(QSize{200, 100}, QColor{255, 0, 0}), style);
    ASSERT_TRUE(rendered.has_value()) << rendered.error().message;

    const auto card       = QRect{20, 20, 200, 100};
    const auto background = QColor{0, 255, 0};
    auto       outsideInk = false;
    for (auto y = 0; y < rendered->height(); ++y)
    {
        for (auto x = 0; x < rendered->width(); ++x)
        {
            if (!card.contains(x, y) && rendered->pixelColor(x, y) != background)
            {
                outsideInk = true;
            }
        }
    }
    EXPECT_FALSE(outsideInk) << "水印画到了卡片外";
    EXPECT_FALSE(containsNonColor(*rendered, QRect{card.left(), card.top() + card.height() / 2, 8, card.height() / 2}, QColor(255, 0, 0)))
            << "水印没有按可用宽度截断，压到了卡片左沿";
    EXPECT_TRUE(containsNonColor(*rendered, card, QColor(255, 0, 0))) << "整段水印都没画";
}

/// 大圆角时水印仍被卡片裁剪路径兜住，不落进圆角外的缺口
TEST(ShareRenderer, WatermarkStaysInsideRoundedCard)
{
    auto style               = flatStyle();
    style.paddingPixels      = 20;
    style.cornerRadiusPixels = kMaximumShareCornerRadius;
    style.watermarkCorner    = ShareWatermarkCorner::BottomRight;
    style.watermarkOpacity   = 255;
    style.watermarkText      = QStringLiteral("美化").repeated(6);
    const auto rendered      = renderShareImage(makeSolidImage(QSize{400, 400}, QColor{255, 0, 0}), style);
    ASSERT_TRUE(rendered.has_value()) << rendered.error().message;
    EXPECT_EQ(rendered->size(), QSize(440, 440));

    const auto card           = QRect{20, 20, 400, 400};
    const auto background     = QColor{0, 255, 0};
    auto       paintedOutside = false;
    for (auto y = card.top(); y <= card.bottom(); ++y)
    {
        for (auto x = card.left(); x <= card.right(); ++x)
        {
            if (outsideRoundedCard(QPoint{x, y}, card, kMaximumShareCornerRadius, 1.0) && rendered->pixelColor(x, y) != background)
            {
                paintedOutside = true;
            }
        }
    }
    EXPECT_FALSE(paintedOutside) << "水印画到了卡片圆角外";
    EXPECT_TRUE(containsNonColor(*rendered, QRect{card.left() + card.width() / 2, card.bottom() - 64, card.width() / 2 - 16, 64}, QColor(255, 0, 0))) << "水印没画在右下角";
}

/// 预览按最大宽度等比缩小，内外边距与高度都取整到固定值；导出走原尺寸
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
    // 画布 2120x1120，缩放 400/2120：外圈 60→11，图 2000x1000→378x189，画布高度 189+22
    EXPECT_EQ(preview->size(), QSize(400, 211));
    EXPECT_EQ(preview->pixelColor(11, 105), QColor(255, 0, 0)) << "卡片左沿应在 x=11";
    EXPECT_NE(preview->pixelColor(10, 105), QColor(255, 0, 0)) << "x=10 还在留白里";
    EXPECT_EQ(preview->pixelColor(200, 11), QColor(255, 0, 0)) << "卡片上沿应在 y=11";
    EXPECT_NE(preview->pixelColor(200, 10), QColor(255, 0, 0)) << "y=10 还在留白里";
    const auto full = renderShareImage(image, style, 0);
    ASSERT_TRUE(full.has_value()) << full.error().message;
    EXPECT_EQ(full->size(), shareCanvasSize(image.size(), style));
}

/// 预览缩到极小时先把外圈夹进画布，不能返回一张卡片在画布外的成品
TEST(ShareRenderer, KeepsCardInsideDegeneratePreview)
{
    auto style          = flatStyle();
    style.paddingPixels = 40;
    style.shadow        = true;
    style.shadowPixels  = 20;
    const auto image    = makeSolidImage(QSize{2000, 1000}, QColor{255, 0, 0});

    const auto one = renderShareImage(image, style, 1);
    ASSERT_TRUE(one.has_value()) << one.error().message;
    EXPECT_EQ(one->size(), QSize(1, 1));
    EXPECT_EQ(one->pixelColor(0, 0), QColor(255, 0, 0)) << "外圈没夹住，卡片被挤出了画布";

    const auto two = renderShareImage(image, style, 2);
    ASSERT_TRUE(two.has_value()) << two.error().message;
    EXPECT_EQ(two->width(), 2);
    EXPECT_EQ(two->pixelColor(0, 0), QColor(255, 0, 0)) << "外圈没夹住，卡片被挤出了画布";

    const auto three = renderShareImage(image, style, 3);
    ASSERT_TRUE(three.has_value()) << three.error().message;
    EXPECT_EQ(three->size(), QSize(3, 3));
    EXPECT_EQ(three->pixelColor(1, 1), QColor(255, 0, 0)) << "外圈应缩到 1 像素，卡片从 (1, 1) 起";
}

/// 投影开关开着但宽度为 0 时不画那一圈：画布也没为它留位置
TEST(ShareRenderer, SkipsShadowWhenWidthIsZero)
{
    auto style          = flatStyle();
    style.paddingPixels = 20;
    style.shadow        = true;
    style.shadowPixels  = 0;
    const auto rendered = renderShareImage(makeSolidImage(QSize{100, 50}, QColor{255, 0, 0}), style);
    ASSERT_TRUE(rendered.has_value()) << rendered.error().message;
    EXPECT_EQ(rendered->size(), QSize(140, 90));
    EXPECT_EQ(rendered->pixelColor(20, 19), QColor(0, 255, 0)) << "零宽投影不该在卡片上沿外再画一圈";
    EXPECT_EQ(rendered->pixelColor(119, 19), QColor(0, 255, 0)) << "零宽投影不该在卡片上沿外再画一圈";
}

/// 1 像素描边整圈落在卡片内侧，卡片外一格仍是背景
TEST(ShareRenderer, BorderStaysInsideCardEdge)
{
    auto style          = flatStyle();
    style.paddingPixels = 20;
    style.border        = true;
    style.borderPixels  = 1;
    style.borderColor   = QColor{0, 0, 255};
    const auto rendered = renderShareImage(makeSolidImage(QSize{200, 100}, QColor{255, 0, 0}), style);
    ASSERT_TRUE(rendered.has_value()) << rendered.error().message;
    EXPECT_EQ(rendered->size(), QSize(240, 140));
    EXPECT_EQ(rendered->pixelColor(120, 20), QColor(0, 0, 255)) << "上边描边应占卡片第一行";
    EXPECT_EQ(rendered->pixelColor(120, 19), QColor(0, 255, 0)) << "上边外一格不该有描边";
    EXPECT_EQ(rendered->pixelColor(20, 70), QColor(0, 0, 255)) << "左边描边应占卡片第一列";
    EXPECT_EQ(rendered->pixelColor(19, 70), QColor(0, 255, 0)) << "左边外一格不该有描边";
    EXPECT_EQ(rendered->pixelColor(219, 70), QColor(0, 0, 255)) << "右边描边应占卡片最后一列";
    EXPECT_EQ(rendered->pixelColor(220, 70), QColor(0, 255, 0)) << "右边外一格不该有描边";
}

/// 四套内置模板各画一张：背景互不相同，卡片里是原图，代码窗口另有标题栏
TEST(ShareRenderer, DrawsEveryBuiltInTemplate)
{
    const auto          image = makeSolidImage(QSize{60, 40}, QColor{255, 0, 0});
    std::vector<QColor> backgrounds;
    for (const auto &shareTemplate: builtInShareTemplates())
    {
        const auto rendered = renderShareImage(image, shareTemplate.style);
        ASSERT_TRUE(rendered.has_value()) << shareTemplate.id.toStdString() << ": " << rendered.error().message;
        EXPECT_EQ(rendered->size(), shareCanvasSize(image.size(), shareTemplate.style)) << shareTemplate.id.toStdString();

        const auto background = rendered->pixelColor(0, 0);
        backgrounds.push_back(background);
        const auto outer  = shareTemplate.style.paddingPixels + (shareTemplate.style.shadow ? shareTemplate.style.shadowPixels : 0);
        const auto chrome = shareTemplate.style.editorChrome ? kShareEditorChromeHeight : 0;
        const auto card   = QRect{outer, outer, image.width(), image.height() + chrome};
        EXPECT_EQ(rendered->pixelColor(card.left() + card.width() / 2, card.top() + chrome + image.height() / 2), QColor(255, 0, 0)) << shareTemplate.id.toStdString();

        if (shareTemplate.id == QStringLiteral("light"))
        {
            EXPECT_EQ(background, QColor(0xF5, 0xF6, 0xF8));
        } else if (shareTemplate.id == QStringLiteral("dark"))
        {
            EXPECT_EQ(background, QColor(0x1E, 0x1F, 0x22));
        } else if (shareTemplate.id == QStringLiteral("gradient"))
        {
            EXPECT_GT(background.blue(), 200) << "渐变上沿应是「渐变」模板的起点紫";
            EXPECT_GT(background.red(), background.green()) << "渐变上沿偏紫，红色分量应高于绿色";
        } else if (shareTemplate.id == QStringLiteral("code"))
        {
            EXPECT_EQ(background, QColor(0x0F, 0x11, 0x15));
            const auto chromePixel = rendered->pixelColor(card.left() + card.width() / 2, card.top() + 2);
            EXPECT_EQ(chromePixel, shareTemplate.style.editorChromeColor) << "代码窗口模板的标题栏颜色";
            EXPECT_NE(background, chromePixel);
        } else
        {
            ADD_FAILURE() << "出现了意料之外的模板 id";
        }
    }
    ASSERT_EQ(backgrounds.size(), 4U);
    for (std::size_t left = 0; left < backgrounds.size(); ++left)
    {
        for (std::size_t right = left + 1; right < backgrounds.size(); ++right)
        {
            EXPECT_NE(backgrounds[left], backgrounds[right]) << "内置模板的背景应互不相同";
        }
    }
}

/// 空图、越界样式、超宽画布都拒绝，并给出 InvalidArgument 与中文原因
TEST(ShareRenderer, RejectsInvalidInputs)
{
    const auto expectInvalidArgument = [](const GyanisScribe::Base::Expected<QImage> &rendered, const char *what)
    {
        EXPECT_FALSE(rendered.has_value()) << what;
        if (rendered.has_value())
        {
            return;
        }
        EXPECT_EQ(rendered.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument) << what;
        EXPECT_TRUE(QString::fromStdString(rendered.error().message).contains(QStringLiteral("美化"))) << what;
    };

    expectInvalidArgument(renderShareImage(QImage{}, ShareStyle{}), "空图");
    auto bad          = flatStyle();
    bad.paddingPixels = -1;
    expectInvalidArgument(renderShareImage(makeSolidImage(QSize{10, 10}, Qt::red), bad), "越界内边距");
    expectInvalidArgument(renderShareImage(makeSolidImage(QSize{32760, 1}, Qt::red), ShareStyle{}), "画布宽超过 32768 应拒绝，而不是分不出图才报错");
}

/// 高倍比源图仍按物理像素铺满卡片，输出倍比固定为 1
TEST(ShareRenderer, IgnoresSourceDevicePixelRatio)
{
    auto image = makeSolidImage(QSize{100, 50}, QColor{255, 0, 0});
    image.setDevicePixelRatio(2.0);
    const auto rendered = renderShareImage(image, flatStyle());
    ASSERT_TRUE(rendered.has_value()) << rendered.error().message;
    EXPECT_EQ(rendered->size(), QSize(120, 70));
    EXPECT_EQ(rendered->devicePixelRatio(), 1.0);
    EXPECT_EQ(rendered->pixelColor(100, 50), QColor(255, 0, 0)) << "源图倍比不该把原图缩到卡片左上角";
}

/// 渲染不动入参
TEST(ShareRenderer, DoesNotMutateTheSource)
{
    const auto image = makeSolidImage(QSize{40, 30}, QColor{1, 2, 3});
    const auto copy  = image.copy();
    ASSERT_TRUE(renderShareImage(image, flatStyle()).has_value());
    EXPECT_EQ(image, copy);
}
