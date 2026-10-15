#include "Core/Annotation/ShapeTransform.h"

#include <QRectF>

#include <gtest/gtest.h>

using GyanisScribe::Core::angleDegreesOf;
using GyanisScribe::Core::AnnotationShape;
using GyanisScribe::Core::kMinimumResizedBoxSide;
using GyanisScribe::Core::normalizedAngleDelta;
using GyanisScribe::Core::resizedBox;
using GyanisScribe::Core::rotatedBoxBounds;
using GyanisScribe::Core::rotatedPoint;
using GyanisScribe::Core::SelectionHandle;
using GyanisScribe::Core::shapeInBox;
using GyanisScribe::Core::unrotatedPoint;

/**
 * @brief 钉住伸缩只动抓住的那几条边
 * @details 八个方向各是一套边组合；把不该动的边也带上，界面上的手感就是「拉一角整框跳」。
 *          起点框取 (10,20)-(90,60)：四个数字互不相同，哪条边被动了一眼看得出来。
 */
TEST(ShapeTransformResize, MovesOnlyTheGrabbedEdges)
{
    const QRectF box{10.0, 20.0, 80.0, 40.0}; // left 10 top 20 right 90 bottom 60

    const auto right = resizedBox(box, SelectionHandle::Right, QPointF{200.0, 55.0});
    EXPECT_EQ(right, QRectF(QPointF(10.0, 20.0), QPointF(200.0, 60.0))) << "右边中点没只动右边";

    const auto top = resizedBox(box, SelectionHandle::Top, QPointF{30.0, 5.0});
    EXPECT_EQ(top, QRectF(QPointF(10.0, 5.0), QPointF(90.0, 60.0))) << "上边中点没只动上边";

    const auto topLeft = resizedBox(box, SelectionHandle::TopLeft, QPointF{0.0, 0.0});
    EXPECT_EQ(topLeft, QRectF(QPointF(0.0, 0.0), QPointF(90.0, 60.0))) << "左上角没同时动左边与上边";

    const auto bottomRight = resizedBox(box, SelectionHandle::BottomRight, QPointF{95.0, 70.0});
    EXPECT_EQ(bottomRight, QRectF(QPointF(10.0, 20.0), QPointF(95.0, 70.0))) << "右下角没同时动右边与下边";

    const auto left = resizedBox(box, SelectionHandle::Left, QPointF{50.0, 30.0});
    EXPECT_EQ(left, QRectF(QPointF(50.0, 20.0), QPointF(90.0, 60.0))) << "左边中点没只动左边";

    const auto bottom = resizedBox(box, SelectionHandle::Bottom, QPointF{40.0, 80.0});
    EXPECT_EQ(bottom, QRectF(QPointF(10.0, 20.0), QPointF(90.0, 80.0))) << "下边中点没只动下边";

    // 非缩放的两档原地不动：旋转与「没抓住」都不改框
    EXPECT_EQ(resizedBox(box, SelectionHandle::Rotate, QPointF{500.0, 500.0}), box);
    EXPECT_EQ(resizedBox(box, SelectionHandle::None, QPointF{500.0, 500.0}), box);
}

/**
 * @brief 钉住拖过对面一条边时夹在最小边长上、不翻转
 * @details 翻转要换来「翻转手柄语义 + 取整方向」两套特例，收益只是镜像一条图形；
 *          夹住的话越过临界点也只是停住，手上不会出现 0 尺寸的框（除零与不可见都从这里来）。
 */
TEST(ShapeTransformResize, ClampsAtTheMinimumWithoutFlipping)
{
    const QRectF box{10.0, 20.0, 80.0, 40.0};

    const auto squeezedRight = resizedBox(box, SelectionHandle::Right, QPointF{5.0, 40.0});
    EXPECT_DOUBLE_EQ(squeezedRight.width(), kMinimumResizedBoxSide) << "右边拖过左边没夹在最小边长上";
    EXPECT_DOUBLE_EQ(squeezedRight.left(), 10.0) << "夹住时左边不该跟着动";

    const auto squeezedLeft = resizedBox(box, SelectionHandle::Left, QPointF{300.0, 40.0});
    EXPECT_DOUBLE_EQ(squeezedLeft.width(), kMinimumResizedBoxSide) << "左边拖过右边没夹在最小边长上";
    EXPECT_DOUBLE_EQ(squeezedLeft.right(), 90.0) << "夹住时右边不该跟着动";
}

