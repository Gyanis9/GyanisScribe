#include "Pin/PinTransform.h"

#include <QPoint>
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QTransform>

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
 * @brief 钉住缩略图方块按同一只距离比例定大小，并夹在自己的取值范围里
 * @details 与 scaleFromDrag 共用一条手感，但落到另一个量上：上限防的是「让地方的方块盖住半屏」，
 *          下限防的是小得认不出上面是什么。中心点起拖那一档同样保持原样。
 */
TEST(PinTransform, ThumbnailEdgeFollowsDragRatioAndClamps)
{
    EXPECT_TRUE(nearEqual(thumbnailEdgeFromDrag(48.0, 50.0, 100.0), 96.0)) << "拖到两倍远该把方块放大一倍";
    EXPECT_TRUE(nearEqual(thumbnailEdgeFromDrag(96.0, 100.0, 50.0), 48.0)) << "拖回一半该缩一半";
    EXPECT_TRUE(nearEqual(thumbnailEdgeFromDrag(60.0, 0.0, 500.0), 60.0)) << "在中心点起拖不该把方块瞬间缩到下限";
    EXPECT_TRUE(nearEqual(thumbnailEdgeFromDrag(48.0, 10.0, 100000.0), kMaximumThumbnailEdge)) << "放大要停在上限";
    EXPECT_TRUE(nearEqual(thumbnailEdgeFromDrag(48.0, 100.0, 1.0), kMinimumThumbnailEdge)) << "缩要停在下限";
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

/**
 * @brief 钉住贴边换算：夹回可用区、够近才贴、角上左/上优先
 * @details 期望值手算：1920×1080 的可用区、200×100 的贴图、容差 12。
 */
TEST(PinTransform, SnapsToEdgesAndClampsInsideTheScreen)
{
    const QRect available{0, 0, 1920, 1080};

    // 差 8 像素（在容差内）该贴到左边
    const auto nearLeft = snapToScreenEdges(QRect{8, 400, 200, 100}, available);
    EXPECT_EQ(nearLeft.edge, ScreenEdge::Left);
    EXPECT_EQ(nearLeft.rect.left(), 0) << "够近却没贴上去";

    // 差 40 像素（超出容差）保持用户放的位置，不许自己吸
    const auto farFromLeft = snapToScreenEdges(QRect{40, 400, 200, 100}, available);
    EXPECT_EQ(farFromLeft.edge, ScreenEdge::None);
    EXPECT_EQ(farFromLeft.rect.left(), 40) << "离得远也被吸走了";

    // 右边与下边各差 5 像素
    const auto nearRight = snapToScreenEdges(QRect{1920 - 200 - 5, 400, 200, 100}, available);
    EXPECT_EQ(nearRight.edge, ScreenEdge::Right);
    EXPECT_EQ(nearRight.rect.right(), 1919) << "右边没对齐到可用区右沿";

    const auto nearBottom = snapToScreenEdges(QRect{700, 1080 - 100 - 5, 200, 100}, available);
    EXPECT_EQ(nearBottom.edge, ScreenEdge::Bottom);
    EXPECT_EQ(nearBottom.rect.bottom(), 1079);

    // 角上两条边都够近：左右先判，所以是 Left（与录屏控制条那边同一口径）
    const auto corner = snapToScreenEdges(QRect{3, 3, 200, 100}, available);
    EXPECT_EQ(corner.edge, ScreenEdge::Left);
    EXPECT_EQ(corner.rect.topLeft(), QPoint(0, 0));

    // 夹取：拖出屏幕外要夹回来，不然这张贴图从此点不到
    const auto outside = snapToScreenEdges(QRect{2200, -80, 200, 100}, available);
    EXPECT_EQ(outside.rect, QRect(1920 - 200, 0, 200, 100)) << "越界的贴图没被夹回可用区";

    // 可用区退化时原样交回，不猜一个位置
    const auto degenerate = snapToScreenEdges(QRect{10, 20, 30, 40}, QRect{});
    EXPECT_EQ(degenerate.edge, ScreenEdge::None);
    EXPECT_EQ(degenerate.rect, QRect(10, 20, 30, 40));
}

/**
 * @brief 钉住贴边收起后的位置：收起时只在屏上留一条，露出时贴齐边
 */
TEST(PinTransform, DockedRectKeepsAStripOnScreen)
{
    const QRect available{0, 0, 1920, 1080};
    const QRect revealed{0, 300, 200, 100};

    EXPECT_EQ(dockedRect(revealed, available, ScreenEdge::None, true), revealed) << "没贴边的贴图不该被推到屏外";
    EXPECT_EQ(dockedRect(revealed, available, ScreenEdge::Left, false), revealed) << "露出态就是贴齐后的那个矩形";

    const auto leftCollapsed = dockedRect(revealed, available, ScreenEdge::Left, true);
    EXPECT_EQ(leftCollapsed.right(), kEdgeStripWidth - 1) << "左边收起后留在屏上的那条边宽度不对";
    EXPECT_EQ(leftCollapsed.width(), revealed.width()) << "收起只挪位置，不改尺寸";

    const auto rightCollapsed = dockedRect(QRect{1720, 300, 200, 100}, available, ScreenEdge::Right, true);
    EXPECT_EQ(rightCollapsed.left(), available.right() - kEdgeStripWidth + 1) << "右边收起后该只剩贴右边的一条";

    const auto topCollapsed = dockedRect(QRect{600, 0, 200, 100}, available, ScreenEdge::Top, true);
    EXPECT_EQ(topCollapsed.bottom(), kEdgeStripWidth - 1);

    const auto bottomCollapsed = dockedRect(QRect{600, 980, 200, 100}, available, ScreenEdge::Bottom, true);
    EXPECT_EQ(bottomCollapsed.top(), available.bottom() - kEdgeStripWidth + 1);
}

/**
 * @brief 钉住「窗口点 → 图片像素」的换算：把每个像素中心按 Qt 的旋转矩阵投出去，再反算回来
 * @details 期望值不走被测那套三角公式，而是用 QTransform 正向投出去（与绘制约定同源）：
 *          反变换只要少一个负号或把缩放漏掉，就会有格子对不上。
 */
TEST(PinTransform, MapsWindowPointsBackToThePixelTheyCameFrom)
{
    const QRectF content{0.0, 0.0, 300.0, 200.0};
    const QSize  pixels{600, 400}; // 物理像素是逻辑的两倍，dpr 2 那种
    const double degrees = 30.0;

    for (int x = 0; x < pixels.width(); x += 37)
    {
        for (int y = 0; y < pixels.height(); y += 29)
        {
            QTransform transform;
            transform.translate(content.center().x(), content.center().y());
            transform.rotate(degrees);
            transform.translate(-content.center().x(), -content.center().y());
            const QPointF windowPoint =
                    transform.map(QPointF(content.left() + (x + 0.5) * content.width() / pixels.width(), content.top() + (y + 0.5) * content.height() / pixels.height()));

            const auto mapped = imagePixelAt(windowPoint, content, pixels, degrees);
            ASSERT_TRUE(mapped.has_value()) << "投出去的点反算不回来：" << x << ',' << y;
            EXPECT_EQ(*mapped, QPoint(x, y)) << "像素中心投出去再反算回来落到了别的格";
        }
    }

    EXPECT_FALSE(imagePixelAt(QPointF{-5.0, -5.0}, content, pixels, degrees).has_value()) << "画布外的点不该给出像素";
    EXPECT_FALSE(imagePixelAt(QPointF{10.0, 10.0}, QRectF{}, pixels, degrees).has_value()) << "退化画布要交回空";
    EXPECT_FALSE(imagePixelAt(QPointF{10.0, 10.0}, content, QSize{}, degrees).has_value()) << "没有像素的图要交回空";

    // 转 0 度时按缩放直接折：400×200 的图铺进 300×200 的画布，窗口 (150,100) 落在图心
    const QSize small{400, 200};
    const auto  centre = imagePixelAt(QPointF{150.0, 100.0}, content, small, 0.0);
    ASSERT_TRUE(centre.has_value());
    EXPECT_EQ(*centre, QPoint(200, 100)) << "不旋时的等比折算不对";
}

/**
 * @brief 钉住翻转下的反算：四种翻转组合都要与绘制那一步互逆
 * @details 绘制是「先按中心翻转、后绕中心旋转」（转的是翻好的那一份），所以这里用
 *          QTransform 按同样的次序搭一份独立参照，把像素中心投出去再反算回来。
 *          另外配一条手算锚点：90° + 左右翻转时，窗口 (75,75) 必须落回 (0,0)——
 *          只测互逆的话，两处一起把次序写反（先镜像后反旋转 / 先反旋转后镜像）照样绿。
 */
TEST(PinTransform, MapsWindowPointsBackUnderMirrors)
{
    const QRectF content{0.0, 0.0, 300.0, 200.0};
    const QSize  pixels{600, 400};

    for (const bool horizontal: {false, true})
    {
        for (const bool vertical: {false, true})
        {
            const double degrees = 40.0;
            for (int x = 0; x < pixels.width(); x += 61)
            {
                for (int y = 0; y < pixels.height(); y += 43)
                {
                    QTransform transform;
                    transform.translate(content.center().x(), content.center().y());
                    transform.rotate(degrees);
                    transform.scale(horizontal ? -1.0 : 1.0, vertical ? -1.0 : 1.0);
                    transform.translate(-content.center().x(), -content.center().y());
                    const QPointF windowPoint =
                            transform.map(QPointF(content.left() + (x + 0.5) * content.width() / pixels.width(), content.top() + (y + 0.5) * content.height() / pixels.height()));

                    const auto mapped = imagePixelAt(windowPoint, content, pixels, degrees, horizontal, vertical);
                    ASSERT_TRUE(mapped.has_value()) << "翻转组合 " << horizontal << vertical << " 投出去的点反算不回来：" << x << ',' << y;
                    EXPECT_EQ(*mapped, QPoint(x, y)) << "翻转组合 " << horizontal << vertical << " 的反算落到了别的格";
                }
            }
        }
    }

    // 手算锚点：100×50 的图铺进同尺寸画布、转 90°、左右翻转。
    // 源像素 (0,0) 在画布中心左边 50、上边 25 → 先翻转成右边 50、上边 25 → 顺时针 90° 后到底边中段 → 窗口 (75,75)
    const QRectF tallContent{0.0, 0.0, 100.0, 50.0};
    const QSize  samePixels{100, 50};
    const auto   anchored = imagePixelAt(QPointF{75.0, 75.0}, tallContent, samePixels, 90.0, true, false);
    ASSERT_TRUE(anchored.has_value()) << "手算锚点没落回图内";
    EXPECT_EQ(*anchored, QPoint(0, 0)) << "90° + 左右翻转的反算次序不对";
    EXPECT_FALSE(imagePixelAt(QPointF{75.0, 75.0}, tallContent, samePixels, 90.0, false, false).has_value()) << "同一点在不翻转时该落回图外";
}

/**
 * @brief 钉住裁剪框的按块换算：左闭右开，右边界那一格不多算、贴边那一列不少算
 * @details 1:1 手算：40×40 折进 40×40 的画布，(10,10) 拖到 (20,30) 该裁出 10×20——
 *          拿两个角去问点命中会得到 11×21（右/下边界那一格被多算一列）。
 *          缩放档：300×200 的画布铺 600×400 的图，(30,20) 拖到 (300,180) 折成 (60,40)+(540,320)。
 */
TEST(PinTransform, CropRectFoldsLeftClosedRightOpen)
{
    const QRectF content{0.0, 0.0, 40.0, 40.0};
    const QSize  pixels{40, 40};

    const auto box = imagePixelRect(QRectF(10.0, 10.0, 10.0, 20.0), content, pixels);
    ASSERT_TRUE(box.has_value());
    EXPECT_EQ(*box, QRect(10, 10, 10, 20)) << "1:1 的裁剪框折算不对（多半是右/下边界多算了一格）";

    const auto full = imagePixelRect(QRectF(0.0, 0.0, 40.0, 40.0), content, pixels);
    ASSERT_TRUE(full.has_value());
    EXPECT_EQ(*full, QRect(0, 0, 40, 40)) << "整张的框该覆盖到最后一行/列，不该被排掉";

    const QRectF scaledContent{0.0, 0.0, 300.0, 200.0};
    const auto   scaled = imagePixelRect(QRectF(30.0, 20.0, 270.0, 160.0), scaledContent, QSize(600, 400));
    ASSERT_TRUE(scaled.has_value());
    EXPECT_EQ(*scaled, QRect(60, 40, 540, 320)) << "带缩放的裁剪框折算不对";

    // 拖出画布的部分先夹回来：(-50,-50) 起、到 (10,10) 止，只算画布里的那 10×10
    const auto clamped = imagePixelRect(QRectF(-50.0, -50.0, 60.0, 60.0), content, pixels);
    ASSERT_TRUE(clamped.has_value());
    EXPECT_EQ(*clamped, QRect(0, 0, 10, 10)) << "画布外的部分没被夹掉";

    EXPECT_FALSE(imagePixelRect(QRectF(-50.0, -50.0, 10.0, 10.0), content, pixels).has_value()) << "整框都在画布外该交回空";
    EXPECT_FALSE(imagePixelRect(QRectF(10.0, 10.0, 5.0, 5.0), QRectF{}, pixels).has_value()) << "退化画布要交回空";
}

/**
 * @brief 钉住贴图对齐吸附：每轴吸一条线，容差之内取位移最小的那一条
 */
TEST(SnapToCandidateEdges, TakesTheSmallestShiftWithinTolerance)
{
    const QRect       moved{100, 100, 40, 40};
    const std::vector candidates{QRect{143, 200, 10, 10}};

    // 候选的左边线 143 距 moved 的右边线 139 只差 4，比它的中心线（148，差 9）更近；y 轴没有够近的线，原地不动
    EXPECT_EQ(snapToCandidateEdges(moved, candidates), (QRect{104, 100, 40, 40})) << "没吸到候选的左边线上";
}

/**
 * @brief 钉住中心线也参与对齐：一窄一宽两张要「居中摆齐」，只比外边线就永远差半格
 */
TEST(SnapToCandidateEdges, AlignsCenterLinesToo)
{
    const QRect       moved{101, 100, 40, 40};
    const std::vector candidates{QRect{60, 900, 120, 10}};

    EXPECT_EQ(snapToCandidateEdges(moved, candidates), (QRect{100, 100, 40, 40})) << "moved 的中心线 121 该吸到候选的中心线 120 上";
}

/**
 * @brief 钉住没有参照、够不着参照与容差非法时都原样交回
 */
TEST(SnapToCandidateEdges, LeavesTheRectAloneWhenNothingQualifies)
{
    const QRect              moved{100, 100, 40, 40};
    const std::vector<QRect> none;
    const std::vector        far{QRect{400, 400, 10, 10}};

    EXPECT_EQ(snapToCandidateEdges(moved, none), moved) << "没有参照就该原样交回";
    EXPECT_EQ(snapToCandidateEdges(moved, far), moved) << "最近的线也在容差之外就不该吸";
    EXPECT_EQ(snapToCandidateEdges(moved, far, -1), moved) << "容差为负是非法输入，不该被当成全都算够近";
}

/**
 * @brief 钉住棋盘贴片的形状：2×2 格、两色对角，非正数的格长交出空图
 * @details 贴片是平铺出来的最小单位：只有一色就是「画成一整块实心」那个坏法，在这儿当场看得见。
 *          cell 不是正数时交出空贴片而不是 1×1 的一格——调用方拿空刷子什么都不画，比糊出一张点图诚实。
 */
TEST(PinCheckerTile, AlternatesTwoColorsAndRejectsNonPositiveCell)
{
    const QColor first{10, 20, 30};
    const QColor second{200, 210, 220};

    const auto tile = checkerboardTile(first, second, 4);
    ASSERT_EQ(tile.size(), QSize(8, 8)) << "贴片该是 2×2 格，平铺之后才连得成棋盘";
    EXPECT_EQ(tile.pixelColor(1, 1), first) << "左上格";
    EXPECT_EQ(tile.pixelColor(5, 1), second) << "右上格该换成另一色";
    EXPECT_EQ(tile.pixelColor(1, 5), second) << "左下格该换成另一色";
    EXPECT_EQ(tile.pixelColor(5, 5), first) << "右下格该回到第一色";

    EXPECT_TRUE(checkerboardTile(first, second, 0).isNull()) << "格长为 0 要交出空贴片";
    EXPECT_TRUE(checkerboardTile(first, second, -3).isNull()) << "负格长同样是非法输入，不该被折成一格";
}

/**
 * @brief 钉住只有两档棋盘给得出颜色对，露底色与桌面透出那两档什么都不画
 * @details 深色档两色由暗到亮、亮色档由亮到暗：这是「摆在白文档上」与「摆在深色桌面上」两种用途的分界，
 *          写反了就成了深色档画亮棋盘。
 */
TEST(PinCheckerColors, OnlyTheTwoCheckerModesDrawATile)
{
    EXPECT_FALSE(checkerColorsFor(TransparentBackdrop::ThemeBase).has_value()) << "跟随窗口底色不该画棋盘";
    EXPECT_FALSE(checkerColorsFor(TransparentBackdrop::SeeThrough).has_value()) << "桌面透出该让那块空着，垫上棋盘就把桌面又挡住了";

    const auto dark  = checkerColorsFor(TransparentBackdrop::DarkChecker);
    const auto light = checkerColorsFor(TransparentBackdrop::LightChecker);
    ASSERT_TRUE(dark.has_value());
    ASSERT_TRUE(light.has_value());
    EXPECT_LT(dark->first.lightness(), dark->second.lightness()) << "深色档的两色反了顺序，摆在白底上就看不出边界";
    EXPECT_GT(light->first.lightness(), light->second.lightness()) << "亮色档同理";
    EXPECT_NE(dark->first, light->first) << "深浅两档撞成同一组色，那一档等于白给";
}

/**
 * @brief 钉住「吸附距离」的校验区间：0 是一档合法设置，越界要给出路
 * @details 0 意味着「完全不吸」，不是「没填」，把它当坏值拒会让这一档填不出来；
 *          区间只住在这一个函数里——设置页的数字框与贴图的读端都从这里取，两处各写一遍就会一处放行一处拒绝。
 */
TEST(PinEdgeSnapDistance, AcceptsZeroAndRejectsOutOfRange)
{
    for (const int good: {GyanisScribe::Pin::kEdgeSnapDistanceMinimum, 1, GyanisScribe::Pin::kEdgeSnapTolerance, GyanisScribe::Pin::kEdgeSnapDistanceMaximum})
    {
        const auto parsed = GyanisScribe::Pin::parseEdgeSnapDistance(good);
        ASSERT_TRUE(parsed.has_value()) << good << "：" << parsed.error().message;
        EXPECT_EQ(*parsed, good);
    }

    for (const int bad: {-1, GyanisScribe::Pin::kEdgeSnapDistanceMaximum + 1})
    {
        const auto rejected = GyanisScribe::Pin::parseEdgeSnapDistance(bad);
        ASSERT_FALSE(rejected.has_value()) << bad << " 这个越界值被收下了";
        EXPECT_EQ(rejected.error().code, GyanisScribe::Base::ErrorCode::InvalidArgument) << bad;
        EXPECT_NE(rejected.error().message.find("Pin/EdgeSnapDistance"), std::string::npos) << "拒绝要说清删哪一格能回默认，实际：" << rejected.error().message;
    }
}

/**
 * @brief 钉住按倍率算尺寸：逐维四舍五入、每维保底一格、非正倍率原样交回
 * @details 期望值手算：1.5 倍的 101×51 是 151.5×76.5，四舍五入到 152×77（截断会得到 151×76，
 *          来回两次就少几格）；0.5 倍的 101×51 是 50.5×25.5，进位到 51×26。
 */
TEST(PinTransform, ScalesASizeByRatioRoundingHalfUp)
{
    EXPECT_EQ(GyanisScribe::Pin::sizeScaledBy(QSize{101, 51}, 1.5), QSize(152, 77)) << "除不尽时四舍五入，不是截断";
    EXPECT_EQ(GyanisScribe::Pin::sizeScaledBy(QSize{101, 51}, 0.5), QSize(51, 26));
    EXPECT_EQ(GyanisScribe::Pin::sizeScaledBy(QSize{640, 360}, 2.0), QSize(1280, 720)) << "整数倍要逐位相等";
    EXPECT_EQ(GyanisScribe::Pin::sizeScaledBy(QSize{100, 100}, 0.001), QSize(1, 1)) << "缩到零格就没有窗口了，每维保底一格";
    EXPECT_EQ(GyanisScribe::Pin::sizeScaledBy(QSize{100, 100}, 0.0), QSize(100, 100)) << "非正倍率不是「缩到零」，原样交回";
}
