#include "Ui/HotkeyEdit.h"

#include <gtest/gtest.h>

#include <Qt>

using GyanisScribe::Ui::editableKeyFromEvent;

/**
 * @brief 钉住「修饰键本身不是热键的按键本体」
 * @details 只按到 Ctrl 时组合还不完整，收下它就会把半截组合写进配置，
 *          重启后那一档热键登记不上，症状离原因很远。
 */
TEST(HotkeyEditMapping, ModifierKeysAreNotHotkeyBodies)
{
    for (const auto key: {Qt::Key_Control, Qt::Key_Shift, Qt::Key_Alt, Qt::Key_AltGr, Qt::Key_Meta, Qt::Key_CapsLock, Qt::Key_NumLock, Qt::Key_ScrollLock})
    {
        EXPECT_FALSE(editableKeyFromEvent(key).has_value()) << static_cast<int>(key);
    }
}

/**
 * @brief 钉住小键盘数字归一到主键盘
 * @details 同一颗键在两台机器上存成两种写法，用户换台机器就会发现热键「莫名其妙变了」；
 *          字母与功能键则原样通过，不该被这次归一误伤。
 */
TEST(HotkeyEditMapping, KeypadDigitsNormalizeToTheMainRow)
{
    // 小键盘数字在事件里是「数字键值 | KeypadModifier」，Qt6 把这个组合叫 QKeyCombination，
    // 这里按它的整数形态造出来，判据是归一之后回到主键盘那一枚键
    const auto keypadFive = static_cast<Qt::Key>(static_cast<int>(Qt::KeypadModifier) | static_cast<int>(Qt::Key_5));

    EXPECT_EQ(editableKeyFromEvent(Qt::Key_5), Qt::Key_5);
    EXPECT_EQ(editableKeyFromEvent(keypadFive), Qt::Key_5);
    EXPECT_EQ(editableKeyFromEvent(Qt::Key_A), Qt::Key_A);
    EXPECT_EQ(editableKeyFromEvent(Qt::Key_F7), Qt::Key_F7);
    EXPECT_EQ(editableKeyFromEvent(Qt::Key_Space), Qt::Key_Space);
}