/**
 * @brief 钉住 Ctrl 档（中心点缩放）：拖一条边，对面一条对向走同样多的距离，中心不动
 * @details 中心是 (50,40) 的框，右边拖到 80 时半程 30：左边该从 10 走到 20。
 *          拖过中心时夹在最小边长上、中心也不挪——翻转在这里同样不做，两档共用一个规矩。
 */
TEST(ShapeTransformResize, ScalesAboutTheCenterWhenAsked)
{
    const QRectF box{10.0, 20.0, 80.0, 40.0}; // 中心 (50,40)

    const auto right = resizedBox(box, SelectionHandle::Right, QPointF{80.0, 40.0}, kMinimumResizedBoxSide, true, false);
    EXPECT_DOUBLE_EQ(right.left(), 20.0) << "中心点缩放时左边没对向跟上";
    EXPECT_DOUBLE_EQ(right.right(), 80.0);
    EXPECT_DOUBLE_EQ(right.top(), 20.0) << "没拖的那条轴不该跟着动";
    EXPECT_DOUBLE_EQ(right.bottom(), 60.0);

    const auto topLeft = resizedBox(box, SelectionHandle::TopLeft, QPointF{30.0, 30.0}, kMinimumResizedBoxSide, true, false);
    EXPECT_DOUBLE_EQ(topLeft.left(), 30.0);
    EXPECT_DOUBLE_EQ(topLeft.right(), 70.0) << "右上角没绕中心对向跟上";
    EXPECT_DOUBLE_EQ(topLeft.top(), 30.0);
    EXPECT_DOUBLE_EQ(topLeft.bottom(), 50.0);
    EXPECT_DOUBLE_EQ(topLeft.center().x(), 50.0) << "中心点缩放的框该以原中心为不动点";
    EXPECT_DOUBLE_EQ(topLeft.center().y(), 40.0);

    const auto crossed = resizedBox(box, SelectionHandle::Right, QPointF{20.0, 40.0}, kMinimumResizedBoxSide, true, false);
    EXPECT_DOUBLE_EQ(crossed.width(), kMinimumResizedBoxSide) << "拖过中心没夹在最小边长上";
    EXPECT_DOUBLE_EQ(crossed.center().x(), 50.0) << "夹住时中心也被带走了";
}

/**
 * @brief 钉住 Shift 档（等比例）：角上手柄取变化大的那一轴定倍率，边中手柄另一轴绕中线补出去
 * @details 80×40（2:1）的框：右下角拖到 (90,80) 时两轴比值 1.0 与 1.5，取大的 1.5，
 *          两轴同倍、锚定的左与上两条边不动；右边中点拖到 170 时横向倍率 2，
 *          纵向按 2 绕中线长出去，上下各让出 20。
 */
TEST(ShapeTransformResize, KeepsTheAspectRatioWhenAsked)
{
    const QRectF box{10.0, 20.0, 80.0, 40.0};

    const auto corner = resizedBox(box, SelectionHandle::BottomRight, QPointF{90.0, 80.0}, kMinimumResizedBoxSide, false, true);
    EXPECT_DOUBLE_EQ(corner.left(), 10.0) << "等比例缩放把锚定的左边带走了";
    EXPECT_DOUBLE_EQ(corner.top(), 20.0);
    EXPECT_DOUBLE_EQ(corner.width(), 120.0) << "宽没按变化大的那一轴倍率走";
    EXPECT_DOUBLE_EQ(corner.height(), 60.0) << "高没按同一倍率跟出去";
    EXPECT_DOUBLE_EQ(corner.width() / corner.height(), 2.0) << "长宽比没保住";

    const auto edge = resizedBox(box, SelectionHandle::Right, QPointF{170.0, 40.0}, kMinimumResizedBoxSide, false, true);
    EXPECT_DOUBLE_EQ(edge.left(), 10.0);
    EXPECT_DOUBLE_EQ(edge.width(), 160.0);
    EXPECT_DOUBLE_EQ(edge.height(), 80.0) << "另一轴没按同一倍率补偿";
    EXPECT_DOUBLE_EQ(edge.top(), 0.0) << "补偿的那条轴没绕自己的中线长出去";
    EXPECT_DOUBLE_EQ(edge.bottom(), 80.0);
    EXPECT_DOUBLE_EQ(edge.center().y(), 40.0) << "补偿把整框带得跳了位";
}

/**
 * @brief 钉住两档同时按：以中心为不动点、保持长宽比
 */
