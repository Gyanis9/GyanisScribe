// 空白画布的两件纯计算：帧要自洽（物理=逻辑×缩放、来源区域留空），摆放要居中且不退到屏外。

#include "Annotation/BlankCanvas.h"

#include "Core/Types.h"

#include <gtest/gtest.h>

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

using GyanisScribe::Annotation::blankCanvasRegion;
using GyanisScribe::Annotation::kBlankCanvasSize;
using GyanisScribe::Annotation::makeBlankCanvas;

/**
 * @brief 钉住缩放屏上的画布自洽：物理像素是逻辑乘以 dpr，图上标的 dpr 与帧里那份同解
 * @details 标注窗与工具栏摆放按 logicalSize 走，成品按物理像素存——两者对不上就是
 *          「看着 1280 宽、存出来 2560 宽」那一类错位。
 */
TEST(BlankCanvas, KeepsPhysicalLogicalAndRatioConsistent)
{
    const QSize   logical{800, 600};
    const QString screenName{QString::fromUtf8("HDMI-0")};

    const auto canvas = makeBlankCanvas(logical, 2.0, screenName, false);

    EXPECT_EQ(canvas.image.width(), 1600) << "dpr 2 下物理宽该是逻辑的两倍";
    EXPECT_EQ(canvas.image.height(), 1200);
    EXPECT_EQ(canvas.logicalSize, logical) << "逻辑尺寸不该被物理像素顶掉";
    EXPECT_DOUBLE_EQ(canvas.devicePixelRatio, 2.0);
    EXPECT_DOUBLE_EQ(canvas.image.devicePixelRatio(), 2.0) << "图上的 dpr 没标：下游按 1.0 换算就整张糊一倍";
    EXPECT_EQ(canvas.screenName, screenName) << "屏名丢了，多屏上回不到该摆的那块屏";
    EXPECT_EQ(canvas.image.pixelColor(0, 0), QColor(Qt::white)) << "白底才是「能直接存出去」的那一档";
}

/**
 * @brief 钉住空白画布不声称自己有来源区域
 * @details 这一格是「刷新截图」的判据：留空它才会在这一轮自己拒掉；填上就等于许诺「还能对着那块地方重抓」，
 *          而画布根本不是抓出来的。
 */
TEST(BlankCanvas, CarriesNoSourceRegion)
{
    const auto canvas = makeBlankCanvas(kBlankCanvasSize, 1.0, QStringLiteral("eDP-1"), false);

    EXPECT_TRUE(canvas.logicalRegion.isNull()) << "来源区域被填上了：F5 会去重抓一块并不存在的来源";
}

/**
 * @brief 钉住退化入参照默认档走：非正的 dpr 与非正的尺寸都不许造出一张看不见也存不出的图
 */
TEST(BlankCanvas, FallsBackToDefaultsOnDegenerateInputs)
{
    const QSize zero{0, 500};

    const auto badRatio = makeBlankCanvas(kBlankCanvasSize, 0.0, QString{}, false);
    EXPECT_EQ(badRatio.image.width(), kBlankCanvasSize.width()) << "dpr 非正时按 1.0 处理，而不是把尺寸收成 0";
    EXPECT_DOUBLE_EQ(badRatio.devicePixelRatio, 1.0);

    const auto badSize = makeBlankCanvas(zero, 1.0, QString{}, false);
    EXPECT_EQ(badSize.logicalSize, kBlankCanvasSize) << "尺寸有一维非正就整张退回默认档，而不是留一条 0 宽的画布";
    EXPECT_GT(badSize.image.height(), 0);
}

/**
 * @brief 钉住画布摆在可用区中央，可用区更小时退回可用区而不是把画布甩到屏外
 */
TEST(BlankCanvasRegion, CentersAndShrinksToTheAvailableArea)
{
    const QRect available{100, 50, 1000, 800};
    const QSize small{400, 300};

    const QRect  placed = blankCanvasRegion(available, small);
    const QPoint centered{400, 300};
    EXPECT_EQ(placed.size(), small);
    EXPECT_EQ(placed.topLeft(), centered) << "左边留 (1000-400)/2=300、上边留 (800-300)/2=250，错一格都算摆歪";
    EXPECT_TRUE(available.contains(placed)) << "居中居中，摆到可用区外面去就不是居中了";

    const QSize huge{4000, 3000};
    const QRect cramped = blankCanvasRegion(available, huge);
    EXPECT_EQ(cramped, available) << "可用区给不起这张画布时退回可用区本身：宁可顶满也不甩到屏外";

    EXPECT_TRUE(blankCanvasRegion(QRect{}, small).isNull()) << "可用区为空时不该凭空造一块位置";
}

/**
 * @brief 钉住透明底那一档：整张全透明，而几何与白底那一档逐位相同
 * @details 判据打在手的绝对值上（每一格都是 0x00000000，白底那一路是 0xFFFFFFFF），不判「有没有变」——
 *          只钉一边时，「顺手把白底也改成半透明」这种坏法照样全绿。两档只差那一次填充。
 */
TEST(BlankCanvas, TransparentTierIsFullyTransparentWhileGeometryStays)
{
    const QSize   logical{400, 200};
    const QString screenName{QString::fromUtf8("DP-2")};

    const auto white       = makeBlankCanvas(logical, 2.0, screenName, false);
    const auto transparent = makeBlankCanvas(logical, 2.0, screenName, true);

    ASSERT_EQ(transparent.image.size(), white.image.size()) << "换底不该改画布大小";
    EXPECT_EQ(transparent.logicalSize, logical);
    EXPECT_EQ(transparent.devicePixelRatio, 2.0);
    EXPECT_EQ(transparent.screenName, screenName);
    EXPECT_TRUE(transparent.logicalRegion.isNull()) << "透明底也不该有来源区域：这一张不是从屏上抓来的";

    for (int y = 0; y < transparent.image.height(); y += 37)
    {
        for (int x = 0; x < transparent.image.width(); x += 41)
        {
            EXPECT_EQ(transparent.image.pixel(x, y), 0x00000000u) << x << ',' << y << " 这一格该是全透明";
        }
    }
    EXPECT_EQ(white.image.pixel(0, 0), 0xFFFFFFFFu) << "白底那一档要逐位是白且不透明";
}
