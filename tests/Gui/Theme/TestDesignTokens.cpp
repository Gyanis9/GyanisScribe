#include "Gui/Theme/DesignTokens.h"

#include <gtest/gtest.h>

#include <array>
#include <optional>

using GyanisScribe::Gui::kAnnotationWidths;
using GyanisScribe::Gui::kFontSizes;
using GyanisScribe::Gui::stepIndexOf;

/**
 * @brief 钉住「阶梯取值 ↔ 控件下标」的互指
 * @details 期望下标逐个手写，不拿表自己反推：把表改成 {2,4,6,8} 之外的任何东西都要能红，
 *          而只断「stepIndexOf(table, table[i]) == i」的写法换什么表都绿。
 */
TEST(DesignTokenSteps, MapsEachRungToItsIndex)
{
    EXPECT_EQ(stepIndexOf(kAnnotationWidths, 2).value(), 0);
    EXPECT_EQ(stepIndexOf(kAnnotationWidths, 4).value(), 1);
    EXPECT_EQ(stepIndexOf(kAnnotationWidths, 6).value(), 2);
    EXPECT_EQ(stepIndexOf(kAnnotationWidths, 8).value(), 3);

    EXPECT_EQ(stepIndexOf(kFontSizes, 12).value(), 0);
    EXPECT_EQ(stepIndexOf(kFontSizes, 13).value(), 1);
    EXPECT_EQ(stepIndexOf(kFontSizes, 14).value(), 2);
    EXPECT_EQ(stepIndexOf(kFontSizes, 16).value(), 3);
    EXPECT_EQ(stepIndexOf(kFontSizes, 20).value(), 4);
    EXPECT_EQ(stepIndexOf(kFontSizes, 24).value(), 5);
}

/**
 * @brief 钉住阶梯外的值返回空而不是就近取一档
 * @details 就近取档会让「配置里存的 5」看起来像生效了，界面上摆的却是 4 或 6。
 */
TEST(DesignTokenSteps, RejectsValuesOffTheLadderWithoutSnapping)
{
    for (const int offLadder: {0, 1, 3, 5, 7, 9, -4})
    {
        EXPECT_FALSE(stepIndexOf(kAnnotationWidths, offLadder).has_value()) << offLadder;
    }
    for (const int offLadder: {11, 15, 18, 25})
    {
        EXPECT_FALSE(stepIndexOf(kFontSizes, offLadder).has_value()) << offLadder;
    }
}
