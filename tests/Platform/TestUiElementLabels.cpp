#include "Platform/UiElementLabels.h"

#include <gtest/gtest.h>

using GyanisScribe::Platform::headElementOf;
using GyanisScribe::Platform::UiElementInfo;
using GyanisScribe::Platform::uiControlTypeLabel;

namespace
{
    /// 造一级元素描述：只填名字这一格，其余字段按一份最小可用的样子补齐
    UiElementInfo levelNamed(const QString &name)
    {
        UiElementInfo info;
        info.name          = name;
        info.controlType   = QStringLiteral("按钮");
        info.logicalGlobal = QRect{10, 20, 30, 40};
        return info;
    }
} // namespace

/// UIA 控件类型 id 映射成界面用中文；不认识的 id 回落“控件”，不空串
TEST(UiElementLabels, MapsKnownControlTypesAndFallsBack)
{
    EXPECT_EQ(uiControlTypeLabel(50000), QStringLiteral("按钮"));   // UIA_ButtonControlTypeId
    EXPECT_EQ(uiControlTypeLabel(50004), QStringLiteral("编辑框")); // UIA_EditControlTypeId
    EXPECT_EQ(uiControlTypeLabel(50011), QStringLiteral("菜单项")); // UIA_MenuItemControlTypeId
    EXPECT_EQ(uiControlTypeLabel(50020), QStringLiteral("文本"));   // UIA_TextControlTypeId
    EXPECT_EQ(uiControlTypeLabel(123456), QStringLiteral("控件"));
}

/// 空链交回空的 optional：光标下没有元素时不许编出一份描述
TEST(UiElementLabels, HeadElementOfRejectsEmptyChain)
{
    EXPECT_FALSE(headElementOf(std::vector<UiElementInfo>{}).has_value()) << "空链居然算出了元素描述";
}

/// 链首没有名字或只有空白都算「没有元素」：交回一条只有控件类型的描述看着像是真认出了什么
TEST(UiElementLabels, HeadElementOfRejectsBlankHeadName)
{
    EXPECT_FALSE(headElementOf(std::vector<UiElementInfo>{levelNamed(QString{})}).has_value()) << "空名不该交回";

    const std::vector<UiElementInfo> blank{levelNamed(QStringLiteral("   "))};
    EXPECT_FALSE(headElementOf(blank).has_value()) << "纯空白名字不该交回";

    // 链上后面那级有名字也不算数：取的是链首那一级
    const std::vector<UiElementInfo> namedBehind{levelNamed(QString{}), levelNamed(QStringLiteral("窗口"))};
    EXPECT_FALSE(headElementOf(namedBehind).has_value()) << "链首无名时不该拿后面那级顶上";
}

/// 链首有名字时原样交回链首：不取链尾，也不与相邻级混成一份
TEST(UiElementLabels, HeadElementOfReturnsDeepestNamedLevel)
{
    const std::vector<UiElementInfo> chain{levelNamed(QStringLiteral("叶片")), levelNamed(QStringLiteral("窗口"))};
    const auto                       head = headElementOf(chain);
    ASSERT_TRUE(head.has_value()) << "链首有名字却什么都没交回";
    EXPECT_EQ(head->name, QStringLiteral("叶片"));
    EXPECT_EQ(head->controlType, chain.front().controlType);
    EXPECT_EQ(head->logicalGlobal, chain.front().logicalGlobal);
}
