// 输出装饰的像素级直测：尺寸、边框的圈数与颜色、投影的衰减方向、垫白与保透明两档。
// 期望值全部手算写死，不叫被测函数自己算期望（那条路会让规范怎么改都绿）。
#include "Core/OutputDecoration.h"

#include <QColor>
#include <QImage>

#include <gtest/gtest.h>

using GyanisScribe::Core::decoratedOutputSize;
using GyanisScribe::Core::decorateForOutput;
using GyanisScribe::Core::kOutputBorderColor;
using GyanisScribe::Core::kOutputBorderPixels;
using GyanisScribe::Core::kOutputShadowPixels;

namespace
{
    /// 一张 4×4 的红图：取样点少，整幅逐像素核也不费事
    QImage makeRedImage()
    {
        QImage image{4, 4, QImage::Format_ARGB32_Premultiplied};
        image.fill(QColor{255, 0, 0});
        return image;
    }
} // namespace

/**
 * @brief 两档都关时尺寸不动、图像原样交回
 * @details 装饰是「要的时候才有」：默认档下多出任何一圈都会让每一张成品都带上没人要的边。
 */
TEST(OutputDecoration, BothOffChangesNothing)
{
    const auto image = makeRedImage();

    EXPECT_EQ(decoratedOutputSize(image.size(), false, false), image.size());

    const auto output = decorateForOutput(image, false, false, false);
    EXPECT_EQ(output.size(), image.size());
    EXPECT_EQ(output, image) << "两档都关时像素被改动了";

    // 垫底那一档单开着也要生效：JPG 那一路没有装饰也要垫白（否则透明区按驱动解释落色）
    QImage transparent{2, 2, QImage::Format_ARGB32_Premultiplied};
    transparent.fill(Qt::transparent);
    const auto flattened = decorateForOutput(transparent, false, false, true);
    EXPECT_EQ(flattened.pixelColor(0, 0), QColor(255, 255, 255)) << "垫底那一路没生效";
}

/**
 * @brief 尺寸核算：每开一档按它的宽度向四周各撑一圈
 */
TEST(OutputDecoration, SizeGrowsByEachEnabledLayer)
{
    const QSize    base{100, 60};
    constexpr auto border = kOutputBorderPixels;
    constexpr auto shadow = kOutputShadowPixels;

    EXPECT_EQ(decoratedOutputSize(base, true, false), QSize(100 + 2 * border, 60 + 2 * border));
    EXPECT_EQ(decoratedOutputSize(base, false, true), QSize(100 + 2 * shadow, 60 + 2 * shadow));
    EXPECT_EQ(decoratedOutputSize(base, true, true), QSize(100 + 2 * (border + shadow), 60 + 2 * (border + shadow)));
}

/**
 * @brief 钉住边框那两圈的对外内里：外圈与内圈都是中灰，图心一格不动
 * @details 边框的宽度与颜色是「成品从底上分得出来」这条承诺的全部内容，
 *          少一圈会在浅色底上消失、多一圈会吃掉画面。
 */
TEST(OutputDecoration, BorderDrawsThickNeutralFrameAndKeepsTheImageIntact)
{
    const auto image  = makeRedImage();
    const auto output = decorateForOutput(image, true, false, false);

    constexpr auto border = kOutputBorderPixels;
    ASSERT_EQ(output.size(), QSize(4 + 2 * border, 4 + 2 * border));

    // 左上角那 2×2 都是边框灰；图的第一行从 (border, border) 起
    for (int offset = 0; offset < border; ++offset)
    {
        EXPECT_EQ(output.pixelColor(offset, offset), kOutputBorderColor) << "外圈第 " << offset << " 圈不是边框色";
        EXPECT_EQ(output.pixelColor(4 + border + offset, 4 + border + offset), kOutputBorderColor) << "右下外圈不是边框色";
    }
    EXPECT_EQ(output.pixelColor(border, border), QColor(255, 0, 0)) << "图心被边框侵占了";
    EXPECT_EQ(output.pixelColor(4 + border - 1, 4 + border - 1), QColor(255, 0, 0)) << "图的右下角被边框侵占";
    // 边框四边对称：右边缘与下边缘的灰色条也要在
    EXPECT_EQ(output.pixelColor(output.width() - 1, output.height() / 2), kOutputBorderColor) << "右边没有边框";
    EXPECT_EQ(output.pixelColor(output.width() / 2, output.height() - 1), kOutputBorderColor) << "下边没有边框";
}

