#include "Platform/UiElementLabels.h"

#include <gtest/gtest.h>

using GyanisScribe::Platform::uiControlTypeLabel;

/// UIA 控件类型 id 映射成界面用中文；不认识的 id 回落“控件”，不空串
TEST(UiElementLabels, MapsKnownControlTypesAndFallsBack)
{
    EXPECT_EQ(uiControlTypeLabel(50000), QStringLiteral("按钮"));   // UIA_ButtonControlTypeId
    EXPECT_EQ(uiControlTypeLabel(50004), QStringLiteral("编辑框")); // UIA_EditControlTypeId
    EXPECT_EQ(uiControlTypeLabel(50011), QStringLiteral("菜单项")); // UIA_MenuItemControlTypeId
    EXPECT_EQ(uiControlTypeLabel(50020), QStringLiteral("文本"));   // UIA_TextControlTypeId
    EXPECT_EQ(uiControlTypeLabel(123456), QStringLiteral("控件"));
}
