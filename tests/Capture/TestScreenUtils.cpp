#include "Capture/ScreenUtils.h"

#include <QImage>
#include <QPoint>
#include <QRect>

#include <gtest/gtest.h>

#include <vector>

#include <optional>
#include <string>
#include <vector>

using GyanisScribe::Capture::cropToLogicalRect;
using GyanisScribe::Capture::cursorForGrip;
using GyanisScribe::Capture::dragRegionByGrip;
using GyanisScribe::Capture::formatSelectionHint;
using GyanisScribe::Capture::gripAt;
using GyanisScribe::Capture::kSelectionGripTolerance;
using GyanisScribe::Capture::nudgeRegion;
using GyanisScribe::Capture::regionAtPoint;
using GyanisScribe::Capture::regionFromText;
using GyanisScribe::Capture::regionToText;
using GyanisScribe::Capture::resizeRegion;
using GyanisScribe::Capture::SelectionGrip;
using GyanisScribe::Capture::toPhysicalRect;
using GyanisScribe::Core::CapturedImage;
using GyanisScribe::Core::ErrorCode;

namespace
{
    // 一帧 200×100 的物理像素图，上半涂红、下半涂蓝：裁完之后按像素颜色就能验出偏移
    CapturedImage makeFrame(qreal devicePixelRatio)
    {
        QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(255, 0, 0));
        for (int y = image.height() / 2; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                image.setPixelColor(x, y, QColor(0, 0, 255));
            }
        }
        image.setDevicePixelRatio(1.0);

        CapturedImage frame;
        frame.image            = image;
        frame.devicePixelRatio = devicePixelRatio;
        frame.logicalSize      = QSize(static_cast<int>(200 / devicePixelRatio), static_cast<int>(100 / devicePixelRatio));
        frame.screenName       = QStringLiteral("FakeScreen-1");
        return frame;
    }
} // namespace

/**
 * @brief 钉住逻辑→物理的换算在三个缩放档上的手算结果
 */
TEST(ScreenUtils, ScalesLogicalRectToPhysicalPixels)
{
    EXPECT_EQ(*toPhysicalRect(QRect(0, 0, 100, 50), 1.0), QRect(0, 0, 100, 50));
    EXPECT_EQ(*toPhysicalRect(QRect(10, 20, 40, 20), 1.0), QRect(10, 20, 40, 20));
    // dpr 1.5：起点 15/30，宽 60 高 30（40×1.5、20×1.5）
    EXPECT_EQ(*toPhysicalRect(QRect(10, 20, 40, 20), 1.5), QRect(15, 30, 60, 30));
    // dpr 2：起点 32/32，宽 96 高 48
    EXPECT_EQ(*toPhysicalRect(QRect(16, 16, 48, 24), 2.0), QRect(32, 32, 96, 48));

    const auto rejected = toPhysicalRect(QRect(0, 0, 10, 10), 0.0);
    ASSERT_FALSE(rejected.has_value()) << "缩放因子为 0 时按 1 处理会得到一张与屏幕上不等大的图";
    EXPECT_EQ(rejected.error().code, ErrorCode::InvalidArgument);
}

/**
 * @brief 钉住裁图后「像素尺寸 ÷ 缩放因子 = 逻辑尺寸」这一对互指关系，以及像素确实落在选区里
 */
TEST(ScreenUtils, CropKeepsPixelAndLogicalSizePairing)
{
    const auto frame   = makeFrame(2.0);
    const auto cropped = cropToLogicalRect(frame, QRect(10, 10, 40, 15));
    ASSERT_TRUE(cropped.has_value()) << "选区在帧内应当裁得出，实际原因：" << (cropped.has_value() ? std::string{} : cropped.error().message);

    EXPECT_EQ(cropped->image.width(), 80) << "物理宽不是逻辑宽的两倍";
    EXPECT_EQ(cropped->image.height(), 30) << "逻辑 15 高在 dpr 2 上应是 30 物理行";
    EXPECT_EQ(cropped->logicalSize, (QSize(40, 15))) << "逻辑尺寸没跟着选区走，标注层会按错的大小排版";
    EXPECT_EQ(cropped->devicePixelRatio, 2.0);
    EXPECT_EQ(cropped->screenName, QStringLiteral("FakeScreen-1")) << "来源屏幕丢了，多显示器下无法回到那一块屏";

    // 像素判据先钉格式：非 ARGB32 时按 QColor 比较的含义不同
    ASSERT_EQ(cropped->image.format(), QImage::Format_ARGB32_Premultiplied);
    // 原帧物理 0..49 行是红、50..99 行是蓝；逻辑 (10,10,40,15) → 物理 (20,20,80,30) 全落在红区
    EXPECT_EQ(cropped->image.pixelColor(0, 0), QColor(255, 0, 0)) << "裁到的第一格不是选区左上角那一格，换算排错了点";
    EXPECT_EQ(cropped->image.pixelColor(79, 29), QColor(255, 0, 0)) << "最后一格应是原帧物理 (99,49)，仍在红区";

    // 跨界选区：逻辑 y=25 → 物理 y=50 正好是蓝区第一行，颜色判据能证明偏移量而不只是尺寸
    const auto crossing = cropToLogicalRect(frame, QRect(10, 25, 40, 10));
    ASSERT_TRUE(crossing.has_value());
    EXPECT_EQ(crossing->image.pixelColor(0, 0), QColor(0, 0, 255)) << "逻辑 y=25 在 dpr 2 上应当已经进了蓝区";
    EXPECT_EQ(crossing->image.height(), 20);
}

