#include "Gui/Memory/ImportProgressDialog.h"

#include "Base/Text.h"
#include "Gui/Controls/Button.h"
#include "Gui/Theme/DesignTokens.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace GyanisScribe::Gui
{
    ImportProgressDialog::ImportProgressDialog(QWidget *parent) : QDialog(parent)
    {
        setObjectName(QStringLiteral("appRoot"));
        setWindowTitle(Base::uiText("导入旧截图"));
        // 顶层窗口自己铺底：样式表只给具名的根上底色
        setAttribute(Qt::WA_StyledBackground, true);
        setModal(false);

        auto *root = new QVBoxLayout{this};
        root->setContentsMargins(kSpacingLarge, kSpacingLarge, kSpacingLarge, kSpacingLarge);
        root->setSpacing(kSpacingMedium);

        m_text = new QLabel{this};
        m_text->setObjectName(QStringLiteral("importProgressText"));
        m_text->setWordWrap(true);
        m_text->setMinimumWidth(360);
        root->addWidget(m_text);

        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(kSpacingSmall);
        root->addLayout(buttons);
        buttons->addStretch(1);

        m_cancel = new Button{Base::uiText("取消"), this};
        m_cancel->setObjectName(QStringLiteral("importCancel"));
        m_cancel->setAccessibleName(Base::uiText("取消导入"));
        buttons->addWidget(m_cancel);
        QObject::connect(m_cancel, &QPushButton::clicked, this, &ImportProgressDialog::cancelRequested);

        updateText();
    }

    void ImportProgressDialog::setProgress(const Core::MemoryImportProgress &progress)
    {
        m_progress = progress;
        updateText();
    }

    void ImportProgressDialog::setPendingOcr(const int count)
    {
        m_pendingOcr = count;
        updateText();
    }

    void ImportProgressDialog::updateText()
    {
        QStringList lines;
        lines.append(Base::uiText("正在扫描并导入旧截图…"));
        lines.append(QString::fromUtf8(Base::uiFormat(Base::uiTextUtf8("已扫描 {} 张 · 入库 {} 张 · 跳过 {} 张 · 失败 {} 张"), m_progress.scanned, m_progress.inserted,
                                                      m_progress.skipped, m_progress.failed)
                                               .c_str()));
        // 统计读不出时宁可不写这一行，也不把不知道报成 0 条
        if (m_pendingOcr >= 0)
        {
            lines.append(QString::fromUtf8(Base::uiFormat(Base::uiTextUtf8("待识别 {} 条，导入后将在后台继续识别"), m_pendingOcr).c_str()));
        }
        if (m_progress.cancelled)
        {
            lines.append(Base::uiText("正在取消；已入库的条目与已完成的识别结果将保留。"));
        }
        m_text->setText(lines.join(QChar{u'\n'}));
        // 取消只该点一次：重复点击没有额外作用，灰掉也让「正在取消」这句更可信
        m_cancel->setEnabled(!m_progress.cancelled);
    }

} // namespace GyanisScribe::Gui
