// 长图分带与跨带合并：带表边界、贴边丢行、平移与语言继承。

#include "Core/Ocr/OcrTypes.h"
#include "Core/Privacy/PrivacyScanTiles.h"

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace
{
    using GyanisScribe::Core::OcrLine;
    using GyanisScribe::Core::OcrResult;
    using GyanisScribe::Core::OcrWord;
    using GyanisScribe::Core::Privacy::kPrivacyScanBandHeight;
    using GyanisScribe::Core::Privacy::kPrivacyScanOverlap;
    using GyanisScribe::Core::Privacy::mergeTileResult;
    using GyanisScribe::Core::Privacy::scanTilesForImage;

    OcrLine lineAt(const QString &text, const QRect &box)
    {
        return OcrLine{{OcrWord{text, box}}, box};
    }

    /// 用例里按 std::vector 写行表更顺手；OcrResult 存的是 QVector，逐条搬进去
    OcrResult resultWith(std::vector<OcrLine> lines, const QString &language = QStringLiteral("zh-Hans"))
    {
        OcrResult result;
        result.language = language;
        result.lines.reserve(static_cast<qsizetype>(lines.size()));
        for (OcrLine &line: lines)
        {
            result.lines.push_back(std::move(line));
        }
        return result;
    }
} // namespace

/// 带表：单带、恰好两带、末带夹边
TEST(PrivacyScanTiles, SplitsTheImageIntoOverlappingBands)
{
    const auto single = scanTilesForImage(QSize{800, 1000}, 1200, 120);
    ASSERT_EQ(single.size(), 1U);
    EXPECT_EQ(single.front(), (QRect{0, 0, 800, 1000}));

    // 2400 高、步长 1080：第三带只剩 240 行，必须补上——漏掉它就会少扫底部那一段
    const auto exact = scanTilesForImage(QSize{800, 2400}, 1200, 120);
    ASSERT_EQ(exact.size(), 3U) << "2400 高在 1080 步长下应切三带";
    EXPECT_EQ(exact[0], (QRect{0, 0, 800, 1200}));
    EXPECT_EQ(exact[1], (QRect{0, 1080, 800, 1200}));
    EXPECT_EQ(exact[2], (QRect{0, 2160, 800, 240})) << "末带要夹到图底";

    const auto tail = scanTilesForImage(QSize{800, 2401}, 1200, 120);
    ASSERT_EQ(tail.size(), 3U);
    EXPECT_EQ(tail.back(), (QRect{0, 2160, 800, 241}));

    // 1320 高：两带，末带夹到 240
    const auto clipped = scanTilesForImage(QSize{800, 1320}, 1200, 120);
    ASSERT_EQ(clipped.size(), 2U);
    EXPECT_EQ(clipped.back(), (QRect{0, 1080, 800, 240}));
}

/// 坏参数与退化输入
TEST(PrivacyScanTiles, RejectsImpossibleBands)
{
    EXPECT_TRUE(scanTilesForImage(QSize{0, 100}, 1200, 120).empty());
    EXPECT_TRUE(scanTilesForImage(QSize{100, 0}, 1200, 120).empty());
    EXPECT_TRUE(scanTilesForImage(QSize{100, 100}, 0, 0).empty());
    EXPECT_TRUE(scanTilesForImage(QSize{100, 100}, 1200, 1200).empty()) << "重叠等于带宽时带不动";
    EXPECT_TRUE(scanTilesForImage(QSize{100, 100}, 1200, -1).empty());
}

/// 平移：带内框按带原点挪成全局坐标
TEST(PrivacyScanTiles, ShiftsTileBoxesIntoGlobalCoordinates)
{
    const auto merged = mergeTileResult(OcrResult{}, resultWith({lineAt(QStringLiteral("a"), QRect{10, 20, 30, 10})}), QPoint{0, 1080}, 1200, false, true);
    ASSERT_EQ(merged.lines.size(), 1);
    EXPECT_EQ(merged.lines.front().boundingBox, (QRect{10, 1100, 30, 10}));
    ASSERT_EQ(merged.lines.front().words.size(), 1);
    EXPECT_EQ(merged.lines.front().words.front().boundingBox, (QRect{10, 1100, 30, 10}));
    EXPECT_EQ(merged.language, QStringLiteral("zh-Hans")) << "语言该从第一份非空结果里继承";
}

/// 贴边丢行：非首带丢贴顶、非末带丢贴底，首末带各留另一侧
TEST(PrivacyScanTiles, DropsLinesClippedByBandEdges)
{
    const auto tile = resultWith(
            {lineAt(QStringLiteral("top"), QRect{0, 0, 50, 20}), lineAt(QStringLiteral("mid"), QRect{0, 600, 50, 20}), lineAt(QStringLiteral("bottom"), QRect{0, 1185, 50, 15})});

    const auto middle = mergeTileResult(OcrResult{}, tile, QPoint{0, 1080}, 1200, false, false);
    ASSERT_EQ(middle.lines.size(), 1) << "中间带两侧贴边的行都该丢";
    EXPECT_EQ(middle.lines.front().words.front().text, QStringLiteral("mid"));

    const auto first = mergeTileResult(OcrResult{}, tile, QPoint{0, 0}, 1200, true, false);
    ASSERT_EQ(first.lines.size(), 2) << "首带要留贴顶那一行";
    EXPECT_EQ(first.lines.front().words.front().text, QStringLiteral("top"));

    const auto last = mergeTileResult(OcrResult{}, tile, QPoint{0, 1080}, 1200, false, true);
    ASSERT_EQ(last.lines.size(), 2) << "末带要留贴底那一行";
}

/// 同一手机号在重叠区出现两次时只留一条：它两带都看得完整，贴边规则管不到，靠文本与框判重
TEST(PrivacyScanTiles, KeepsASingleCopyOfAnOverlapLine)
{
    const QRect inUpperBand{10, 1150, 200, 30};
    const QRect inLowerBand{10, 70, 200, 30};

    auto merged = mergeTileResult(OcrResult{}, resultWith({lineAt(QStringLiteral("13800138000"), inUpperBand)}), QPoint{0, 0}, 1200, true, false);
    merged      = mergeTileResult(merged, resultWith({lineAt(QStringLiteral("13800138000"), inLowerBand)}), QPoint{0, 1080}, 1200, false, true);

    ASSERT_EQ(merged.lines.size(), 1) << "重叠区的同一行被记了两遍";
    EXPECT_EQ(merged.lines.front().boundingBox, (QRect{10, 1150, 200, 30})) << "留下的应是完整那一份";
}

/// 常量就是 spec 写死的那两个
TEST(PrivacyScanTiles, ConstantsMatchTheSpec)
{
    EXPECT_EQ(kPrivacyScanBandHeight, 1200);
    EXPECT_EQ(kPrivacyScanOverlap, 120);
}
