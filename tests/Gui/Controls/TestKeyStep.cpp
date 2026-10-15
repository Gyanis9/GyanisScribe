// 方向键到「一格位移」的那张表：四个方向各钉一个向量，再把不归它管的键钉回空。
// 遮罩的选区微调与标注的选中项微调共用这一份，方向写反在两个界面上都会看出来，但只有这里能一次钉住四格。
#include "Gui/Controls/KeyStep.h"

#include <gtest/gtest.h>

#include <QPoint>

using GyanisScribe::Gui::arrowStepFor;

/**
 * @brief 钉住四个方向的符号与「一格就是一格」
 * @details Qt 的屏幕坐标 y 轴朝下，所以 Up 是 -1。写反不会编译报错，只会让上下键看起来是反的。
 */
TEST(KeyStep, ArrowKeysMapToTheirOwnStep)
{
    const QPoint left{-1, 0};
    const QPoint right{1, 0};
    const QPoint up{0, -1};
    const QPoint down{0, 1};

    EXPECT_EQ(arrowStepFor(Qt::Key_Left), left);
    EXPECT_EQ(arrowStepFor(Qt::Key_Right), right);
    EXPECT_EQ(arrowStepFor(Qt::Key_Up), up);
    EXPECT_EQ(arrowStepFor(Qt::Key_Down), down);
}

/**
 * @brief 拒绝面：不是方向键就不许给位移
 * @details 认下不认识的键，调用方就再也分不清「这条键不归微调管」与「这条键要挪到 (0,0)」，
 *          界面级快捷键会静默少掉几项。
 */
TEST(KeyStep, OtherKeysYieldNoStep)
{
    EXPECT_FALSE(arrowStepFor(Qt::Key_A).has_value());
    EXPECT_FALSE(arrowStepFor(Qt::Key_Tab).has_value());
    EXPECT_FALSE(arrowStepFor(Qt::Key_Enter).has_value());
    EXPECT_FALSE(arrowStepFor(0).has_value());
}