/**
 * @brief 钉住选区超出帧边时按 ScreenUnavailable 回绝，而不是裁出一张小图
 */
TEST(ScreenUtils, RejectsRegionOutsideFrame)
{
    const auto frame = makeFrame(1.0);

    const auto overflow = cropToLogicalRect(frame, QRect(150, 10, 100, 20));
    ASSERT_FALSE(overflow.has_value()) << "超出帧宽的选区被裁成了窄图，用户会以为屏幕就这么宽";
    EXPECT_EQ(overflow.error().code, ErrorCode::ScreenUnavailable);

    const auto negative = cropToLogicalRect(frame, QRect(-5, 0, 10, 10));
    ASSERT_FALSE(negative.has_value()) << "负坐标的选区不能悄悄夹回来";
    EXPECT_EQ(negative.error().code, ErrorCode::ScreenUnavailable);
}

/**
 * @brief 钉住读数的文本形状（尺寸在前、坐标在后）
 */
TEST(ScreenUtils, FormatsSelectionHint)
{
    const auto hint = formatSelectionHint(QRect(128, 96, 1024, 640));
    EXPECT_NE(hint.indexOf(QStringLiteral("1024 × 640")), -1) << "读数要报宽高，实际是：" << hint.toStdString();
    EXPECT_NE(hint.indexOf(QStringLiteral("(128, 96)")), -1) << "读数要报起点坐标，实际是：" << hint.toStdString();
}

/**
 * @brief 钉住选区存取文本的往返与拒绝面
 */
TEST(ScreenUtils, RegionTextRoundTripsAndRejects)
{
    EXPECT_EQ(regionToText(QRect(10, 20, 40, 50)), QStringLiteral("10,20,40,50"));
    EXPECT_EQ(*regionFromText("10,20,40,50"), QRect(10, 20, 40, 50));

    EXPECT_FALSE(regionFromText("10,20,40").has_value()) << "少一段不该补 0 当高度";
    EXPECT_FALSE(regionFromText("10,20,40,50,60").has_value()) << "多一段不该忽略尾巴";
    EXPECT_FALSE(regionFromText("a,20,40,50").has_value()) << "非数字要点名是哪一段";
    EXPECT_FALSE(regionFromText("10,20,12abc,50").has_value()) << "截断成 12 就是静默变形";
    EXPECT_FALSE(regionFromText("10,20,0,50").has_value()) << "宽 0 的选区存下去会让「上次区域」复用一个空框";
    EXPECT_FALSE(regionFromText("10,20,40,-3").has_value()) << "负高同理";

    const auto withSpaces = regionFromText(" 10 , 20 , 40 , 50 ");
    ASSERT_TRUE(withSpaces.has_value()) << "手改配置时的首尾空格不该判坏";
    EXPECT_EQ(*withSpaces, QRect(10, 20, 40, 50));
}

/**
 * @brief 钉住命中测试取的是压在最上面的那一个，而不是面积最小或最后一个
 * @details 候选那份顺序就是 z 序。往后找会把用户看得见的那扇换成被压在底下的一扇——
 *          这不会报错，只会截出一张「明明点的是它」的图。
 */
TEST(ScreenUtils, HitTestTakesTheTopmostCandidate)
{
    const std::vector<QRect> candidates{QRect{0, 0, 100, 100}, QRect{20, 20, 30, 30}, QRect{50, 50, 40, 40}};

    // (25,25) 同时落在前三块里：按顺序取第一块
    EXPECT_EQ(regionAtPoint(QPoint{25, 25}, candidates), std::optional<QRect>{candidates[0]});
    // (55,55) 落在第一块与第三块里：仍取压在上面的第一块
    EXPECT_EQ(regionAtPoint(QPoint{55, 55}, candidates), std::optional<QRect>{candidates[0]});
    // 一个都不含：返回空，遮罩上不该亮任何东西
    EXPECT_FALSE(regionAtPoint(QPoint{120, 120}, candidates).has_value());
    // 空名单（平台给不出候选）：同样的空结果，不越界也不崩
    EXPECT_FALSE(regionAtPoint(QPoint{10, 10}, {}).has_value());
}

