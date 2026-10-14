#include "Hotkey/HotkeyCombination.h"

#include <QString>

#include <gtest/gtest.h>

using GyanisScribe::Core::ErrorCode;
using GyanisScribe::Hotkey::HotkeyCombination;

namespace
{
    HotkeyCombination parseOrThrow(std::string_view text)
    {
        const auto parsed = HotkeyCombination::fromText(text);
        EXPECT_TRUE(parsed.has_value()) << "文本「" << text << "」应当能解析，实际原因：" << (parsed.has_value() ? std::string{} : parsed.error().message);
        return parsed.has_value() ? *parsed : HotkeyCombination{};
    }
} // namespace

/**
 * @brief 钉住文本形态：别名与顺序都被规整成同一份可比较、可显示的形状
 */
TEST(HotkeyCombination, NormalizesAliasesAndOrderIntoCanonicalText)
{
    EXPECT_EQ(parseOrThrow("ctrl+ALT+a").toText(), QStringLiteral("Ctrl+Alt+A")) << "大小写与顺序都该归一到固定形态";
    EXPECT_EQ(parseOrThrow("Shift+Ctrl+F12").toText(), QStringLiteral("Ctrl+Shift+F12")) << "修饰键顺序必须固定，否则同一组合存出两份文本";
    EXPECT_EQ(parseOrThrow(" super +1 ").toText(), QStringLiteral("Win+1")) << "首尾空格与 Super 别名都要吃下";
    EXPECT_EQ(parseOrThrow("Ctrl+Space").toText(), QStringLiteral("Ctrl+Space"));
    EXPECT_EQ(parseOrThrow("Ctrl+Ctrl+A").toText(), QStringLiteral("Ctrl+A")) << "重复的修饰键要去重，否则与 toText 的输出不同形";
}

/**
 * @brief 钉住解析与构成两条路指向同一个组合（互指判据）
 */
TEST(HotkeyCombination, TextAndEnumFormsPointAtTheSameCombination)
{
    EXPECT_EQ(HotkeyCombination::fromText("Ctrl+Alt+A").value(), (HotkeyCombination(Qt::ControlModifier | Qt::AltModifier, Qt::Key_A)));
    EXPECT_EQ(HotkeyCombination::fromText("Ctrl+Alt+R").value(), (HotkeyCombination(Qt::ControlModifier | Qt::AltModifier, Qt::Key_R)));
    EXPECT_EQ(HotkeyCombination::fromText("Alt+F5").value(), (HotkeyCombination(Qt::AltModifier, Qt::Key_F5)));
    EXPECT_NE(HotkeyCombination::fromText("Ctrl+Alt+A").value(), (HotkeyCombination(Qt::ControlModifier | Qt::AltModifier, Qt::Key_B)))
            << "两个不同按键被判成相等，热键编号就会串到别的动作上";
}

/**
 * @brief 钉住数字与功能键两端的边界
 */
TEST(HotkeyCombination, AcceptsBoundaryKeys)
{
    EXPECT_EQ(parseOrThrow("Ctrl+0").key(), Qt::Key_0);
    EXPECT_EQ(parseOrThrow("Ctrl+9").key(), Qt::Key_9);
    EXPECT_EQ(parseOrThrow("Ctrl+F1").key(), Qt::Key_F1);
    EXPECT_EQ(parseOrThrow("Ctrl+F24").key(), Qt::Key_F24);
    // 单个 F 是字母 F，不是「缺编号的功能键」：两种形状在文本上就分得开，解析也不许混
    EXPECT_EQ(parseOrThrow("Ctrl+Alt+F").key(), Qt::Key_F);
    EXPECT_EQ(parseOrThrow("Ctrl+A").key(), Qt::Key_A);
    EXPECT_EQ(parseOrThrow("Ctrl+Z").key(), Qt::Key_Z);
}

/**
 * @brief 钉住两档松紧：解析收裸键、全局那一档仍拒，而界面那一档收裸键却照样拒修饰键本身
 * @details 「至少要带一个修饰键」是登记到系统才需要的限制。把它留在解析那一步，
 *          界面按键就存不出「R」这一形；拆成两档之后，各自的拒绝面都要能红。
 *          toText() 一并钉：裸键要吐出那颗键自己，写进配置的才是「R」而不是空串——
 *          空文本每次读都算「填的是空」，还会带出一条警告。
 */
