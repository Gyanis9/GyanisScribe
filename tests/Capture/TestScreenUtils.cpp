#include "Capture/ScreenUtils.h"

#include "Core/SizeText.h"

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
using GyanisScribe::Capture::cursorRectInPixels;
using GyanisScribe::Capture::dragRegionByGrip;
using GyanisScribe::Capture::drawCursorInto;
using GyanisScribe::Capture::formatSelectionHint;
using GyanisScribe::Capture::gripAt;
using GyanisScribe::Capture::kSelectionGripTolerance;
using GyanisScribe::Capture::nudgeRegion;
using GyanisScribe::Capture::ratioLockedPoint;
using GyanisScribe::Capture::regionAtPoint;
using GyanisScribe::Capture::regionFromText;
using GyanisScribe::Capture::regionToText;
using GyanisScribe::Capture::resizeRegion;
using GyanisScribe::Capture::SelectionGrip;
using GyanisScribe::Capture::squareLockedPoint;
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

/**
 * @brief 钉住指针落点：只有位置乘缩放因子，形状尺寸不动
 * @details 热点回退排在乘因子之后，反过来的话高 DPI 屏上尖端会偏出好几个像素；
 *          缩放因子非正时退回按 1
 * 算，而不是判成「画不出去」返回空矩形。
 */
TEST(ScreenUtils, CursorRectScalesOnlyItsPosition)
{
    GyanisScribe::Capture::CursorSprite sprite;
    sprite.image   = QImage(12, 18, QImage::Format_ARGB32_Premultiplied);
    sprite.hotspot = QPoint(3, 6);

    sprite.position = QPoint(100, 80);
    EXPECT_EQ(cursorRectInPixels(sprite, 1.0), QRect(97, 74, 12, 18));
    EXPECT_EQ(cursorRectInPixels(sprite, 2.0), QRect(197, 154, 12, 18)) << "12×18 变成 24×36 就是把形状也乘了一遍，指针会大一圈";
    // 1.5 这一档的半格按四舍五入落格：16.5 → 17
    sprite.position = QPoint(11, 11);
    EXPECT_EQ(cursorRectInPixels(sprite, 1.5), QRect(14, 11, 12, 18));
    // 多屏时来源已经把位置折回屏内，这里只按屏内读数算
    sprite.position = QPoint(10, 500);
    EXPECT_EQ(cursorRectInPixels(sprite, 1.0), QRect(7, 494, 12, 18));
    // 缩放因子非正：退回按 1 算
    sprite.position = QPoint(50, 50);
    EXPECT_EQ(cursorRectInPixels(sprite, 0.0), QRect(47, 44, 12, 18));
    // 尖端贴着屏左上角时形状会探出屏外：允许负落点，裁切交给画的那一步
    sprite.position = QPoint(1, 1);
    EXPECT_EQ(cursorRectInPixels(sprite, 1.0), QRect(-2, -5, 12, 18));
}

/**
 * @brief 钉住画进帧里的指针只改自己覆盖的那几格
 * @details 落点算对了还得真画得出来：不透明的格子换成指针色、透明的格子一个字都不动，
 *
 * 这两格一起才证明混合走的是源-over，而不是整块覆盖或整块跳过。
 */
TEST(ScreenUtils, DrawCursorReplacesOnlyItsOwnPixels)
{
    CapturedImage frame;
    frame.image = QImage(20, 20, QImage::Format_ARGB32_Premultiplied);
    frame.image.fill(QColor(255, 0, 0));
    frame.devicePixelRatio = 1.0;

    GyanisScribe::Capture::CursorSprite sprite;
    sprite.image = QImage(4, 4, QImage::Format_ARGB32_Premultiplied);
    sprite.image.fill(QColor(0, 255, 0));
    sprite.image.setPixelColor(0, 0, Qt::transparent); // 左上角留一格全透明，混合要放过它
    sprite.hotspot  = QPoint(0, 0);
    sprite.position = QPoint(5, 5);

    drawCursorInto(frame, sprite);

    const auto green = QColor(0, 255, 0);
    const auto red   = QColor(255, 0, 0);
    EXPECT_EQ(frame.image.pixelColor(5, 5), red) << "指针上全透明那一格被盖上了颜色，混合做成了整格覆盖";
    EXPECT_EQ(frame.image.pixelColor(6, 5), green);
    EXPECT_EQ(frame.image.pixelColor(8, 8), green);
    EXPECT_EQ(frame.image.pixelColor(4, 5), red) << "指针外的像素被动了，落点或尺寸算错";
    EXPECT_EQ(frame.image.pixelColor(9, 9), red);
}

