#include "Core/Privacy/RedactionGeometry.h"

#include <QRect>
#include <QSize>

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace
{
    using GyanisScribe::Core::Privacy::expandBarcodeScanline;
    using GyanisScribe::Core::Privacy::logicalRedactionRect;

    const QSize kImageSize{400, 200};
    const QSize kLogicalSize{200, 100};
} // namespace

/**
 * @brief 钉住物理框到逻辑框的倍比与四周留边
 * @details 画布是原图的一半大：物理 (100,50,200,100) 先缩到 (50,25,100,50)，
 *          四边各留 2 逻辑像素得到 (48,23,104,54)。
 */
TEST(RedactionGeometry, MapsPhysicalBoxToLogicalWithPadding)
{
    const auto mapped = logicalRedactionRect(QRect{100, 50, 200, 100}, kImageSize, kLogicalSize);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(*mapped, QRect(48, 23, 104, 54));
}

/**
 * @brief 钉住留边在画布边上被夹回：左上、右下两角各判一次
 * @details 留边会越出画布边界，夹取后框的边必须正好落在画布内，不许出现负坐标或越过右下角。
 */
TEST(RedactionGeometry, ClampsPaddingAtTheCanvasEdges)
{
    const auto topLeft = logicalRedactionRect(QRect{0, 0, 20, 10}, kImageSize, kLogicalSize);
    ASSERT_TRUE(topLeft.has_value());
    EXPECT_EQ(*topLeft, QRect(0, 0, 12, 7)) << "左上的留边没夹回画布";

    const auto bottomRight = logicalRedactionRect(QRect{380, 190, 20, 10}, kImageSize, kLogicalSize);
    ASSERT_TRUE(bottomRight.has_value());
    EXPECT_EQ(*bottomRight, QRect(188, 93, 12, 7)) << "右下的留边没夹回画布";
}

/// 空的源框不产出 4x4 的小块：检测器给空框时跳过，而不是在画布上落一个看不见的点
TEST(RedactionGeometry, DropsBoxesWhoseSourceIsEmpty)
{
    EXPECT_FALSE(logicalRedactionRect(QRect{}, kImageSize, kLogicalSize).has_value());
    EXPECT_FALSE(logicalRedactionRect(QRect{50, 50, 0, 30}, kImageSize, kLogicalSize).has_value());
}

/// 完全落在画布外的框没有可打码的区域，返回空让调用方按「没有可用框」提示
TEST(RedactionGeometry, DropsBoxesFullyOutsideTheCanvas)
{
    EXPECT_FALSE(logicalRedactionRect(QRect{500, 300, 20, 10}, kImageSize, kLogicalSize).has_value());
    EXPECT_FALSE(logicalRedactionRect(QRect{-80, 20, 20, 10}, kImageSize, kLogicalSize).has_value());
}

/// 尺寸退化时不换算：除以零或把空尺寸当成画布都会得到无意义的框
TEST(RedactionGeometry, DropsDegenerateSizes)
{
    EXPECT_FALSE(logicalRedactionRect(QRect{10, 10, 20, 10}, QSize{}, kLogicalSize).has_value());
    EXPECT_FALSE(logicalRedactionRect(QRect{10, 10, 20, 10}, kImageSize, QSize{}).has_value());
}

/**
 * @brief 钉住一维码扫描线被撑到至少 32 逻辑像素高，且仍以扫描线为中心
 * @details 原图是画布的两倍：32 逻辑像素折成 64 物理像素。扫描线 y=100、高 1，
 *          目标框从 y=68 到 y=131，扫描线中心落在里面；换算回逻辑坐标再加留边后高 36。
 */
TEST(RedactionGeometry, BarcodeScanlineExpandsToAtLeastThirtyTwoLogicalPixels)
{
    const auto expanded = expandBarcodeScanline(QRect{50, 100, 100, 1}, QSize{800, 600}, QSize{400, 300});
    EXPECT_EQ(expanded, QRect(50, 68, 100, 64)) << "一维码没有撑到 32 逻辑像素高，或没有以扫描线为中心";
    EXPECT_TRUE(expanded.contains(QRect{50, 100, 100, 1}.center())) << "扫描线中心跑出了打码框";

    const auto mapped = logicalRedactionRect(expanded, QSize{800, 600}, QSize{400, 300});
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(*mapped, QRect(23, 32, 54, 36)) << "换算回逻辑坐标并留边之后的框对不上";
    EXPECT_GE(mapped->height(), 32) << "留边后的成品框仍不足 32 逻辑像素高";
}

/// 扩框在图像边上夹住：上沿不许出负坐标，下沿不许越过图像最后一行
TEST(RedactionGeometry, BarcodeScanlineExpansionClampsToTheImage)
{
    const auto top = expandBarcodeScanline(QRect{50, 2, 100, 1}, QSize{800, 600}, QSize{400, 300});
    EXPECT_EQ(top, QRect(50, 0, 100, 34)) << "上沿扩出图像后没夹回";

    const auto bottom = expandBarcodeScanline(QRect{50, 596, 100, 1}, QSize{800, 600}, QSize{400, 300});
    EXPECT_EQ(bottom, QRect(50, 564, 100, 36)) << "下沿扩出图像后没夹回";
}

/**
 * @brief 钉住非等倍比的画布：横纵两向各按自己的倍比换算
 * @details 原图 800×600、画布 400×150：横向 0.5、纵向 0.25。
 *          物理 (100,50,200,100) 换成逻辑 (50,12.5,100,25)，四边留 2 后取整外扩为 (48,10,104,30)。
 */
TEST(RedactionGeometry, UsesIndependentScalesForNonUniformCanvases)
{
    const auto mapped = logicalRedactionRect(QRect{100, 50, 200, 100}, QSize{800, 600}, QSize{400, 150});
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(*mapped, QRect(48, 10, 104, 30)) << "横纵没有各按自己的倍比换算，或留边取整口径变了";
}

/// 一批框里只数换算成功的那些：空框与完全越界的框被剔掉，其余照常产出
TEST(RedactionGeometry, CountsOnlyBoxesThatMapIntoTheCanvas)
{
    const std::vector<QRect> boxes{QRect{100, 50, 200, 100}, QRect{500, 300, 20, 10}, QRect{}, QRect{380, 190, 20, 10}};

    std::size_t mappedCount = 0;
    for (const QRect &box: boxes)
    {
        if (logicalRedactionRect(box, kImageSize, kLogicalSize).has_value())
        {
            ++mappedCount;
        }
    }
    EXPECT_EQ(mappedCount, 2U) << "被剔掉的应当是空框与完全越界的框，实测数量对不上";
}
