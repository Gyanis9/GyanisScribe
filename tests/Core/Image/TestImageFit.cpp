#include "Core/Image/ImageFit.h"

#include <gtest/gtest.h>

#include <QPoint>
#include <QRect>
#include <QSize>

using GyanisScribe::Core::displayRectFor;
using GyanisScribe::Core::imageRectToDisplay;

/**
 * @brief 钉住显示区算的是「等比缩放 + 居中」
 * @details 手算三个数：800x200 的图放进 400x400 的控件，比例由更紧的横向定死（0.5），
 *          纵向留白 (400-100)/2 = 150。拉伸填满会破坏坐标换算，所以这里判的是精确矩形。
 */
TEST(ImageFitGeometry, DisplayRectScalesInsideWithoutDistorting)
{
    EXPECT_EQ(displayRectFor({400, 400}, {800, 200}), QRect(0, 150, 400, 100));
    // 反过来：控件很扁时是纵向说了算，横向留白 (200-100)/2 = 50
    EXPECT_EQ(displayRectFor({200, 50}, {200, 100}), QRect(50, 0, 100, 50));
    // 控件比图大时允许放大：小截图也要看得清
    EXPECT_EQ(displayRectFor({100, 100}, {50, 50}), QRect(0, 0, 100, 100));
}

/**
 * @brief 钉住退化尺寸的显示区是空矩形
 * @details 0 或负数尺寸会让后面的除法炸掉，判成空矩形让调用方走「什么都不画」那条路
 */
TEST(ImageFitGeometry, DisplayRectRejectsDegenerateSizes)
{
    EXPECT_TRUE(displayRectFor({0, 100}, {200, 100}).isEmpty());
    EXPECT_TRUE(displayRectFor({100, 100}, {0, 0}).isEmpty());
    EXPECT_TRUE(displayRectFor({-5, 100}, {200, 100}).isEmpty());
}

/**
 * @brief 钉住图上方框换算的精确结果
 * @details 期望值由手算给出（0.5 的比例、150 的顶部留白），不取自被测函数：
 *          用被测函数算期望，等比与偏移同时算错也照样全绿。
 */
TEST(ImageFitGeometry, LineRectMapsToHandComputedDisplayRect)
{
    const QSize image{800, 200};
    const QRect display = displayRectFor({400, 400}, image);

    // 图上 (100,20) 起、300x16 的行框：x 50、y 150+10、宽 150、高 8
    EXPECT_EQ(imageRectToDisplay({100, 20, 300, 16}, image, display), QRect(50, 160, 150, 8));
    // 整张图的框必须正好铺满显示区，不多不少一像素
    EXPECT_EQ(imageRectToDisplay({0, 0, 800, 200}, image, display), display);
}

/**
 * @brief 钉住两个方向的缩放各算一次
 * @details displayRectFor 产出的矩形是等比的，但调用方也可能给一个非等比区域；
 *          只按宽度算会让纵向整体压扁。这里用 scaleX=2、scaleY=1 的区域把两条分开。
 */
TEST(ImageFitGeometry, NonUniformDisplayRectUsesTwoScales)
{
    const QSize image{100, 100};
    const QRect display{0, 0, 200, 100};

    EXPECT_EQ(imageRectToDisplay({10, 20, 30, 40}, image, display), QRect(20, 20, 60, 40));
}

/**
 * @brief 钉住「图的中心」与「显示区的中心」指同一个点
 * @details 摆图与画框是两套换算，任一边独自改口径就会点着第 3 行圈住第 2 行；
 *          这条互指判据让两边必须同解。
 */
TEST(ImageFitGeometry, ImageCenterAndDisplayCenterAgree)
{
    const QSize image{800, 200};
    const QRect display = displayRectFor({400, 400}, image);

    // 期望值手算：比例 0.5、顶部留白 150 → 图中心 (400,100) 落在 (200, 200)。
    // 不写 display.center()：QRect::center() 按右下角取整会给出 (199,199)，那是另一种口径
    const QPoint mappedCenter = imageRectToDisplay({400, 100, 1, 1}, image, display).topLeft();
    EXPECT_EQ(mappedCenter, QPoint(200, 200));
}

/**
 * @brief 钉住退化输入的方框换算返回空
 * @details 空矩形在调用方就是「这一行不画」，比画在 (0,0) 的一个 1x1 点好读
 */
TEST(ImageFitGeometry, DegenerateInputsYieldEmptyRect)
{
    EXPECT_TRUE(imageRectToDisplay({10, 10, 20, 20}, {0, 100}, {0, 0, 100, 100}).isEmpty());
    EXPECT_TRUE(imageRectToDisplay({10, 10, 20, 20}, {100, 100}, QRect{}).isEmpty());
}