TEST(ShapeTransformResize, CombinesCenterAndProportional)
{
    const QRectF box{10.0, 20.0, 80.0, 40.0};

    // 右下角拖到 (110,40)：横向半程 60（倍率 1.5）、纵向半程 0（夹在最小），取大的 1.5
    const auto both = resizedBox(box, SelectionHandle::BottomRight, QPointF{110.0, 40.0}, kMinimumResizedBoxSide, true, true);
    EXPECT_DOUBLE_EQ(both.width(), 120.0);
    EXPECT_DOUBLE_EQ(both.height(), 60.0);
    EXPECT_DOUBLE_EQ(both.center().x(), 50.0) << "两档一起按时框该仍以原中心为不动点";
    EXPECT_DOUBLE_EQ(both.center().y(), 40.0);
}

/**
 * @brief 钉住某一轴长为零时等比例退回逐轴伸缩：没有长宽比可算，也不能除零
 * @details 竖直线（宽 0、高 40）横向拖大：零宽那轴按拖动点直接给长度，有长度的那轴原地不动。
 */
TEST(ShapeTransformResize, FallsBackToPerAxisForAZeroLengthAxis)
{
    const QRectF line{50.0, 10.0, 0.0, 40.0};

    const auto widened = resizedBox(line, SelectionHandle::Right, QPointF{80.0, 30.0}, kMinimumResizedBoxSide, false, true);
    EXPECT_DOUBLE_EQ(widened.width(), 30.0) << "零宽那轴该按拖动点直接给出长度";
    EXPECT_DOUBLE_EQ(widened.height(), 40.0) << "有长度的那轴没有倍率可算，该原地不动";
    EXPECT_DOUBLE_EQ(widened.top(), 10.0) << "退回逐轴伸缩时那条轴该保持原位";
}

/**
 * @brief 钉住整条映射：几何按倍率走，笔宽/字号/粒度按两轴里大的那边缩放
 * @details 取整与 QRect 的包含口径各差一个像素，断言留一格容差；轨迹点与锚点是无损的，
 *          可以按手算的绝对值钉死。
 */
TEST(ShapeTransformScale, MapsGeometryAndDerivedSizes)
{
    AnnotationShape shape;
    shape.rect            = QRect{10, 20, 80, 40};
    shape.points          = {QPoint{10, 20}, QPoint{50, 30}};
    shape.position        = QPoint{30, 40};
    shape.lineWidth       = 2;
    shape.fontSize        = 12;
    shape.mosaicBlockSize = 10;

    const QRectF from{10.0, 20.0, 80.0, 40.0};
    const QRectF to{10.0, 20.0, 160.0, 80.0}; // 两轴都翻倍

    const auto scaled = shapeInBox(shape, from, to);

    EXPECT_NEAR(scaled.rect.left(), 10.0, 1.0) << "左边界该留在原地";
    EXPECT_NEAR(scaled.rect.top(), 20.0, 1.0) << "上边界该留在原地";
    EXPECT_NEAR(scaled.rect.width(), 158.0, 2.0) << "矩形没按两倍宽长出去";
    EXPECT_NEAR(scaled.rect.height(), 78.0, 2.0) << "矩形没按两倍高长出去";

    // 采样点：框中心 (50,40) 是不动点，手算绝对值
    EXPECT_EQ(scaled.points.at(0), QPoint(10, 20)) << "落在框角上的采样点该原位不动";
    EXPECT_EQ(scaled.points.at(1), QPoint(90, 40)) << "框内采样点没按两倍映射";

    // 锚点：(30,40) 相对旧中心 (50,40) 的偏移 (-20,0) 翻倍成 (-40,0) → 新中心 (90,60) 往回退
    EXPECT_EQ(scaled.position, QPoint(50, 60)) << "锚点没按同一套系数映射";

    EXPECT_EQ(scaled.lineWidth, 4) << "笔宽没跟着放大";
    EXPECT_EQ(scaled.fontSize, 24) << "字号没跟着放大";
    EXPECT_EQ(scaled.mosaicBlockSize, 20) << "马赛克粒度没跟着放大";
}

/**
 * @brief 钉住粒度档位夹在合法区间里
 */
TEST(ShapeTransformScale, ClampsMosaicBlockSizeIntoItsLadderRange)
{
    AnnotationShape shape;
    shape.mosaicBlockSize = 40;

    const QRectF from{0.0, 0.0, 10.0, 10.0};
    const QRectF to{0.0, 0.0, 40.0, 40.0}; // 四倍

    const auto scaled = shapeInBox(shape, from, to);
    EXPECT_EQ(scaled.mosaicBlockSize, 64) << "四倍之后该夹在 64 上限";
}