TEST(HotkeyCombination, ParsingAcceptsBareKeysWhileTheGlobalCheckStillRejectsThem)
{
    const auto bare = HotkeyCombination::parse("R");
    ASSERT_TRUE(bare.has_value()) << bare.error().message;
    EXPECT_EQ(bare->key(), Qt::Key_R);
    EXPECT_EQ(bare->modifiers(), Qt::NoModifier);
    EXPECT_EQ(bare->toText(), QStringLiteral("R")) << "裸键要吐出那颗键自己，不然存进配置的是空串";
    EXPECT_TRUE(bare->checkUsableInWindow().has_value()) << "界面那一档该收裸键";
    ASSERT_FALSE(bare->checkUsableGlobally().has_value()) << "全局那一档仍要拒：单键会把别的程序的按键吞掉";
    EXPECT_NE(bare->checkUsableGlobally().error().message.find("至少要带一个修饰键"), std::string::npos);

    EXPECT_FALSE(HotkeyCombination::fromText("R").has_value()) << "fromText 归全局那一档，不能因为多了 parse 就放行裸键";

    // 界面那一档也不是什么都收：修饰键本身与不在允许集合里的键照样拒，且文案点名的是这一档
    EXPECT_FALSE(HotkeyCombination(Qt::ControlModifier, Qt::Key_Control).checkUsableInWindow().has_value());
    const auto symbol = HotkeyCombination(Qt::NoModifier, Qt::Key_Escape);
    ASSERT_FALSE(symbol.checkUsableInWindow().has_value());
    EXPECT_NE(symbol.checkUsableInWindow().error().message.find("界面按键"), std::string::npos) << "两档的文案要各自说自己那一档，混着说用户不知道该改成什么";

    // 组合在两档都合法，文本形态与全局那一条同形
    const auto combo = HotkeyCombination::parse("Ctrl+Shift+Z");
    ASSERT_TRUE(combo.has_value()) << combo.error().message;
    EXPECT_EQ(combo->toText(), QStringLiteral("Ctrl+Shift+Z"));
    EXPECT_TRUE(combo->checkUsableInWindow().has_value());
    EXPECT_TRUE(combo->checkUsableGlobally().has_value());
}

/**
 * @brief 钉住拒绝面：单键、空段、越界功能键与不支持的键一律回绝并给替代做法
 */
TEST(HotkeyCombination, RejectsUnusableTexts)
{
    for (const auto &text: {"A", "Space", "1"})
    {
        const auto parsed = HotkeyCombination::fromText(text);
        ASSERT_FALSE(parsed.has_value()) << "单键「" << text << "」不能当全局热键，它会吞掉其它程序的普通按键";
        EXPECT_EQ(parsed.error().code, ErrorCode::InvalidArgument);
        EXPECT_NE(parsed.error().message.find("至少要带一个修饰键"), std::string::npos) << "文案要写出为什么不行，实际：" << parsed.error().message;
    }

    EXPECT_FALSE(HotkeyCombination::fromText("").has_value()) << "空文本不该解析出一个组合";
    EXPECT_FALSE(HotkeyCombination::fromText("Ctrl+").has_value()) << "尾部空段不该被当成按键";
    EXPECT_FALSE(HotkeyCombination::fromText("+Ctrl+A").has_value()) << "头部空段不该被忽略";
    EXPECT_FALSE(HotkeyCombination::fromText("Ctrl+Opt+A").has_value()) << "不认识的修饰键不许猜成 Alt";
    EXPECT_FALSE(HotkeyCombination::fromText("Ctrl+Alt+AB").has_value()) << "多字母的按键段不在允许集合内";
    EXPECT_FALSE(HotkeyCombination::fromText("Ctrl+Alt+F25").has_value()) << "F25 越出允许的功能键段";
    EXPECT_FALSE(HotkeyCombination::fromText("Ctrl+?").has_value()) << "符号键随布局变位，不能登记";

    const auto modifierAsKey = HotkeyCombination(Qt::ControlModifier, Qt::Key_Control).checkUsableGlobally();
    ASSERT_FALSE(modifierAsKey.has_value()) << "按键部分是修饰键本身时会被注册成「按住 Ctrl 就触发」";
    EXPECT_NE(modifierAsKey.error().message.find("不能是修饰键本身"), std::string::npos);
}

/**
 * @brief 钉住未设组合的表现：文本为空且不能通过全局使用检查
 */
TEST(HotkeyCombination, DefaultConstructedCombinationIsUnusable)
{
    const HotkeyCombination unset;
    EXPECT_TRUE(unset.toText().isEmpty()) << "未设的组合不该给界面吐出任何东西";
    EXPECT_EQ(unset.key(), Qt::Key_unknown);
    ASSERT_FALSE(unset.checkUsableGlobally().has_value());
    EXPECT_EQ(unset.checkUsableGlobally().error().code, ErrorCode::InvalidArgument);
}