/**
 * @brief 钉住投影的衰减方向：贴边最深、越远越淡、到边界归零，并且图上一格不受影响
 * @details 衰减是投影「柔和」的全部内容：反过来画（远处最深）会出现一圈看得见的亮边；
 *          图上沾一点黑则会把每张成品都抹脏一围。两者都是肉眼先看见、代码里不出错的坏法。
 */
TEST(OutputDecoration, ShadowFadesWithDistanceAndNeverTouchesTheImage)
{
    const auto image  = makeRedImage();
    const auto output = decorateForOutput(image, false, true, false);

    constexpr auto shadow = kOutputShadowPixels;
    ASSERT_EQ(output.size(), QSize(4 + 2 * shadow, 4 + 2 * shadow));

    // 图左上角在 (shadow, shadow)；它正上方一格外是全图最深的投影，距离越远 alpha 越小
    const auto nearest  = output.pixelColor(shadow, shadow - 1);
    const auto midway   = output.pixelColor(shadow, shadow - shadow / 2);
    const auto boundary = output.pixelColor(shadow, shadow - shadow);
    EXPECT_GT(nearest.alpha(), midway.alpha()) << "投影没有随距离衰减";
    EXPECT_GT(midway.alpha(), 0) << "中段投影不该已经归零";
    EXPECT_EQ(boundary.alpha(), 0) << "投影在边界上还有残影，会留一条看得见的边";
    EXPECT_EQ(boundary, QColor(0, 0, 0, 0)) << "边界外应当完全透明";

    // 图本身一格都不许动：投影只画在图区之外
    for (int y = shadow; y < shadow + 4; ++y)
    {
        for (int x = shadow; x < shadow + 4; ++x)
        {
            EXPECT_EQ(output.pixelColor(x, y), QColor(255, 0, 0)) << "投影抹到了图上 (" << x << ',' << y << ')';
        }
    }
}

/**
 * @brief 垫白档：投影底下是白纸而不是透明
 * @details 存 JPG 或往纸上打印时没有透明通道可用，透明区会被压成不可预期的底色。
 */
TEST(OutputDecoration, BackgroundFillPutsWhiteUnderTheShadow)
{
    const auto image  = makeRedImage();
    const auto output = decorateForOutput(image, false, true, true);

    constexpr auto shadow = kOutputShadowPixels;
    // 远离样品的那一角（距离超过投影半径）应当是纯白不透
    const auto farCorner = output.pixelColor(0, 0);
    EXPECT_EQ(farCorner, QColor(255, 255, 255)) << "垫白档的远处不是白底";

    // 贴着图那一圈：白底叠上黑投影，应当是灰而不是白也不是黑
    const auto nearEdge = output.pixelColor(shadow, shadow - 1);
    EXPECT_EQ(nearEdge.alpha(), 255) << "垫白之后不该还有透明";
    EXPECT_LT(nearEdge.red(), 255) << "投影没叠在垫白之上";
    EXPECT_GT(nearEdge.red(), 0) << "投影把白底画成了纯黑，衰减公式没生效";
}

/**
 * @brief 装饰保留原图的透明通道：带 alpha 的像素在图上仍在
 * @details 只开投影不开边框：边框是不透明的，半透明图压在边框上本来就会按源覆盖混合，
 *          那是边框该干的事；这里钉的是「装饰动作本身不把图内像素抹平」。
 */
TEST(OutputDecoration, KeepsTransparencyInsideTheImage)
{
    QImage image{4, 4, QImage::Format_ARGB32_Premultiplied};
    image.fill(QColor{0, 255, 0, 128});

    const auto output = decorateForOutput(image, false, true, false);
    const auto inside = output.pixelColor(kOutputShadowPixels, kOutputShadowPixels);
    EXPECT_EQ(inside.alpha(), 128) << "图内的半透明被抹平了";
    EXPECT_EQ(inside.green(), 255);
}
