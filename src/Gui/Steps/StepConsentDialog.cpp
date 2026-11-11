#include "Gui/Steps/StepConsentDialog.h"

#include "Base/Text.h"
#include "Gui/Controls/Button.h"
#include "Gui/Theme/DesignTokens.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace GyanisScribe::Gui
{
    StepConsentDialog::StepConsentDialog(QWidget *parent) : QDialog(parent)
    {
        setWindowTitle(Base::uiText("步骤记录"));
        setObjectName(QStringLiteral("stepsConsent"));
        setMinimumWidth(460);

        auto *outer = new QVBoxLayout{this};
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        // 顶层窗口的底色由 appRoot 那条样式表规则铺；本窗对象名留给调用方与用例定位，
        // 所以在对话框里再放一层负责铺底
        auto *panel = new QWidget{this};
        panel->setObjectName(QStringLiteral("appRoot"));
        outer->addWidget(panel);

        auto *root = new QVBoxLayout{panel};
        root->setContentsMargins(kSpacingLarge, kSpacingLarge, kSpacingLarge, kSpacingLarge);
        root->setSpacing(kSpacingMedium);

        auto *body = new QLabel{consentTextForTesting(), panel};
        body->setObjectName(QStringLiteral("stepsConsentBody"));
        body->setAccessibleName(Base::uiText("步骤记录说明"));
        body->setTextFormat(Qt::PlainText);
        body->setWordWrap(true);
        // 一行铺满整窗宽的长句读起来要回头数，收到正文的阅读宽度就折行
        body->setMaximumWidth(kIntroMaximumWidth);
        applyTextRole(*body, TextRole::Body);
        root->addWidget(body);

        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(kSpacingSmall);
        root->addLayout(buttons);
        buttons->addStretch(1);

        auto *cancel = new Button{Base::uiText("取消"), panel};
        cancel->setObjectName(QStringLiteral("stepsConsentCancel"));
        // 默认动作是取消：这个说明只会出现一次，误按回车不该直接开录
        cancel->setDefault(true);
        buttons->addWidget(cancel);

        auto *accept = new Button{Base::uiText("开始记录"), panel};
        accept->setObjectName(QStringLiteral("stepsConsentAccept"));
        accept->setDefault(false);
        buttons->addWidget(accept);

        QObject::connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        QObject::connect(accept, &QPushButton::clicked, this, &QDialog::accept);
    }

    StepConsentDialog::~StepConsentDialog() = default;

    QString StepConsentDialog::consentTextForTesting()
    {
        return Base::uiText("开始记录后，智录会记录每次点击的坐标、前台窗口的标题与所属进程，并逐步截取整屏画面。"
                            "所有内容只保存在本机，不会上传；录制期间可以随时暂停或停止。");
    }

} // namespace GyanisScribe::Gui
