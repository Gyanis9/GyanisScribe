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
using GyanisScribe::Core::StitchOptions;

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

    /// 造一张「每行一个颜色」的聊天内容长页：行号就是颜色，接错一行都能从颜色上认出来
    /// @details 相邻行的通道值至少差 17，超过 pixelTolerance：容差搜索不会把多出来的一行也当成重叠
    [[nodiscard]] QImage makeChatContent(int width, int height)
    {
        QImage page{QSize{width, height}, QImage::Format_ARGB32};
        for (auto row = 0; row < height; ++row)
        {
            std::fill_n(reinterpret_cast<QRgb *>(page.scanLine(row)), width, QColor{row * 17 % 251, row * 29 % 251, row * 43 % 251, 255}.rgba());
        }
        return page;
    }

    /// 造一帧聊天窗口：顶部 20 行标题栏、中间 contentHeight 行内容、底部 footerHeight 行输入框
    [[nodiscard]] QImage makeChatFrame(const QImage &content, int contentTop, int contentHeight = 65, int footerHeight = 15)
    {
        const auto frameHeight = 20 + contentHeight + footerHeight;
        QImage     frame{QSize{content.width(), frameHeight}, QImage::Format_ARGB32};
        for (auto row = 0; row < 20; ++row)
        {
            std::fill_n(reinterpret_cast<QRgb *>(frame.scanLine(row)), content.width(), QColor{10, 20, 30, 255}.rgba());
        }
        for (auto row = 0; row < contentHeight; ++row)
        {
            std::memcpy(frame.scanLine(20 + row), content.constScanLine(contentTop + row), static_cast<std::size_t>(content.bytesPerLine()));
        }
        for (auto row = 20 + contentHeight; row < frameHeight; ++row)
        {
            std::fill_n(reinterpret_cast<QRgb *>(frame.scanLine(row)), content.width(), QColor{200, 210, 220, 255}.rgba());
        }
        return frame;
    }

    /// 造一张聊天窗口长图：顶部 20 行标题栏，下面接内容的前 rows 行，不带输入框
    [[nodiscard]] QImage makeChatPage(const QImage &content, int rows)
    {
        QImage page{QSize{content.width(), 20 + rows}, QImage::Format_ARGB32};
        for (auto row = 0; row < 20; ++row)
        {
            std::fill_n(reinterpret_cast<QRgb *>(page.scanLine(row)), content.width(), QColor{10, 20, 30, 255}.rgba());
        }
        for (auto row = 0; row < rows; ++row)
        {
            std::memcpy(page.scanLine(20 + row), content.constScanLine(row), static_cast<std::size_t>(content.bytesPerLine()));
        }
        return page;
    }

    /// 长截图接不上时的统一说法：用例两端都要逐字对上，文案一改这里就得跟着改
    constexpr const char *kNoSeamMessage = "长截图接不上：这一段和上一段没有可信的重叠。请回到上一段末尾附近，等画面停稳后再滚一小段；如果已经滚过一屏，请缩小滚动幅度重来";

    /// 造一张每行都不同的长页：makeTallPage 的颜色每 251 行重复一次，认边界时会撞出假重叠
    [[nodiscard]] QImage makeUniqueTallPage(int width, int height)
    {
        QImage page{QSize{width, height}, QImage::Format_ARGB32};
        for (auto row = 0; row < height; ++row)
        {
            const auto color = QColor{row % 251, (row / 251) * 100, (row * 13) % 251, 255};
            std::fill_n(reinterpret_cast<QRgb *>(page.scanLine(row)), width, color.rgba());
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
        if (row == 100)
        {
            // 第一次追加就装不下：容量要从 100 翻到 200。按精确行数重分配时分配器可能复用地址，
            // 只看地址与最终内容都发现不了，只能盯容量
            EXPECT_EQ(stitcher.allocatedPixelHeightForTesting(), 200) << "第一次追加后的容量不是 200：扩容按精确行数在走，不是在翻倍";
        }
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

/**
 * @brief 钉住重叠区里小面积动图不会把整帧误判成接不上
 * @details 动图或光标闪烁只改动重叠区里的一行、两个像素；逐像素完全相等会返回 0，
 *          调用方于是把整帧重复接进长图。行内允许少量像素不同、整段允许少量行不同之后，
 *          真接缝仍要认出 40 行。
 */
TEST(FrameStitch, ToleratesOneAnimatedRow)
{
    const auto page     = makeTallPage(200, 200);
    const auto first    = page.copy(0, 0, 200, 100);
    const auto second   = page.copy(0, 60, 200, 100);
    QImage     animated = second;
    // 重叠区里只有一行、两个像素不同：整段至少留一行余量，应仍算接上
    animated.setPixelColor(0, 0, QColor{255, 0, 255});
    animated.setPixelColor(1, 0, QColor{255, 0, 255});

    EXPECT_EQ(overlapRowCount(first, second), 40);
    EXPECT_EQ(overlapRowCount(first, animated), 40) << "小面积动图把整帧误判成接不上";
}

/**
 * @brief 钉住超过行或者行内宽容差的动画仍判失败
 * @details 三行各改五个像素：每行的不同像素数超过宽度的百分之一，不同行数也超过整段
 *          允许的行数。这种幅度的变化可能是页面真的换了内容，宁可判没接上。
 */
TEST(FrameStitch, RejectsAnimationLargerThanTolerance)
{
    const auto page     = makeTallPage(200, 200);
    const auto first    = page.copy(0, 0, 200, 100);
    const auto second   = page.copy(0, 60, 200, 100);
    QImage     animated = second;
    for (auto row = 0; row < 3; ++row)
    {
        for (auto column = 0; column < 5; ++column)
        {
            animated.setPixelColor(column, row, QColor{255, 0, 255});
        }
    }
    EXPECT_EQ(overlapRowCount(first, animated), 0) << "超过行/行内宽容差仍应判失败";
}

/**
 * @brief 钉住短于最小接缝的帧仍按整帧追加
 * @details 逐行滚动的用例用的是不到 24 行的帧；这类帧没有可判的重叠，
 *          必须原样接在末尾，不能被接缝搜索折成零行。
 */
TEST(FrameStitch, ShortFramesKeepWholeFrameSemantics)
{
    const auto    page  = makeTallPage(20, 120);
    const auto    first = page.copy(0, 0, 20, 100);
    FrameStitcher stitcher{first};
    const auto    appended = stitcher.append(page.copy(0, 100, 20, 1));
    ASSERT_TRUE(appended.has_value());
    EXPECT_EQ(stitcher.pixelHeight(), 101);
}

/**
 * @brief 钉住聊天窗口的吸顶标题栏与吸底输入框只保留一次
 * @details 每帧都是「20 行标题栏 + 65 行内容 + 15 行输入框」，整帧没有重叠；
 *          去掉头尾后内容才是接得上的那一段。成品该是标题栏 20 行加两帧内容共 95 行，
 *          输入框一次都不该进入长图。
 */
TEST(FrameStitch, RemovesStickyHeaderAndFooterFromChatFrames)
{
    const auto content = makeChatContent(40, 200);
    const auto first   = makeChatFrame(content, 0);
    const auto second  = makeChatFrame(content, 30);

    FrameStitcher stitcher{first};
    ASSERT_TRUE(stitcher.append(second).has_value());
    EXPECT_EQ(stitcher.stickyTopRowsForTesting(), 20);
    EXPECT_EQ(stitcher.stickyBottomRowsForTesting(), 15);
    EXPECT_EQ(stitcher.pixelHeight(), 115) << "应为标题 20 + 内容 95 行，输入框不应进入长图";
    EXPECT_TRUE(stitcher.snapshot() == makeChatPage(content, 95));
}

/**
 * @brief 钉住粘性头尾连续几帧都只跳过一次
 * @details 第二帧认出粘性头尾之后，第三帧仍是同一聊天窗口；继续按同一组头尾跳过，
 *          标题栏只在长图顶部保留一次，输入框一次都不该进入长图。
 */
TEST(FrameStitch, ReusesStickyBandsAcrossFrames)
{
    const auto content = makeChatContent(40, 200);

    FrameStitcher stitcher{makeChatFrame(content, 0)};
    for (const auto contentTop: {30, 60})
    {
        const auto appended = stitcher.append(makeChatFrame(content, contentTop));
        ASSERT_TRUE(appended.has_value()) << appended.error().message;
    }

    EXPECT_EQ(stitcher.stickyTopRowsForTesting(), 20);
    EXPECT_EQ(stitcher.stickyBottomRowsForTesting(), 15);
    EXPECT_EQ(stitcher.pixelHeight(), 145) << "应为标题 20 + 三帧内容共 125 行";
    EXPECT_TRUE(stitcher.snapshot() == makeChatPage(content, 125)) << "三帧接完的内容与原始聊天长页逐位不同";
}

/**
 * @brief 钉住头尾消失的那一帧把粘性状态清零、按整帧接上
 * @details 两帧聊天窗口之后来一张只有内容的帧，粘性头尾不再适用；这一帧必须整帧找重叠接上，
 *          状态清零，也不能把缓冲末尾当成输入框裁掉。
 */
TEST(FrameStitch, ResetsStickyBandsWhenTheyDisappear)
{
    const auto content = makeChatContent(40, 200);

    FrameStitcher stitcher{makeChatFrame(content, 0)};
    ASSERT_TRUE(stitcher.append(makeChatFrame(content, 30)).has_value());
    ASSERT_EQ(stitcher.pixelHeight(), 115);

    const auto appended = stitcher.append(content.copy(0, 60, 40, 65));
    ASSERT_TRUE(appended.has_value()) << appended.error().message;
    EXPECT_EQ(stitcher.stickyTopRowsForTesting(), 0);
    EXPECT_EQ(stitcher.stickyBottomRowsForTesting(), 0);
    EXPECT_EQ(stitcher.pixelHeight(), 145) << "整帧接上 30 行，不该把缓冲末尾当输入框裁掉";
    EXPECT_TRUE(stitcher.snapshot() == makeChatPage(content, 125)) << "头尾消失后接上的内容与原始长页逐位不同";
}

/**
 * @brief 钉住粘性头尾回来后不把缓冲末尾的内容当输入框裁掉
 * @details 头尾消失的那一帧整帧接上后，缓冲末尾是真实内容；下一帧聊天窗口重新带回标题栏与输入框，
 *          去掉头尾后的内容正好与缓冲末尾接上。裁剪必须以「缓冲末尾仍是那条参考尾带」为前提，
 *          末尾是内容时一行都不能裁。
 */
TEST(FrameStitch, DoesNotTrimContentTailWhenStickyBandsReturn)
{
    const auto content = makeChatContent(40, 200);

    FrameStitcher stitcher{makeChatFrame(content, 0)};
    ASSERT_TRUE(stitcher.append(makeChatFrame(content, 30)).has_value());
    ASSERT_TRUE(stitcher.append(content.copy(0, 60, 40, 65)).has_value());
    ASSERT_EQ(stitcher.pixelHeight(), 145);
    ASSERT_EQ(stitcher.stickyTopRowsForTesting(), 0);

    const auto appended = stitcher.append(makeChatFrame(content, 95));
    ASSERT_TRUE(appended.has_value()) << appended.error().message;
    EXPECT_EQ(stitcher.stickyTopRowsForTesting(), 20);
    EXPECT_EQ(stitcher.stickyBottomRowsForTesting(), 15);
    EXPECT_EQ(stitcher.pixelHeight(), 180) << "应为标题 20 + 内容 160 行；少了 15 行说明把内容当成输入框裁了";
    EXPECT_TRUE(stitcher.snapshot() == makeChatPage(content, 160)) << "接出来的内容与原始长页逐位不同";
}

/**
 * @brief 钉住粘性状态清零之后仍按无可信接缝失败
 * @details 头尾消失让状态归零；接下来这张完全不同的帧既没有整帧重叠、也没有稳定头尾，
 *          必须报可操作的错误且不改已接好的高度，不能因为前一帧曾是聊天窗口就硬接。
 */
TEST(FrameStitch, FailsWithoutSeamAfterStickyReset)
{
    const auto content = makeChatContent(40, 200);

    FrameStitcher stitcher{makeChatFrame(content, 0)};
    ASSERT_TRUE(stitcher.append(makeChatFrame(content, 30)).has_value());
    ASSERT_TRUE(stitcher.append(content.copy(0, 60, 40, 65)).has_value());
    ASSERT_EQ(stitcher.stickyTopRowsForTesting(), 0);
    ASSERT_EQ(stitcher.pixelHeight(), 145);

    QImage different{QSize{40, 100}, QImage::Format_ARGB32};
    for (auto row = 0; row < 100; ++row)
    {
        std::fill_n(reinterpret_cast<QRgb *>(different.scanLine(row)), 40, QColor{255 - row, row, 40, 255}.rgba());
    }

    const auto appended = stitcher.append(different);
    ASSERT_FALSE(appended.has_value());
    EXPECT_EQ(appended.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(appended.error().message, kNoSeamMessage);
    EXPECT_EQ(stitcher.pixelHeight(), 145) << "接不上时成品不该被硬接长";
}

/**
 * @brief 钉住紧贴吸底输入框的内容行动图仍算接上
 * @details 首帧多露出一些内容，裁剪后的接缝末行正落在第二帧输入框上方那一行；
 *          这一行改了两个像素，整段仍允许一行不同。去掉头尾后的重叠应认出 65 行；
 *          要求重叠末行逐字节相同会把它判成接不上。
 */
TEST(FrameStitch, ToleratesAnimatedContentRowBesideStickyFooter)
{
    const auto content = makeChatContent(40, 200);
    const auto first   = makeChatFrame(content, 0, 95);
    QImage     second  = makeChatFrame(content, 30);
    // 第二帧紧贴输入框的那行内容改两个像素：一行里的不同像素数仍在上限之内
    second.setPixelColor(0, 84, QColor{255, 0, 255});
    second.setPixelColor(1, 84, QColor{255, 0, 255});

    FrameStitcher stitcher{first};
    const auto    appended = stitcher.append(second);
    ASSERT_TRUE(appended.has_value()) << appended.error().message;
    EXPECT_EQ(stitcher.stickyTopRowsForTesting(), 20);
    EXPECT_EQ(stitcher.stickyBottomRowsForTesting(), 15);
    EXPECT_EQ(stitcher.pixelHeight(), 115) << "标题 20 + 内容 95 行，输入框不应进入长图";
    EXPECT_TRUE(stitcher.snapshot() == makeChatPage(content, 95)) << "重叠区里的动图不该改掉已接好的像素";
}

/**
 * @brief 钉住底边整条参考带都对上时不裁尾
 * @details 首帧比后续帧高，第二帧底部的 17 行内容连同 15 行输入框正好填满整条参考带，
 *          其中一行只动了一个像素、仍在容差之内。这时分不清哪几行是输入框，裁掉会吃掉内容：
 *          这一帧必须原样通过、不裁尾，下一帧带来新内容时再重新认出输入框并裁掉。
 */
TEST(FrameStitch, KeepsContentWhenWholeBottomBandMatches)
{
    const auto content = makeChatContent(40, 200);
    const auto first   = makeChatFrame(content, 0, 95);
    QImage     second  = makeChatFrame(content, 30);
    // 只改一个像素：仍在「一行至多 1% 像素不同」的容差内，底边整条参考带都对得上
    second.setPixelColor(0, 84, QColor{255, 0, 255});

    FrameStitcher stitcher{first};
    const auto    appended = stitcher.append(second);
    ASSERT_TRUE(appended.has_value()) << appended.error().message;
    EXPECT_EQ(stitcher.stickyTopRowsForTesting(), 0) << "整条参考带都相同，粘性判定不可信；这一帧没带来新内容，状态应清零";
    EXPECT_EQ(stitcher.stickyBottomRowsForTesting(), 0) << "分不清输入框与内容的整条带不能当成粘性尾裁掉";
    EXPECT_EQ(stitcher.pixelHeight(), 130) << "底边整条参考带相同不能裁；内容一行都不能少";
    EXPECT_TRUE(stitcher.snapshot() == makeChatFrame(content, 0, 95)) << "这一帧没有新内容，成品应与首帧逐位相同";

    const auto third     = makeChatFrame(content, 60);
    const auto appended3 = stitcher.append(third);
    ASSERT_TRUE(appended3.has_value()) << appended3.error().message;
    EXPECT_EQ(stitcher.stickyTopRowsForTesting(), 20);
    EXPECT_EQ(stitcher.stickyBottomRowsForTesting(), 15);
    EXPECT_EQ(stitcher.pixelHeight(), 145) << "标题 20 + 内容 125 行；少一行都说明底边被当成输入框裁了";
    EXPECT_TRUE(stitcher.snapshot() == makeChatPage(content, 125)) << "第三帧接上后应铺满内容 0..124";
}

/**
 * @brief 钉住整帧能接上时不许裁头尾
 * @details 首尾两帧同属一屏的重复块，整帧重叠 60 行；这时按粘性头尾去裁会把真实内容当成
 *          标题栏与输入框吃掉。整帧重叠优先，只有整帧接不上时才试粘性裁剪。
 */
TEST(FrameStitch, DoesNotTrimStickyWhenWholeFrameOverlaps)
{
    const auto page   = makeBlockPage(20, {1, 2, 3, 3});
    const auto first  = page.copy(0, 0, 20, 120);
    const auto second = page.copy(0, 60, 20, 60);

    FrameStitcher stitcher{first};
    ASSERT_TRUE(stitcher.append(second).has_value());
    EXPECT_EQ(stitcher.stickyTopRowsForTesting(), 0);
    EXPECT_EQ(stitcher.stickyBottomRowsForTesting(), 0);
    EXPECT_EQ(stitcher.pixelHeight(), 120);
}

/**
 * @brief 钉住高度够却没有可信重叠的帧明确失败、成品不硬接长
 * @details 两帧内容完全不同时既没有整帧重叠、也没有稳定的头尾横带；
 *          这时整帧追加只会得到一张重复的长图，必须返回可操作的错误且不动已接好的高度。
 */
TEST(FrameStitch, FailsWhenNoTrustworthySeam)
{
    const auto first = makeTallPage(20, 100);
    QImage     different{QSize{20, 100}, QImage::Format_ARGB32};
    for (auto row = 0; row < 100; ++row)
    {
        std::fill_n(reinterpret_cast<QRgb *>(different.scanLine(row)), 20, QColor{255 - row, row, 40, 255}.rgba());
    }

    FrameStitcher stitcher{first};
    const auto    appended = stitcher.append(different);
    ASSERT_FALSE(appended.has_value());
    EXPECT_EQ(appended.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(appended.error().message, kNoSeamMessage);
    EXPECT_EQ(stitcher.pixelHeight(), 100) << "接不上时成品不该被硬接长";
}

/**
 * @brief 钉住粘性尾至少 8 行才认
 * @details 7 行输入框与 8 行输入框在同样内容下只差一行；7 行时认不出粘性尾、按无接缝失败，
 *          8 行时才跳过输入框并把标题栏只留一次。判据同时钉住失败时高度一行未动。
 */
TEST(FrameStitch, StickyFooterNeedsAtLeastEightRows)
{
    const auto content = makeChatContent(40, 200);

    FrameStitcher sevenRows{makeChatFrame(content, 0, 65, 7)};
    const auto    seven = sevenRows.append(makeChatFrame(content, 30, 65, 7));
    ASSERT_FALSE(seven.has_value()) << "7 行输入框不该被认成粘性尾";
    EXPECT_EQ(seven.error().message, kNoSeamMessage);
    EXPECT_EQ(sevenRows.pixelHeight(), 92) << "认不出粘性尾时不能裁掉任何行";

    FrameStitcher eightRows{makeChatFrame(content, 0, 65, 8)};
    const auto    eight = eightRows.append(makeChatFrame(content, 30, 65, 8));
    ASSERT_TRUE(eight.has_value()) << eight.error().message;
    EXPECT_EQ(eightRows.stickyTopRowsForTesting(), 20);
    EXPECT_EQ(eightRows.stickyBottomRowsForTesting(), 8);
    EXPECT_EQ(eightRows.pixelHeight(), 115) << "标题 20 + 两帧内容 95 行，输入框不进成品";
    EXPECT_TRUE(eightRows.snapshot() == makeChatPage(content, 95));
}

/**
 * @brief 钉住自定义重叠上限会截断搜索
 * @details 两帧真实重叠 280 行；默认上限 1200 行时照常认出，给定 100 行上限时按没有重叠处理。
 */
TEST(FrameStitch, CustomMaximumOverlapRowsStopsTheSearch)
{
    const auto page   = makeUniqueTallPage(20, 400);
    const auto first  = page.copy(0, 0, 20, 300);
    const auto second = page.copy(0, 20, 20, 300);
    EXPECT_EQ(overlapRowCount(first, second), 280);
    EXPECT_EQ(overlapRowCount(first, second, StitchOptions{.maximumOverlapRows = 100}), 0) << "重叠超过给定上限时按没有重叠处理";
}