/**
 * @brief 钉住某一轴长度为零时的退化映射：该轴只平移，不除零、不留在原地
 * @details 竖直线这类图形在某一轴上没有长度可缩放；按中心平移是唯一说得通的结果，
 *          而且与「倍率派生的笔宽字号」那两格共用同一套读法。
 */
TEST(ShapeTransformScale, TreatsAZeroLengthAxisAsPureTranslation)
{
    AnnotationShape line;
    line.points = {QPoint{50, 10}, QPoint{50, 50}};

    // 一轴为 0 的框：x 轴纯平移（新中心 60），y 轴照常缩放（40 → 80）
    const QRectF from{50.0, 10.0, 0.0, 40.0};
    const QRectF to{50.0, 10.0, 20.0, 80.0};
    const auto   scaled = shapeInBox(line, from, to);

    EXPECT_EQ(scaled.points.at(0), QPoint(60, 10)) << "零长那轴该平移到新中心上";
    EXPECT_EQ(scaled.points.at(1), QPoint(60, 90)) << "有长度那轴该照常缩放";
}

/**
 * @brief 钉住旋转辅助与 QPainter::rotate 同向（顺时针为正，y 轴向下）
 * @details 画的时候是 QPainter 转的，命中与手柄靠这里的公式映回来；两边反向的话，
 *          转过 90° 的图形会「看得见点不中」，而且越转越偏。
 */
TEST(ShapeTransformRotation, TurnsWithTheSameHandednessAsThePainter)
{
    const QPointF center{0.0, 0.0};

    const auto turned = rotatedPoint(QPointF{1.0, 0.0}, center, 90.0);
    EXPECT_NEAR(turned.x(), 0.0, 1e-9);
    EXPECT_NEAR(turned.y(), 1.0, 1e-9) << "顺时针 90° 该把右侧转到下方（y 轴向下）";

    const QPointF source{13.0, -7.0};
    const auto    back = unrotatedPoint(rotatedPoint(source, center, 37.0), center, 37.0);
    EXPECT_NEAR(back.x(), source.x(), 1e-9) << "转过去再转回来没回到原点";
    EXPECT_NEAR(back.y(), source.y(), 1e-9);

    EXPECT_NEAR(angleDegreesOf(QPointF{1.0, 0.0}, center), 0.0, 1e-9);
    EXPECT_NEAR(angleDegreesOf(QPointF{0.0, -1.0}, center), -90.0, 1e-9) << "正上方该是 -90°";
    EXPECT_NEAR(angleDegreesOf(QPointF{0.0, 1.0}, center), 90.0, 1e-9);
}

/**
 * @brief 钉住旋转外接框：90° 交换宽高，0° 保持原框
 */
TEST(ShapeTransformRotation, RotatedBoundsWrapAllFourCorners)
{
    const QRectF box{0.0, 0.0, 10.0, 20.0};

    EXPECT_EQ(rotatedBoxBounds(box, 0.0), box) << "没转的框不该被改";

    const auto turned = rotatedBoxBounds(box, 90.0);
    EXPECT_NEAR(turned.width(), 20.0, 1e-9) << "转 90° 后宽高该互换";
    EXPECT_NEAR(turned.height(), 10.0, 1e-9);
    EXPECT_NEAR(turned.center().x(), box.center().x(), 1e-9) << "外接框中心该留在原处";
    EXPECT_NEAR(turned.center().y(), box.center().y(), 1e-9);
}

/**
 * @brief 钉住角度差归一化到 (-180, 180]
 * @details 拖着旋转柄绕过对侧时角度差一步跨过 ±180°：不归一图形会跟着猛转一整圈。
 */
TEST(ShapeTransformRotation, NormalizesAngleDeltaIntoHalfOpenRange)
{
    EXPECT_DOUBLE_EQ(normalizedAngleDelta(0.0), 0.0);
    EXPECT_DOUBLE_EQ(normalizedAngleDelta(190.0), -170.0) << "超过半圈该折成反向的短弧";
    EXPECT_DOUBLE_EQ(normalizedAngleDelta(-190.0), 170.0);
    EXPECT_DOUBLE_EQ(normalizedAngleDelta(180.0), 180.0) << "半个整圈该取正的那一侧";
    EXPECT_DOUBLE_EQ(normalizedAngleDelta(-180.0), 180.0);
    EXPECT_DOUBLE_EQ(normalizedAngleDelta(540.0), 180.0) << "绕了一圈半该回到半圈那一档";
}
