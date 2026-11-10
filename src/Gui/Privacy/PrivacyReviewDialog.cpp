#include "Gui/Privacy/PrivacyReviewDialog.h"

#include "Base/Text.h"
#include "Core/Annotation/AnnotationTool.h"
#include "Gui/Controls/Button.h"
#include "Gui/Controls/ComboBox.h"
#include "Gui/Controls/ListWidget.h"
#include "Gui/Theme/DesignTokens.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>

#include <cstddef>
#include <utility>

namespace
{
    /**
     * @brief 命中类型的中文标签
     * @param kind 命中类型
     * @return 给审核人看的类型名
     */
    QString typeLabel(const GyanisScribe::Core::Privacy::PiiKind kind)
    {
        using GyanisScribe::Core::Privacy::PiiKind;

        switch (kind)
        {
            case PiiKind::Phone:
                return GyanisScribe::Base::uiText("手机号");
            case PiiKind::Landline:
                return GyanisScribe::Base::uiText("固定电话");
            case PiiKind::IdCard:
                return GyanisScribe::Base::uiText("身份证");
            case PiiKind::BankCard:
                return GyanisScribe::Base::uiText("银行卡");
            case PiiKind::Email:
                return GyanisScribe::Base::uiText("邮箱");
            case PiiKind::Ip:
                return GyanisScribe::Base::uiText("IP 地址");
            case PiiKind::Barcode:
                return GyanisScribe::Base::uiText("二维码/条形码");
        }
        return {};
    }
} // namespace

namespace GyanisScribe::Gui
{
    PrivacyReviewDialog::PrivacyReviewDialog(std::vector<Core::Privacy::PiiItem> items, QWidget *parent) : QDialog(parent), m_items(std::move(items))
    {
        setWindowTitle(Base::uiText("隐私打码审核"));
        // 顶层窗口自己铺底：样式表只给具名的根上底色
        setObjectName(QStringLiteral("appRoot"));
        setAttribute(Qt::WA_StyledBackground, true);

        auto *root = new QVBoxLayout{this};
        root->setContentsMargins(kSpacingLarge, kSpacingLarge, kSpacingLarge, kSpacingLarge);
        root->setSpacing(kSpacingMedium);

        auto *note = new QLabel{Base::uiText("仅在本机检测，不会上传；请逐项确认，打码后可撤销。"), this};
        note->setObjectName(QStringLiteral("privacyNote"));
        note->setWordWrap(true);
        root->addWidget(note);

        m_list = new ListWidget{this};
        m_list->setObjectName(QStringLiteral("privacyList"));
        m_list->setAccessibleName(Base::uiText("敏感信息清单"));
        // 清单只做勾选：行文本来自脱敏样例，不给编辑入口
        for (const auto &item: m_items)
        {
            const QString rowText = Base::uiText("%1：%2").arg(typeLabel(item.kind), item.maskedSample);
            auto         *row     = new QListWidgetItem{rowText, m_list};
            row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
            row->setCheckState(Qt::Checked);
        }
        root->addWidget(m_list, 1);

        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(kSpacingSmall);
        root->addLayout(buttons);

        auto *selectAll  = new Button{Base::uiText("全选"), this};
        auto *selectNone = new Button{Base::uiText("取消全选"), this};
        selectAll->setObjectName(QStringLiteral("privacySelectAll"));
        selectNone->setObjectName(QStringLiteral("privacySelectNone"));
        buttons->addWidget(selectAll);
        buttons->addWidget(selectNone);
        buttons->addStretch(1);

        buttons->addWidget(new QLabel{Base::uiText("打码方式"), this});
        m_style = new ComboBox{this};
        m_style->setObjectName(QStringLiteral("privacyStyle"));
        m_style->setAccessibleName(Base::uiText("打码方式"));
        m_style->addItem(Base::uiText("马赛克"));
        m_style->addItem(Base::uiText("高斯模糊"));
        buttons->addWidget(m_style);

        auto *cancel = new Button{Base::uiText("取消"), this};
        cancel->setObjectName(QStringLiteral("privacyCancel"));
        m_apply = new Button{Base::uiText("应用打码"), this};
        m_apply->setObjectName(QStringLiteral("privacyApply"));
        // 回车等同应用：用户逐项确认后最常见的动作是直接落笔，默认按钮让键盘路径一致
        m_apply->setDefault(true);
        buttons->addWidget(cancel);
        buttons->addWidget(m_apply);

        QObject::connect(selectAll, &QPushButton::clicked, this, [this] { setAllCheckStates(Qt::Checked); });
        QObject::connect(selectNone, &QPushButton::clicked, this, [this] { setAllCheckStates(Qt::Unchecked); });
        // 逐项取消到一项不剩时「应用打码」也该灰掉：空批次没有可落的笔
        QObject::connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *) { updateApplyEnabled(); });
        QObject::connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        QObject::connect(m_apply, &QPushButton::clicked, this, &QDialog::accept);
        updateApplyEnabled();
    }

    std::vector<Core::Privacy::PiiItem> PrivacyReviewDialog::selectedItems() const
    {
        std::vector<Core::Privacy::PiiItem> selected;
        selected.reserve(m_items.size());
        for (int row = 0; row < m_list->count(); ++row)
        {
            // 行下标就是 m_items 的下标：构造时逐条追加，之后不增删行
            if (m_list->item(row)->checkState() == Qt::Checked)
            {
                selected.push_back(m_items[static_cast<std::size_t>(row)]);
            }
        }
        return selected;
    }

    Core::ToolKind PrivacyReviewDialog::redactionTool() const noexcept
    {
        // 下拉固定两档：0 是马赛克，1 是高斯模糊
        return m_style->currentIndex() == 1 ? Core::ToolKind::Blur : Core::ToolKind::Mosaic;
    }

    void PrivacyReviewDialog::setAllCheckStates(const Qt::CheckState checkState)
    {
        for (int row = 0; row < m_list->count(); ++row)
        {
            m_list->item(row)->setCheckState(checkState);
        }
    }

    void PrivacyReviewDialog::updateApplyEnabled()
    {
        m_apply->setEnabled(!selectedItems().empty());
    }

} // namespace GyanisScribe::Gui
