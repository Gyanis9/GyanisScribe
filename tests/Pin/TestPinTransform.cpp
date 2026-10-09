#include "Pin/PinTransform.h"

#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

using namespace GyanisScribe::Pin;

namespace
{
    // 浮点判等用的绝对容差：这些值都是手算出来的整数或整数倍根号，容差只需要吃掉最后的位噪声
    constexpr double kTolerance = 0.01;

    bool nearEqual(double actual, double expected)
    {
        return std::abs(actual - expected) <= kTolerance;
    }
} // namespace

/**
 * @brief 钉住缩放与透明度的夹取边界（手算的绝对值，不是「在范围内」这种半句话）
 */
TEST(PinTransform, ClampsScaleAndOpacityToTheirRanges)
{
    EXPECT_TRUE(nearEqual(clampScale(0.001), kMinimumScale)) << "过小的缩放该停在下限，而不是缩成看不见的点";
    EXPECT_TRUE(nearEqual(clampScale(-5.0), kMinimumScale)) << "负缩放被放过去会得到翻转的图";
    EXPECT_TRUE(nearEqual(clampScale(999.0), kMaximumScale));
    EXPECT_TRUE(nearEqual(clampScale(1.75), 1.75)) << "范围内的值不该被动过";

    EXPECT_TRUE(nearEqual(clampOpacity(0.0), kMinimumOpacity));
    EXPECT_TRUE(nearEqual(clampOpacity(1.4), 1.0));
    EXPECT_TRUE(nearEqual(clampOpacity(0.4), 0.4));
}

/**
 * @brief 钉住窗口尺寸 = 逻辑尺寸 × 缩放，且不出现零边
 */
TEST(PinTransform, WindowSizeFollowsLogicalSizeTimesScale)
{
    const QSizeF logical(100.0, 50.0);
    EXPECT_TRUE(nearEqual(windowSizeFor(logical, 1.0).width(), 100.0));
    EXPECT_TRUE(nearEqual(windowSizeFor(logical, 1.0).height(), 50.0));
    EXPECT_TRUE(nearEqual(windowSizeFor(logical, 2.0).width(), 200.0));
    EXPECT_TRUE(nearEqual(windowSizeFor(logical, 2.0).height(), 100.0));

    // 0.01 会被夹到 0.05：100×0.05 = 5，50×0.05 = 2.5，两条边都还在
    EXPECT_TRUE(nearEqual(windowSizeFor(logical, 0.01).width(), 5.0));
    EXPECT_TRUE(nearEqual(windowSizeFor(logical, 0.01).height(), 2.5));

    // 极小图片也要留出可点到的尺寸
    EXPECT_GE(windowSizeFor(QSizeF(1.0, 1.0), kMinimumScale).width(), 1.0);
}

/**
 * @brief 钉住旋转外接框在三个特殊角上的手算值
 */
TEST(PinTransform, RotatedBoundingBoxMatchesHandCalculatedSizes)
{
    const QPointF center(50.0, 30.0);
    const QSizeF  size(100.0, 60.0);

    const auto straight = rotatedBoundingBox(center, size, 0.0);
    EXPECT_TRUE(nearEqual(straight.width(), 100.0)) << "0° 时外接框就该等于原尺寸";
    EXPECT_TRUE(nearEqual(straight.height(), 60.0));
    EXPECT_TRUE(nearEqual(straight.center().x(), 50.0)) << "中心不该随角度漂走";
    EXPECT_TRUE(nearEqual(straight.center().y(), 30.0));

    const auto quarter = rotatedBoundingBox(center, size, 90.0);
    EXPECT_TRUE(nearEqual(quarter.width(), 60.0)) << "90° 时宽高应互换";
    EXPECT_TRUE(nearEqual(quarter.height(), 100.0));

    const auto   diagonal      = rotatedBoundingBox(center, size, 45.0);
    const double expectedWidth = 100.0 * std::cos(std::numbers::pi_v<double> / 4.0) + 60.0 * std::sin(std::numbers::pi_v<double> / 4.0);
    EXPECT_TRUE(nearEqual(diagonal.width(), expectedWidth)) << "45° 的投影和算错了，旋转后会切边";
    EXPECT_TRUE(nearEqual(diagonal.height(), expectedWidth));

    // 反向旋转与正向同角度得到同一个框（绝对值的意义所在）
    EXPECT_TRUE(nearEqual(rotatedBoundingBox(center, size, -45.0).width(), expectedWidth));
}

/**
 * @brief 钉住八个手柄的位置：中心分别落在外接框的四角与四边中点
 */
TEST(PinTransform, HandlesSitOnCornersAndEdgeMidpoints)
{
    const QRectF target(10.0, 20.0, 200.0, 100.0);
    const auto   handles = handleRects(target, 10.0);

    ASSERT_EQ(handles.size(), 8U) << "手柄数量与 HandleKind 不一致，按下标取会错位";

    const QPointF expected[8] = {
            target.topLeft(),     QPointF(target.center().x(), target.top()),    target.topRight(),   QPointF(target.right(), target.center().y()),
            target.bottomRight(), QPointF(target.center().x(), target.bottom()), target.bottomLeft(), QPointF(target.left(), target.center().y()),
    };
    for (int index = 0; index < 8; ++index)
    {
        EXPECT_TRUE(nearEqual(handles[index].center().x(), expected[index].x())) << "第 " << index << " 个手柄横坐标不对";
        EXPECT_TRUE(nearEqual(handles[index].center().y(), expected[index].y())) << "第 " << index << " 个手柄纵坐标不对";
        EXPECT_TRUE(nearEqual(handles[index].width(), 10.0));
        EXPECT_TRUE(nearEqual(handles[index].height(), 10.0));
    }
}

/**
 * @brief 钉住拖拽改缩放的比例关系与两个退化分支
 */
TEST(PinTransform, DragScalesByDistanceRatio)
{
    EXPECT_TRUE(nearEqual(scaleFromDrag(1.0, 50.0, 100.0), 2.0)) << "拖到两倍远该放大两倍";
    EXPECT_TRUE(nearEqual(scaleFromDrag(2.0, 100.0, 50.0), 1.0)) << "拖回一半该缩一半";
    EXPECT_TRUE(nearEqual(scaleFromDrag(1.0, 0.0, 500.0), 1.0)) << "在中心点起拖不该把图瞬间缩没";
    EXPECT_TRUE(nearEqual(scaleFromDrag(1.0, 10.0, 100000.0), kMaximumScale)) << "放大要停在上限";
}

/**
 * @brief 钉住角度折算：负角与超圈都要落回 [0,360)
 */
TEST(PinTransform, DegreesFoldIntoFullCircle)
{
    EXPECT_TRUE(nearEqual(normalizeDegrees(-90.0), 270.0));
    EXPECT_TRUE(nearEqual(normalizeDegrees(720.0), 0.0));
    EXPECT_TRUE(nearEqual(normalizeDegrees(450.0), 90.0));
    EXPECT_TRUE(nearEqual(normalizeDegrees(359.9), 359.9));
}
