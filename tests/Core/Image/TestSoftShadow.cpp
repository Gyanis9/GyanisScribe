#include "Core/Image/SoftShadow.h"

#include <QColor>
#include <QImage>
#include <QRect>
#include <QSize>

#include <gtest/gtest.h>

using GyanisScribe::Core::makeSoftShadowLayer;

/**
 * @brief 钉住直角投影的 alpha 与旧装饰逐像素同解
 * @details 距卡片 1 格是 96 * (15/16)^2 ≈ 84；卡片内部与投影半径之外都不写。
 */
TEST(SoftShadow, AlphaMatchesTheOldDecorationFalloff)
{
    const auto layer = makeSoftShadowLayer(QSize{200, 160}, QRect{20, 30, 100, 80}, 0, 16, 96);
    ASSERT_FALSE(layer.isNull());
    EXPECT_EQ(qAlpha(layer.pixel(19, 70)), 84) << "距卡片 1 格应按 96 * (15/16)^2 四舍五入";
    EXPECT_EQ(qAlpha(layer.pixel(20, 70)), 0) << "卡片内部不写投影";
    EXPECT_EQ(qAlpha(layer.pixel(3, 70)), 0) << "投影半径之外不写";
    EXPECT_EQ(qAlpha(layer.pixel(19, 29)), 80) << "斜角距卡片 sqrt(2) 格，alpha 应为 80";
}

/**
 * @brief 钉住圆角投影跟着圆角走
 * @details 圆角切掉的那块仍在卡片外接矩形内，必须有投影；卡片中心一格都不写。
 */
TEST(SoftShadow, RoundedCornerIsCoveredOutsideTheCurve)
{
    const auto layer = makeSoftShadowLayer(QSize{260, 260}, QRect{50, 50, 100, 100}, 20, 16, 96);
    ASSERT_FALSE(layer.isNull());
    EXPECT_GT(qAlpha(layer.pixel(52, 52)), 0) << "圆角外、外接矩形内的角格应有投影";
    EXPECT_EQ(qAlpha(layer.pixel(100, 100)), 0) << "卡片内部不写投影";
}

/**
 * @brief 钉住圆角夹到卡片短边的一半，不因超大半径越界
 */
TEST(SoftShadow, HugeCornerRadiusIsClamped)
{
    const auto layer = makeSoftShadowLayer(QSize{260, 260}, QRect{50, 50, 100, 100}, 999, 16, 96);
    ASSERT_FALSE(layer.isNull());
    EXPECT_EQ(qAlpha(layer.pixel(100, 100)), 0);
    EXPECT_GT(qAlpha(layer.pixel(49, 100)), 0) << "圆片外一格应有投影";
}

/// 投影宽度非正、画布为空或卡片为空时返回空图，不返回一张透明画布让调用方误以为画过了
TEST(SoftShadow, EmptyInputsReturnNull)
{
    EXPECT_TRUE(makeSoftShadowLayer(QSize{}, QRect{0, 0, 10, 10}, 0, 16).isNull());
    EXPECT_TRUE(makeSoftShadowLayer(QSize{100, 100}, QRect{}, 0, 16).isNull());
    EXPECT_TRUE(makeSoftShadowLayer(QSize{100, 100}, QRect{10, 10, 20, 20}, 0, 0).isNull());
}

/**
 * @brief 钉住右、下两条投影带与左、上同解：四条边按同一组 alpha 收尾
 * @details 距离若按 width() / 2 算半宽，右、下两边会各少一格，紧贴卡片那一圈最深
 *          的投影只在左、上出现，四条边看上去一边轻一边重。
 */
TEST(SoftShadow, RightAndBottomBandsMirrorLeftAndTop)
{
    const auto layer = makeSoftShadowLayer(QSize{200, 160}, QRect{20, 30, 100, 80}, 0, 16, 96);
    ASSERT_FALSE(layer.isNull());
    EXPECT_EQ(qAlpha(layer.pixel(19, 70)), qAlpha(layer.pixel(120, 70))) << "左右同距的 alpha 不一致";
    EXPECT_EQ(qAlpha(layer.pixel(70, 29)), qAlpha(layer.pixel(70, 110))) << "上下同距的 alpha 不一致";
    EXPECT_EQ(qAlpha(layer.pixel(120, 70)), 84) << "距卡片右沿 1 格应为 84";
    EXPECT_EQ(qAlpha(layer.pixel(70, 110)), 84) << "距卡片下沿 1 格应为 84";
}
