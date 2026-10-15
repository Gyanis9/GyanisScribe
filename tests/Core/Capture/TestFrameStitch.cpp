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
using GyanisScribe::Core::appendFrame;
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

    const auto appended = appendFrame(first, second);
    ASSERT_TRUE(appended.has_value()) << appended.error().message;
    EXPECT_EQ(appended->height(), 160) << "接完的高度该是「前一张 + 新的一段」";
    EXPECT_TRUE(*appended == page.copy(0, 0, 20, 160)) << "接出来的长页与原始内容逐位不同";
}

/**
 * @brief 钉住两帧全等时长图不许长出一段重复
 */
TEST(FrameStitch, IdenticalFramesAddNothing)
{
    const auto page  = makeTallPage(20, 120);
    const auto first = page.copy(0, 0, 20, 100);

    EXPECT_EQ(overlapRowCount(first, first), 100);

    const auto appended = appendFrame(first, first);
    ASSERT_TRUE(appended.has_value()) << appended.error().message;
    EXPECT_EQ(appended->height(), 100) << "一帧都没滚，成品却长高了";
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

    const auto otherPage  = makeTallPage(30, 120);
    const auto otherWidth = otherPage.copy(0, 0, 30, 100);
    const auto rejected   = appendFrame(first, otherWidth);
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

    const auto appended = appendFrame(previous, next);
    ASSERT_TRUE(appended.has_value()) << appended.error().message;
    EXPECT_EQ(appended->height(), 120) << "整帧都重合还长出了一段";
}
