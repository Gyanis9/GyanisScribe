// 滚动长截图的接缝：认出两张相邻帧重叠了多少行、只把新的一段接上去。
// 判据用合成长页（每行/每块一个颜色），不拿真截图当预期。
#include "Core/Capture/FrameStitch.h"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

using GyanisScribe::Base::ErrorCode;
using GyanisScribe::Core::FrameStitcher;
using GyanisScribe::Core::overlapRowCount;

namespace
{
    /// 造一张「每行一个颜色」的长页：行号就是颜色，接错任何一段都能从颜色上认出来
    [[nodiscard]] QImage makeTallPage(int width, int height)
    {
        QImage page{QSize{width, height}, QImage::Format_ARGB32};
        for (auto row = 0; row < height; ++row)
        {
            // 整行直接写进扫描线：ARGB32 就是 QRgb，一行一次填，比逐像素 setPixelColor 快也好读
            const auto color = QColor{static_cast<int>(row % 251), static_cast<int>(row * 7 % 251), static_cast<int>(row * 13 % 251), 255};
            std::fill_n(reinterpret_cast<QRgb *>(page.scanLine(row)), width, color.rgba());
        }
        return page;
    }

    /// 造一帧「只与上一张对上 rows 行」的新帧：其余行是别的内容
    [[nodiscard]] QImage makeFrameOverlappingBy(const QImage &previous, int rows)
    {
        auto frame = makeTallPage(previous.width(), previous.height());
        for (auto row = 0; row < rows; ++row)
        {
            std::memcpy(frame.scanLine(row), previous.constScanLine(previous.height() - rows + row), static_cast<std::size_t>(previous.bytesPerLine()));
        }
        return frame;
    }

    /// 造一张「按块重复」的长页：每块 30 行，块内容由序列给出，序列里可以出现两次同一个块
    [[nodiscard]] QImage makeBlockPage(int width, const std::vector<int> &blocks, int blockHeight = 30)
    {
        QImage page{QSize{width, static_cast<int>(blocks.size()) * blockHeight}, QImage::Format_ARGB32};
        for (auto row = 0; row < page.height(); ++row)
        {
            const auto block = blocks[static_cast<std::size_t>(row) / static_cast<std::size_t>(blockHeight)];
            std::fill_n(reinterpret_cast<QRgb *>(page.scanLine(row)), width, QColor{block * 20, block * 40, block * 60, 255}.rgba());
        }
        return page;
    }

} // namespace

/**
 * @brief 钉住滚动长截图的接缝：认出滚掉了多少行、只把新的一段接上去
 * @details 判据用「每行一种颜色」的合成长页：行号就是颜色，任何一段接错都能从颜色上看出来，
 *          不需要拿真截图当预期。重叠行数、成品高度与成品内容三样都钉，缺一半都会在
 *          「看着接上了、其实重了一段」上假绿。
 */
TEST(FrameStitch, OverlapIsHowMuchWasScrolled)
{
    const auto page   = makeTallPage(20, 200);
    const auto first  = page.copy(0, 0, 20, 100);
    const auto second = page.copy(0, 60, 20, 100);

    EXPECT_EQ(overlapRowCount(first, second), 40) << "重叠行数没认出滚掉的这一截";

    FrameStitcher stitcher{first};
    const auto    appended = stitcher.append(second);
    ASSERT_TRUE(appended.has_value()) << appended.error().message;
    const auto result = stitcher.snapshot();
    EXPECT_EQ(result.height(), 160) << "接完的高度该是「前一张 + 新的一段」";
    EXPECT_TRUE(result == page.copy(0, 0, 20, 160)) << "接出来的长页与原始内容逐位不同";
}

/**
 * @brief 钉住两帧全等时长图不许长出一段重复
 */
TEST(FrameStitch, IdenticalFramesAddNothing)
{
    const auto page  = makeTallPage(20, 120);
    const auto first = page.copy(0, 0, 20, 100);

    EXPECT_EQ(overlapRowCount(first, first), 100);

    FrameStitcher stitcher{first};
    const auto    appended = stitcher.append(first);
    ASSERT_TRUE(appended.has_value()) << appended.error().message;
    const auto result = stitcher.snapshot();
    EXPECT_EQ(result.height(), 100) << "一帧都没滚，成品却长高了";
}

/**
 * @brief 钉住接不上时的两种收法：只对上几行当没接上，宽度不同直接报错
 * @details 两页开头一样（大片空白恰好相同）时把空白当成接缝，成品里就会出现同一页两遍；
 *          中途换屏或改缩放会让宽度对不上，硬接就是一张歪图，只能拒绝并把原因说清。
 */
TEST(FrameStitch, RefusesASeamItCannotTrust)
{
    const auto page  = makeTallPage(20, 120);
    const auto first = page.copy(0, 0, 20, 100);
    // 只对上八行：少于最小接缝的门槛，判成「没重叠」而不是接上去
    const auto barelyOverlapping = makeFrameOverlappingBy(first, 8);
    EXPECT_EQ(overlapRowCount(first, barelyOverlapping), 0);

    const auto    otherPage  = makeTallPage(30, 120);
    const auto    otherWidth = otherPage.copy(0, 0, 30, 100);
    FrameStitcher stitcher{first};
    const auto    rejected = stitcher.append(otherWidth);
    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error().code, ErrorCode::InvalidArgument) << "宽度对不上要当场拒绝，不许静默接歪";
    EXPECT_FALSE(rejected.error().message.empty()) << "拒绝得报出原因：用户看不出是哪一帧换的屏";
}
/**
 * @brief 钉住接缝取的是「最长的那一段」，而不是第一个对得上的长度
 * @details 连续两块内容相同的列表（每屏都是同样的两行）会让 24 行与 60 行两种重叠都成立。
 *          收短的那条就把已经接过的两块又接一遍——两帧各自看着都正常，成品却多出重复的一段。
 */