/**
 * @brief 钉住方向键几何：平移改位置不改大小，缩放改大小不改顶边
 * @details 这两个量在遮罩上是同一个矩形的两半，混用就会「挪一格」变成「大一格」。
 */
TEST(ScreenUtils, NudgeMovesWithoutChangingSize)
{
    const QRect limit{0, 0, 100, 100};
    const QRect region{10, 20, 30, 40};

    EXPECT_EQ(nudgeRegion(region, QPoint{1, 0}, limit), QRect(11, 20, 30, 40));
    EXPECT_EQ(nudgeRegion(region, QPoint{0, -1}, limit), QRect(10, 19, 30, 40));
    // 连按到边：贴住右边界就停下，越界的那一格不该把整块甩到屏外
    EXPECT_EQ(nudgeRegion(QRect(70, 20, 30, 40), QPoint{1, 0}, limit), QRect(70, 20, 30, 40));
    EXPECT_EQ(nudgeRegion(QRect(0, 20, 30, 40), QPoint{-1, 0}, limit), QRect(0, 20, 30, 40));
    // 空选区没什么可挪的，也不该被挪出一个 (1,0) 的框
    EXPECT_TRUE(nudgeRegion({}, QPoint{1, 0}, limit).isNull());
}

/**
 * @brief 钉住缩放只动右下角，并且不许缩成空框、不许越出这块屏
 * @details 右下角收到与左上角重合就是 0 宽：那种选区在界面上看着还在，交出去却是一张空图。
 */
TEST(ScreenUtils, ResizeMovesOnlyTheBottomRightCorner)
{
    const QRect limit{0, 0, 100, 100};
    const QRect region{10, 20, 30, 40};

    EXPECT_EQ(resizeRegion(region, QPoint{1, 0}, limit), QRect(10, 20, 31, 40));
    EXPECT_EQ(resizeRegion(region, QPoint{0, -1}, limit), QRect(10, 20, 30, 39));
    // 收到只剩一格就停：再往回收不许产出 0 宽
    EXPECT_EQ(resizeRegion(QRect(10, 10, 1, 5), QPoint{-1, 0}, limit), QRect(10, 10, 1, 5));
    // 撑到屏边就停：右下角不许越界
    EXPECT_EQ(resizeRegion(QRect(0, 0, 100, 100), QPoint{1, 1}, limit), QRect(0, 0, 100, 100));
    EXPECT_TRUE(resizeRegion({}, QPoint{1, 1}, limit).isNull());
}

/**
 * @brief 钉住抓点的认序：角优先、再边、最后才是整块平移
 * @details 贴着角的那一点既在两条边的容差内也在区域内。认序颠倒会把「拉这个角」变成
 *          「拉这条边」或「推走整块」——三种动作在界面上看起来都是拖了，交出去的矩形却各不相同。
 */
TEST(ScreenUtils, GripRecognitionPrefersCornersThenEdges)
{
    const QRect region(100, 100, 50, 40); // right=149, bottom=139
    const auto  grip = [&region](const int x, const int y, const int tolerance = kSelectionGripTolerance) { return gripAt(region, QPoint(x, y), tolerance); };

    EXPECT_EQ(grip(100, 100), SelectionGrip::TopLeft);
    EXPECT_EQ(grip(149, 139), SelectionGrip::BottomRight);
    EXPECT_EQ(grip(149, 100), SelectionGrip::TopRight);
    EXPECT_EQ(grip(100, 139), SelectionGrip::BottomLeft);
    // 贴着角但还在容差内：仍是角，不许降级成边
    EXPECT_EQ(grip(104, 104), SelectionGrip::TopLeft);

    EXPECT_EQ(grip(100, 120), SelectionGrip::Left);
    EXPECT_EQ(grip(149, 120), SelectionGrip::Right);
    EXPECT_EQ(grip(120, 100), SelectionGrip::Top);
    EXPECT_EQ(grip(120, 139), SelectionGrip::Bottom);
    // 边线外一格也算抓住：一像素宽的线本来就要容差
    EXPECT_EQ(grip(156, 120), SelectionGrip::Right);

    EXPECT_EQ(grip(125, 125), SelectionGrip::Inside);
    EXPECT_EQ(grip(10, 10), SelectionGrip::None);
    // 空选区没有边可抓；负容差不该反过来把每个点都判成抓到了
    const QRect nothing;
    EXPECT_EQ(gripAt(nothing, QPoint(0, 0), kSelectionGripTolerance), SelectionGrip::None);
    EXPECT_EQ(grip(100, 100, -1), SelectionGrip::None);
}

