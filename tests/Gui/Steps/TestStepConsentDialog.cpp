// 首次说明的用例：正文要逐项说清记录范围，默认动作必须是取消，Esc 与「取消」同解。

#include "Gui/Steps/StepConsentDialog.h"

#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>

#include <gtest/gtest.h>

using GyanisScribe::Gui::StepConsentDialog;

TEST(StepConsentDialog, ExplainsWhatIsRecordedAndDefaultsToCancel)
{
    const auto        text = StepConsentDialog::consentTextForTesting();
    StepConsentDialog dialog;
    EXPECT_TRUE(text.contains(QStringLiteral("点击")));
    EXPECT_TRUE(text.contains(QStringLiteral("整屏")));
    EXPECT_TRUE(text.contains(QStringLiteral("本机")));
    EXPECT_TRUE(text.contains(QStringLiteral("暂停")));
    EXPECT_TRUE(text.contains(QStringLiteral("不会上传")));
    EXPECT_TRUE(text.contains(QStringLiteral("进程")));

    auto *body = dialog.findChild<QLabel *>(QStringLiteral("stepsConsentBody"));
    ASSERT_NE(body, nullptr) << "正文要以 stepsConsentBody 命名供用例核对";
    EXPECT_EQ(body->text(), text) << "对话框里显示的正文与 consentTextForTesting() 不是同一份";
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

    // 构造完 result() 本来就是 Rejected；先置成 Accepted，
    // Esc 到 reject 的接线被删掉时这条用例才会红
    dialog.done(static_cast<int>(QDialog::Accepted));
    ASSERT_EQ(dialog.result(), static_cast<int>(QDialog::Accepted)) << "前提：先让对话框带上已接受的结果";

    QSignalSpy rejected{&dialog, &QDialog::rejected};
    ASSERT_TRUE(rejected.isValid());

    QKeyEvent escape{QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier};
    QApplication::sendEvent(&dialog, &escape);
    EXPECT_EQ(rejected.count(), 1) << "Esc 没有发出 rejected";
    EXPECT_EQ(dialog.result(), static_cast<int>(QDialog::Rejected)) << "Esc 没有取消";
}

TEST(StepConsentDialog, ButtonsReturnTheirOwnResults)
{
    {
        StepConsentDialog dialog;
        auto             *accept = dialog.findChild<QPushButton *>(QStringLiteral("stepsConsentAccept"));
        ASSERT_NE(accept, nullptr);

        QSignalSpy accepted{&dialog, &QDialog::accepted};
        ASSERT_TRUE(accepted.isValid());

        accept->click();
        EXPECT_EQ(accepted.count(), 1) << "「开始记录」没有发出 accepted";
        EXPECT_EQ(dialog.result(), static_cast<int>(QDialog::Accepted));
    }
    {
        StepConsentDialog dialog;
        auto             *cancel = dialog.findChild<QPushButton *>(QStringLiteral("stepsConsentCancel"));
        ASSERT_NE(cancel, nullptr);

        // 同上：不先把结果改成 Accepted，删掉取消的接线这条用例照样是绿的
        dialog.done(static_cast<int>(QDialog::Accepted));
        ASSERT_EQ(dialog.result(), static_cast<int>(QDialog::Accepted)) << "前提：先让对话框带上已接受的结果";

        QSignalSpy rejected{&dialog, &QDialog::rejected};
        ASSERT_TRUE(rejected.isValid());

        cancel->click();
        EXPECT_EQ(rejected.count(), 1) << "取消按钮只该发一次 rejected";
        EXPECT_EQ(dialog.result(), static_cast<int>(QDialog::Rejected)) << "取消后结果应是 Rejected";
    }
}