TEST(FrameStitch, LongestSeamWinsWhenThePageRepeats)
{
    const auto page     = makeBlockPage(20, {1, 2, 3, 3});
    const auto previous = page.copy(0, 0, 20, 120);
    const auto next     = page.copy(0, 60, 20, 60); // 这一帧整段都已在成品里

    EXPECT_EQ(overlapRowCount(previous, next), 60) << "接缝该取最长的那一段，不是第一个对得上的";

    FrameStitcher stitcher{previous};
    const auto    appended = stitcher.append(next);
    ASSERT_TRUE(appended.has_value()) << appended.error().message;
    const auto result = stitcher.snapshot();
    EXPECT_EQ(result.height(), 120) << "整帧都重合还长出了一段";
}

/**
 * @brief 钉住缓冲装得下时追加是原地写的
 * @details 首帧 100 行先开 100 行缓冲，第一次追加就翻倍到 200 行；此后九帧都装得下，
 *          地址必须一动不动——每帧都重分配并把已接的行搬一遍，等于把「只写新行」的成本还回去了。
 */
TEST(FrameStitch, InPlaceAppendKeepsTheBufferWhileItFits)
{
    const auto page  = makeTallPage(20, 300);
    const auto first = page.copy(0, 0, 20, 100);

    FrameStitcher stitcher{first};
    ASSERT_EQ(stitcher.pixelHeight(), 100);

    const uchar *address = nullptr;
    for (auto index = 0; index < 10; ++index)
    {
        const auto appended = stitcher.append(page.copy(0, 100 + index, 20, 1));
        ASSERT_TRUE(appended.has_value()) << appended.error().message;
        // 第一次追加正好装不下，扩容就发生在这里；记的是扩容之后的地址
        if (index == 0)
        {
            address = stitcher.bufferAddressForTesting();
            ASSERT_NE(address, nullptr) << "扩容之后缓冲该落在一片实存的内存上";
            continue;
        }
        EXPECT_EQ(stitcher.bufferAddressForTesting(), address) << "第 " << (index + 1) << " 条追加换了缓冲地址：装得下也在重分配";
    }

    EXPECT_EQ(stitcher.pixelHeight(), 110) << "十条一行高的帧没接成十行";
    EXPECT_TRUE(stitcher.snapshot() == page.copy(0, 0, 20, 110)) << "原地追加十行之后内容与原始长页逐位不同";
}

/**
 * @brief 钉住容量按倍数长，而且扩容搬行之后内容不错位
 * @details 首帧 100 行、再逐行追加 100 条，容量只该在装不下时翻倍；每帧都按精确行数重分配
 *          虽然结果也对，但接整屏滚动就是每帧搬一遍整张长图。
 */
TEST(FrameStitch, CapacityGrowsByDoublingNotByEveryAppend)
{
    const auto page  = makeTallPage(20, 300);
    const auto first = page.copy(0, 0, 20, 100);

    FrameStitcher stitcher{first};
    for (auto row = 100; row < 200; ++row)
    {
        const auto appended = stitcher.append(page.copy(0, row, 20, 1));
        ASSERT_TRUE(appended.has_value()) << appended.error().message;
    }

    EXPECT_EQ(stitcher.pixelHeight(), 200);
    EXPECT_LE(stitcher.allocatedPixelHeightForTesting(), 2 * stitcher.pixelHeight()) << "容量没有按倍数长，每帧都在重分配";
    EXPECT_TRUE(stitcher.snapshot() == page.copy(0, 0, 20, 200)) << "扩容搬行之后内容错位了";
}

/**
 * @brief 钉住接缝只在缓冲里「已经写好」的那些行中找，不拿扩容留的空行凑数
 * @details 首帧 100 行、缓冲 100 行；接一帧 100 行（真实重叠 40）后已用 160 行、容量长到 200 行，
 *          后 40 行是扩容留下的、从没写过的空行。再接 page 的 100..199 行时，真实重叠是 60 行、
 *          只该新写 40 行、成品停在 200 行；把整块 200 行缓冲交给接缝判定，那 40 个空行会让重叠
 *          落空，成品接成 260 行并多出一段重复的内容。
 */
TEST(FrameStitch, SeamIsSearchedOnlyInTheUsedRows)
{
    const auto page  = makeTallPage(20, 300);
    const auto first = page.copy(0, 0, 20, 100);

    FrameStitcher stitcher{first};
    const auto    firstAppended = stitcher.append(page.copy(0, 60, 20, 100));
    ASSERT_TRUE(firstAppended.has_value()) << firstAppended.error().message;
    ASSERT_EQ(stitcher.pixelHeight(), 160) << "第一段接完该是 100 + 100 - 40";
    // 没有这段空行，下面的断言在两个实现下都会绿，等于没钉住
    ASSERT_EQ(stitcher.allocatedPixelHeightForTesting(), 200) << "这一条要靠扩容留下的空行才能证伪";

    const auto appended = stitcher.append(page.copy(0, 100, 20, 100));
    ASSERT_TRUE(appended.has_value()) << appended.error().message;
    EXPECT_EQ(stitcher.pixelHeight(), 200) << "接缝该只在已写好的行里找：重叠是 60、新写 40 行；多出来的行说明把扩容的空行当成了内容";
    EXPECT_TRUE(stitcher.snapshot() == page.copy(0, 0, 20, 200)) << "接缝没落在已用行上，成品里多接了一段";
}
