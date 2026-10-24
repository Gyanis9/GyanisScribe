#include "Gui/Controls/CheckBox.h"

#include "Gui/Theme/DesignTokens.h"
#include "Gui/Theme/ThemeManager.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QStyleOptionButton>

using GyanisScribe::Gui::CheckBox;
using GyanisScribe::Gui::ThemeKind;

/**
 * @brief 钉住自绘复选框：QSS 静态勾图被抑制，勾选从缩小档弹回满幅
 * @details 关动画时勾选直接满幅；开动画后取消再勾选，缩放先落在 0.55，
 *          推进到弹簧稳定时长之后精确收在 1.0。
 */
TEST(CheckBoxChrome, SuppressesTheStaticQssCheckAndAnimatesTheCheck)
{
    qApp->setStyleSheet(GyanisScribe::Gui::ThemeManager::styleSheetFor(ThemeKind::Light));
    GyanisScribe::Gui::setTextThemeKind(ThemeKind::Light);

    CheckBox box;
    box.setAnimationsEnabled(false);
    box.setChecked(true);
    EXPECT_TRUE(box.property("animatedIndicator").toBool());
    EXPECT_DOUBLE_EQ(box.checkScale(), 1.0);

    box.setAnimationsEnabled(true);
    box.setChecked(false);
    box.setChecked(true);
    EXPECT_LT(box.checkScale(), 1.0) << "勾选没有从缩小档起步";
    box.advanceCheckAnimationForTesting(600);
    EXPECT_DOUBLE_EQ(box.checkScale(), 1.0) << "弹簧没有收在 1.0";

    qApp->setStyleSheet(QString{});
}
