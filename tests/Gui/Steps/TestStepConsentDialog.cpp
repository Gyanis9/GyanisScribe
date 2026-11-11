// 首次说明的用例：正文要逐项说清记录范围，默认动作必须是取消，Esc 与「取消」同解。

#include "Gui/Steps/StepConsentDialog.h"

#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QPushButton>

#include <gtest/gtest.h>

using GyanisScribe::Gui::StepConsentDialog;

TEST(StepConsentDialog, ExplainsWhatIsRecordedAndDefaultsToCancel)
{
    const auto text = StepConsentDialog::consentTextForTesting();
    EXPECT_TRUE(text.contains(QStringLiteral("点击")));
    EXPECT_TRUE(text.contains(QStringLiteral("整屏")));
    EXPECT_TRUE(text.contains(QStringLiteral("本机")));
    EXPECT_TRUE(text.contains(QStringLiteral("暂停")));
    EXPECT_TRUE(text.contains(QStringLiteral("不会上传")));
    EXPECT_TRUE(text.contains(QStringLiteral("进程")));
}

TEST(StepConsentDialog, CancelIsTheDefaultAndEscapeRejects)
{
    StepConsentDialog dialog;
    EXPECT_EQ(dialog.objectName(), QStringLiteral("stepsConsent"));
    EXPECT_EQ(dialog.windowTitle(), QStringLiteral("步骤记录"));

    auto *accept = dialog.findChild<QPushButton *>(QStringLiteral("stepsConsentAccept"));
    auto *cancel = dialog.findChild<QPushButton *>(QStringLiteral("stepsConsentCancel"));
    ASSERT_NE(accept, nullptr);
    ASSERT_NE(cancel, nullptr);
    EXPECT_EQ(accept->text(), QStringLiteral("开始记录"));
    EXPECT_EQ(cancel->text(), QStringLiteral("取消"));
    EXPECT_TRUE(cancel->isDefault()) << "回车会落到开始记录上";
    EXPECT_FALSE(accept->isDefault());

    QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
    QApplication::sendEvent(&dialog, &escape);
    EXPECT_EQ(dialog.result(), static_cast<int>(QDialog::Rejected)) << "Esc 没有取消";
}

TEST(StepConsentDialog, ButtonsReturnTheirOwnResults)
{
    {
        StepConsentDialog dialog;
        auto             *accept = dialog.findChild<QPushButton *>(QStringLiteral("stepsConsentAccept"));
        ASSERT_NE(accept, nullptr);
        accept->click();
        EXPECT_EQ(dialog.result(), static_cast<int>(QDialog::Accepted));
    }
    {
        StepConsentDialog dialog;
        auto             *cancel = dialog.findChild<QPushButton *>(QStringLiteral("stepsConsentCancel"));
        ASSERT_NE(cancel, nullptr);
        cancel->click();
        EXPECT_EQ(dialog.result(), static_cast<int>(QDialog::Rejected));
    }
}