/**
 * @brief 钉住高 DPI 那一档画出来的是原尺寸的指针、放在翻倍后的位置上
 * @details 这条是「只换算位置」的现场判据：形状若也跟着乘因子，绿块会铺到 8×8；
 *
 * 位置若没乘，绿块会落在 (10,10) 而不是 (20,20)。
 */
TEST(ScreenUtils, DrawCursorOnHighDpiFrameKeepsSpriteSize)
{
    CapturedImage frame;
    frame.image = QImage(40, 40, QImage::Format_ARGB32_Premultiplied);
    frame.image.fill(QColor(255, 0, 0));
    frame.devicePixelRatio = 2.0;

    GyanisScribe::Capture::CursorSprite sprite;
    sprite.image = QImage(4, 4, QImage::Format_ARGB32_Premultiplied);
    sprite.image.fill(QColor(0, 255, 0));
    sprite.hotspot  = QPoint(0, 0);
    sprite.position = QPoint(10, 10);

    drawCursorInto(frame, sprite);

    const auto green = QColor(0, 255, 0);
    const auto red   = QColor(255, 0, 0);
    EXPECT_EQ(frame.image.pixelColor(20, 20), green) << "落点没按缩放因子翻倍";
    EXPECT_EQ(frame.image.pixelColor(23, 23), green);
    EXPECT_EQ(frame.image.pixelColor(19, 20), red);
    EXPECT_EQ(frame.image.pixelColor(24, 24), red) << "绿块铺过了 4×4，形状被放大了";
}

/**
 * @brief 钉住指针贴到屏边与屏外时的裁切
 * @details 截整屏时光标常在角落：越界的那一半要裁掉而不是回绕到对面，
 *          回绕会在左上角多出一块没人见过的指针。

 */
TEST(ScreenUtils, DrawCursorClipsAtScreenEdges)
{
    GyanisScribe::Capture::CursorSprite sprite;
    sprite.image = QImage(4, 4, QImage::Format_ARGB32_Premultiplied);
    sprite.image.fill(QColor(0, 255, 0));
    sprite.hotspot = QPoint(0, 0);

    CapturedImage corner;
    corner.image = QImage(20, 20, QImage::Format_ARGB32_Premultiplied);
    corner.image.fill(QColor(255, 0, 0));
    corner.devicePixelRatio = 1.0;
    sprite.position         = QPoint(18, 18);
    drawCursorInto(corner, sprite);
    EXPECT_EQ(corner.image.pixelColor(19, 19), QColor(0, 255, 0)) << "右下角只剩 2×2 进得来，那一格必须画到";
    EXPECT_EQ(corner.image.pixelColor(17, 19), QColor(255, 0, 0));

    CapturedImage offScreen;
    offScreen.image = QImage(20, 20, QImage::Format_ARGB32_Premultiplied);
    offScreen.image.fill(QColor(255, 0, 0));
    offScreen.devicePixelRatio = 1.0;
    sprite.position            = QPoint(-5, -5);
    drawCursorInto(offScreen, sprite);
    EXPECT_EQ(offScreen.image.pixelColor(0, 0), QColor(255, 0, 0)) << "屏外的指针回绕进画面了";
}

/**
 * @brief 钉住半透明的指针边缘真的做了混合
 * @details 指针的抗锯齿边是半透明的：只做整格覆盖就会在浅色背景上留一圈硬边，
 *          这一条钉的是「介于两色之间」，具体数值交给
 * Qt 自己算。
 */
TEST(ScreenUtils, DrawCursorBlendsSemiTransparentEdges)
{
    CapturedImage frame;
    frame.image = QImage(8, 8, QImage::Format_ARGB32_Premultiplied);
    frame.image.fill(QColor(255, 0, 0));
    frame.devicePixelRatio = 1.0;

    GyanisScribe::Capture::CursorSprite sprite;
    sprite.image = QImage(2, 2, QImage::Format_ARGB32_Premultiplied);
    sprite.image.fill(QColor(0, 255, 0, 128));
    sprite.hotspot  = QPoint(0, 0);
    sprite.position = QPoint(1, 1);

    drawCursorInto(frame, sprite);

    const auto blended = frame.image.pixelColor(1, 1);
    EXPECT_GT(blended.red(), 0) << "混合后红通道被抹平，等于整格覆盖";
    EXPECT_LT(blended.red(), 255);
    EXPECT_GT(blended.green(), 0);
    EXPECT_LT(blended.green(), 255);
}

