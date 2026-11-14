#include "Core/Ocr/PpOcrPostprocess.h"

#include <QRect>

#include <gtest/gtest.h>

#include <vector>

namespace Core = GyanisScribe::Core;

namespace
{
    /// 把概率图上以 x、y 为左上角的一块矩形区域填成固定值
    void fillBlock(std::vector<float> &map, const int width, const int x, const int y, const int w, const int h, const float value)
    {
        for (int row = y; row < y + h; ++row)
        {
            for (int column = x; column < x + w; ++column)
            {
                map[static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(column)] = value;
            }
        }
    }

    /// 把一个时间步上指定类别的分数抬成峰值，同一步其余类别保持为 0
    void setPeak(std::vector<float> &logits, const int timeSteps, const int classes, const int step, const int classIndex, const float value)
    {
        ASSERT_LT(step, timeSteps);
        logits[static_cast<size_t>(step) * static_cast<size_t>(classes) + static_cast<size_t>(classIndex)] = value;
    }
} // namespace

/// 后处理都是无状态函数，空 fixture 只为按组聚合用例
class PpOcrPostprocessFixture : public ::testing::Test
{
};

TEST_F(PpOcrPostprocessFixture, ExtractsTwoBoxesInReadingOrder)
{
    std::vector<float> map(40 * 24, 0.0F);
    fillBlock(map, 40, 4, 4, 6, 4, 0.9F);
    fillBlock(map, 40, 20, 14, 6, 4, 0.9F);
    const auto boxes = Core::extractTextBoxes(map.data(), 40, 24);
    ASSERT_EQ(boxes.size(), 2);
    EXPECT_EQ(boxes[0].rect, QRect(2, 2, 10, 8)); // 外扩 max(2, round(0.2*4))=2
    EXPECT_EQ(boxes[1].rect, QRect(18, 12, 10, 8));
    EXPECT_NEAR(boxes[0].score, 0.9F, 1e-4F);
}

TEST_F(PpOcrPostprocessFixture, DropsTinyAndLowScoreBlocksAndClampsToImage)
{
    std::vector<float> map(20 * 20, 0.0F);
    map[5 * 20 + 5] = 0.9F;                 // 面积 1
    fillBlock(map, 20, 10, 10, 3, 3, 0.4F); // 低分
    fillBlock(map, 20, 0, 0, 4, 4, 0.9F);   // 贴边
    const auto boxes = Core::extractTextBoxes(map.data(), 20, 20);
    ASSERT_EQ(boxes.size(), 1);
    EXPECT_EQ(boxes[0].rect, QRect(0, 0, 6, 6)); // 外扩 2 后夹回 0..19
}

TEST_F(PpOcrPostprocessFixture, DecodesCtcWithRepeatsAndBlank)
{
    const QStringList dict{"a", "b", "c"};
    std::vector<float> logits(6 * 4, 0.0F); // blank 加 3 个字符
    setPeak(logits, 6, 4, 0, 1, 0.9F); setPeak(logits, 6, 4, 1, 1, 0.9F);
    setPeak(logits, 6, 4, 2, 0, 0.9F); setPeak(logits, 6, 4, 3, 2, 0.8F);
    setPeak(logits, 6, 4, 4, 3, 0.7F); setPeak(logits, 6, 4, 5, 0, 0.9F);
    const auto decoded = Core::decodeCtc(logits.data(), 6, 4, dict);
    EXPECT_EQ(decoded.text, QStringLiteral("abc"));
    EXPECT_NEAR(decoded.confidence, 0.8, 1e-6);
}

TEST_F(PpOcrPostprocessFixture, DecodesAllBlankToEmptyText)
{
    const QStringList dict{"a"};
    std::vector<float> logits(4 * 2, 0.0F); // 每步 blank 最高
    for (int t = 0; t < 4; ++t) { logits[t * 2] = 0.9F; }
    const auto decoded = Core::decodeCtc(logits.data(), 4, 2, dict);
    EXPECT_TRUE(decoded.text.isEmpty()); EXPECT_DOUBLE_EQ(decoded.confidence, 0.0);
}

TEST_F(PpOcrPostprocessFixture, DecodesTheTrailingSpaceClassAsSpace)
{
    const QStringList dict{"a"}; // 类 0=blank, 1='a', 2=use_space_char 空格
    std::vector<float> logits(3 * 3, 0.0F);
    setPeak(logits, 3, 3, 0, 1, 0.9F); setPeak(logits, 3, 3, 1, 2, 0.9F);
    setPeak(logits, 3, 3, 2, 1, 0.9F);
    const auto decoded = Core::decodeCtc(logits.data(), 3, 3, dict);
    EXPECT_EQ(decoded.text, QStringLiteral("a a"));
}
