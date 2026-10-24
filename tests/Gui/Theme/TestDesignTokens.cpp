#include "Core/Annotation/AnnotationPalette.h"
#include "Core/Record/CursorHighlight.h"
#include "Gui/Theme/DesignTokens.h"
#include "Gui/Theme/ThemeKind.h"

#include <gtest/gtest.h>

#include <array>
#include <optional>

using GyanisScribe::Gui::kAccentColor;
using GyanisScribe::Gui::kAnnotationWidths;
using GyanisScribe::Gui::kDarkPalette;
using GyanisScribe::Gui::kFontSizes;
using GyanisScribe::Gui::kLightPalette;
using GyanisScribe::Gui::paletteFor;
using GyanisScribe::Gui::stepIndexOf;
using GyanisScribe::Gui::ThemeKind;

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

/**
 * @brief 光标高亮的圈色必须跟着主色走：两处各写一份字面量就会在某天分家
 */
TEST(DesignTokens, CursorHighlightColorFollowsTheAccent)
{
    EXPECT_EQ(GyanisScribe::Core::kCursorHighlightColor, kAccentColor);
}

/**
 * @brief 标注默认色板里「蓝」那一格必须与主色同值：色板搬去 Core 之后两处字面量最容易分家
 */
TEST(DesignTokens, AnnotationPaletteBlueFollowsTheAccent)
{
    EXPECT_EQ(GyanisScribe::Core::kAnnotationPaletteColors[4], kAccentColor);
}

/**
 * @brief 钉住浅深两档的微明度分层与无纯黑约束
 * @details 控件静息底必须同时区别于窗口底与卡片底，否则按钮与输入框会融进面里；
 *          浅色与深色档一律不出现不透明纯黑，纯黑只留给高对比度档。
 */
TEST(DesignTokens, MicroLayeringRolesAreDistinctAndFreeOfOpaqueBlack)
{
    for (const auto kind: {ThemeKind::Light, ThemeKind::Dark})
    {
        const auto &palette = paletteFor(kind);
        EXPECT_NE(palette.controlFill, palette.surface);
        EXPECT_NE(palette.controlFill, palette.windowBackground);

        const std::array colors = {palette.windowBackground, palette.surface, palette.controlFill,  palette.hover,     palette.pressed,  palette.mutedFill, palette.text,
                                   palette.secondaryText,    palette.border,  palette.borderStrong, palette.accent,    palette.onAccent, palette.selection, palette.icon,
                                   palette.success,          palette.danger,  palette.accentHover,  palette.accentSoft};
        for (const auto &color: colors)
        {
            EXPECT_NE(color, QColor(0, 0, 0)) << "Light/Dark 不该有不透明纯黑";
        }
    }
}