/**
 * @brief 钉住 Shift 锁正方形的三条：不上锁原样、上锁取大边、贴边夹完仍是正方形
 * @details 期望值手算：起手 (100,100)。拖到 (300,200) 差 (200,100) → 取大边 200 → (300,300)；
 *          往左上拖要朝那侧展开；夹到屏边时（0..1919、0..1079）不许把等边破坏掉。
 */
TEST(ScreenUtilsSelection, SquareLockTakesTheLargerExtentAndKeepsTheSquareAtEdges)
{
    const QRect limit{0, 0, 1920, 1080};

    EXPECT_EQ(squareLockedPoint(QPoint{100, 100}, QPoint{300, 200}, false, limit), QPoint(300, 200)) << "没上锁时不该改端点";
    EXPECT_EQ(squareLockedPoint(QPoint{100, 100}, QPoint{300, 200}, true, limit), QPoint(300, 300)) << "上锁后该取两个方向里大的那一边";
    EXPECT_EQ(squareLockedPoint(QPoint{100, 100}, QPoint{150, 300}, true, limit), QPoint(300, 300)) << "纵向更大时对称展开";

    // 往左上拖：正方形该朝左上展开
    EXPECT_EQ(squareLockedPoint(QPoint{300, 300}, QPoint{100, 200}, true, limit), QPoint(100, 100));

    // 贴到左边：夹取之后仍是正方形——按两轴里更小的那个偏移收回来，x 停在屏的左沿
    const auto clamped = squareLockedPoint(QPoint{10, 500}, QPoint{0, 600}, true, limit);
    EXPECT_EQ(clamped.x(), limit.left()) << "该被夹到屏的左沿";
    EXPECT_EQ(std::abs(clamped.x() - 10), std::abs(clamped.y() - 500)) << "夹到屏边之后不是正方形了";
}

/**
 * @brief 钉住任意长宽比锁：按比例格数展开、贴边收缩后比例不变、减掉一格落成精确比例
 * @details 期望值手算：起手 (100,100)、比例 16:9。
 *          拖到 (420,260)：差 (320,160)，需要 ceil(320/16)=20 与 ceil(160/9)=18 取大 → 20 格，
 *          端点 = 100 + 20×16 - 1 = 419 → 选区宽 420-100+1 = 320 = 20 格 ✓（不减那一格会出 321×181）。
 *          贴左边时按能落下的格数收缩，宽高仍是 16:9。
 */
TEST(ScreenUtilsSelection, RatioLockKeepsTheExactRatioAndShrinksAtEdges)
{
    const QRect limit{0, 0, 1920, 1080};
    const QSize ratio{16, 9};

    // 没锁（空比例）时原样交回
    EXPECT_EQ(ratioLockedPoint(QPoint{100, 100}, QPoint{300, 200}, QSize{}, limit), QPoint(300, 200)) << "没上锁时不该改端点";

    // 横向主导：20 格 × 16:9
    EXPECT_EQ(ratioLockedPoint(QPoint{100, 100}, QPoint{420, 260}, ratio, limit), QPoint(419, 279)) << "按格数展开的端点不对";
    // 纵向主导：拖到 (150, 400)，差 (50,300)：ceil(300/9)=34 格 → (644, 405)? 手算：34×16-1=543 → x=643；34×9-1=305 → y=405
    EXPECT_EQ(ratioLockedPoint(QPoint{100, 100}, QPoint{150, 400}, ratio, limit), QPoint(643, 405)) << "纵向主导时该按纵轴要的格数定尺度";

    // 往左上拖：朝左上展开。差 (-200,-100)：ceil(200/16)=13 格与 ceil(100/9)=12 取大 → 13 格
    EXPECT_EQ(ratioLockedPoint(QPoint{500, 500}, QPoint{300, 400}, ratio, limit), QPoint(500 - 13 * 16 + 1, 500 - 13 * 9 + 1));

    // 贴左边：能落下的格数 = min(500/16, 500/9) = 31 格 → 宽 496、高 279，比例仍是 16:9
    const auto clamped = ratioLockedPoint(QPoint{500, 500}, QPoint{0, 0}, ratio, limit);
    EXPECT_EQ(500 - clamped.x(), 16 * 31 - 1);
    EXPECT_EQ(500 - clamped.y(), 9 * 31 - 1);
}

/**
 * @brief 钉住尺寸与长宽比文本的解析与书写：认得四种分隔符，写的是约分后的样子
 */