/**
 * @brief 钉住每个抓点只动它自己那几条线
 * @details 右下角动了顶边、或左边拖大了宽度却没挪位置，都是「拖对了、形状错了」。
 *          这里把八个点各自的四边读数逐个钉住，连同越界夹取与 None 的原样返回。
 */
TEST(ScreenUtils, EachGripMovesOnlyItsOwnEdges)
{
    const QRect limit(0, 0, 100, 100);
    const QRect region(10, 10, 30, 20); // left=10 top=10 right=39 bottom=29
    const auto  drag = [&region, &limit](const SelectionGrip grip, const int dx, const int dy) { return dragRegionByGrip(region, grip, QPoint(dx, dy), limit); };

    EXPECT_EQ(drag(SelectionGrip::Inside, 5, 5), QRect(15, 15, 30, 20));
    EXPECT_EQ(drag(SelectionGrip::BottomRight, 5, 5), QRect(10, 10, 35, 25));
    EXPECT_EQ(drag(SelectionGrip::TopLeft, 5, 5), QRect(15, 15, 25, 15));
    EXPECT_EQ(drag(SelectionGrip::Left, -5, 0), QRect(5, 10, 35, 20));
    EXPECT_EQ(drag(SelectionGrip::Top, 0, 5), QRect(10, 15, 30, 15));
    EXPECT_EQ(drag(SelectionGrip::Right, 60, 0), QRect(10, 10, 90, 20)) << "右边不许越出这块屏";
    EXPECT_EQ(drag(SelectionGrip::Bottom, 0, 90), QRect(10, 10, 30, 90)) << "下边不许越出这块屏";
    // None 与空选区：原样交回，不许产出「一个格」的凭空选区
    EXPECT_EQ(drag(SelectionGrip::None, 50, 50), region);
    const QRect nothing;
    EXPECT_TRUE(dragRegionByGrip(nothing, SelectionGrip::Inside, QPoint(5, 5), limit).isNull());
}

/**
 * @brief 钉住拖过头时收成一条格而不是把选区翻面
 * @details 越过对边后交出去的是归一化矩形：原本要对齐的那条边会被丢掉，
 *          而遮罩上看着仍然像「拖到最小就停了」。
 */
TEST(ScreenUtils, GripDragStopsAtOnePixelInsteadOfFlipping)
{
    const QRect limit(0, 0, 100, 100);
    const QRect region(10, 10, 30, 20);
    const auto  drag = [&region, &limit](const SelectionGrip grip, const int dx, const int dy) { return dragRegionByGrip(region, grip, QPoint(dx, dy), limit); };

    // 拖过头时收成一条格，并且钉在「没被拖的那对角」上：右下角拖过头就塌在左上角，反之亦然
    const QRect collapsedAtTopLeft(10, 10, 1, 1);
    const QRect collapsedAtBottomRight(39, 29, 1, 1);
    EXPECT_EQ(drag(SelectionGrip::BottomRight, -50, -50), collapsedAtTopLeft);
    EXPECT_EQ(drag(SelectionGrip::TopLeft, 50, 50), collapsedAtBottomRight);

    const QRect oneRow(10, 10, 30, 1);
    EXPECT_EQ(drag(SelectionGrip::Bottom, 0, -50), oneRow) << "只拖下边不该动左右";
}

/**
 * @brief 钉住每个抓点的鼠标形状
 * @details 形状是「这里能抓」的唯一提示：指错方向时用户会以为自己抓错了边，
 *          而给回十字就等于没有提示。
 */
TEST(ScreenUtils, EachGripShowsItsOwnCursor)
{
    EXPECT_EQ(cursorForGrip(SelectionGrip::TopLeft), Qt::SizeFDiagCursor);
    EXPECT_EQ(cursorForGrip(SelectionGrip::BottomRight), Qt::SizeFDiagCursor);
    EXPECT_EQ(cursorForGrip(SelectionGrip::TopRight), Qt::SizeBDiagCursor);
    EXPECT_EQ(cursorForGrip(SelectionGrip::BottomLeft), Qt::SizeBDiagCursor);
    EXPECT_EQ(cursorForGrip(SelectionGrip::Left), Qt::SizeHorCursor);
    EXPECT_EQ(cursorForGrip(SelectionGrip::Right), Qt::SizeHorCursor);
    EXPECT_EQ(cursorForGrip(SelectionGrip::Top), Qt::SizeVerCursor);
    EXPECT_EQ(cursorForGrip(SelectionGrip::Bottom), Qt::SizeVerCursor);
    EXPECT_EQ(cursorForGrip(SelectionGrip::Inside), Qt::SizeAllCursor);
    EXPECT_EQ(cursorForGrip(SelectionGrip::None), Qt::CrossCursor);
}