TEST(ScreenUtilsSelection, SizeAndAspectTextRoundTrip)
{
    using GyanisScribe::Capture::aspectRatioFromText;
    using GyanisScribe::Capture::aspectRatioToText;
    using GyanisScribe::Core::parseSizeText;

    EXPECT_EQ(parseSizeText(QStringLiteral("300x200")), (QSize(300, 200)));
    EXPECT_EQ(parseSizeText(QStringLiteral("300×200")), (QSize(300, 200)));
    EXPECT_EQ(parseSizeText(QStringLiteral("300,200")), (QSize(300, 200)));
    EXPECT_EQ(parseSizeText(QStringLiteral(" 300 200 ")), (QSize(300, 200)));
    EXPECT_FALSE(parseSizeText(QStringLiteral("300")).has_value()) << "只有一个数不算尺寸";
    EXPECT_FALSE(parseSizeText(QStringLiteral("300x0")).has_value()) << "零高度不算尺寸";
    EXPECT_FALSE(parseSizeText(QStringLiteral("三百x两百")).has_value());
    EXPECT_FALSE(parseSizeText(QString()).has_value());

    EXPECT_EQ(aspectRatioToText(QSize{1920, 1080}), QStringLiteral("16:9")) << "写出去的要先约分";
    EXPECT_EQ(aspectRatioToText(QSize{100, 50}), QStringLiteral("2:1"));
    EXPECT_EQ(aspectRatioToText(QSize{}), QString());
    EXPECT_EQ(aspectRatioFromText(QStringLiteral("16:9")), (QSize(16, 9)));
    EXPECT_EQ(aspectRatioFromText(QStringLiteral("32:18")), (QSize(16, 9))) << "读进来也要约分，运算里数字才小";
    EXPECT_FALSE(aspectRatioFromText(QStringLiteral("16")).has_value()) << "缺冒号不算比例";
    EXPECT_FALSE(aspectRatioFromText(QStringLiteral("16:0")).has_value());
    EXPECT_FALSE(aspectRatioFromText(QStringLiteral("16:9:1")).has_value()) << "多余的尾巴不该被忽略";
}

/**
 * @brief 钉住圆角切图：四角变透明、边与内里一个像素不动、半径非正原样交回
 * @details 40×30 的实心红图切半径 8：角尖 (0,0) 一定在圆角之外；(0,15) 是左边中点、必在；
 *
 * 圆心附近 (8,8) 恰在弧上，不取——抗锯齿会给出半透明的答案，判据不拿它当靶子。
 */
TEST(ScreenUtils, RoundedCornersBecomeTransparentAndKeepTheMiddle)
{
    using GyanisScribe::Capture::roundedCornerImage;

    QImage square{40, 30, QImage::Format_ARGB32_Premultiplied};
    square.fill(QColor(255, 0, 0));

    const auto rounded = roundedCornerImage(square, 8);
    ASSERT_EQ(rounded.size(), square.size());
    EXPECT_EQ(rounded.pixelColor(0, 0).alpha(), 0) << "角尖没被切掉";
    EXPECT_EQ(rounded.pixelColor(20, 15), QColor(255, 0, 0)) << "内里被动过了";
    EXPECT_EQ(rounded.pixelColor(0, 15), QColor(255, 0, 0)) << "左边中点不该被切到";
    EXPECT_EQ(rounded.pixelColor(39, 15), QColor(255, 0, 0)) << "右边中点不该被切到";

    EXPECT_EQ(roundedCornerImage(square, 0), square) << "半径为 0 时该原样交回";
}

/**
 * @brief 钉住三分网格线的位置：两竖在 1/3 与 2/3 处贯穿全高，两横同理
 */
TEST(ScreenUtils, ThirdsGridSplitsTheRegionIntoThirds)
{
    using GyanisScribe::Capture::thirdsGridLinesFor;

    const auto lines = thirdsGridLinesFor(QRect(100, 50, 90, 60)); // 1/3 = 30、2/3 = 60
    EXPECT_EQ(lines[0], (QLine(130, 50, 130, 109))) << "第一条竖线不在三分之一处";
    EXPECT_EQ(lines[1], (QLine(160, 50, 160, 109))) << "第二条竖线不在三分之二处";
    EXPECT_EQ(lines[2], (QLine(100, 70, 189, 70))) << "第一条横线不在三分之一处";
    EXPECT_EQ(lines[3], (QLine(100, 90, 189, 90))) << "第二条横线不在三分之二处";
}
