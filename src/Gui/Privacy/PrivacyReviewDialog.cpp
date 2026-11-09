#include "Gui/Privacy/PrivacyReviewDialog.h"

#include "Base/Text.h"
#include "Gui/Theme/DesignTokens.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
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

        auto *note = new QLabel{Base::uiText("只在本机检测，未上传；请逐项确认，打码后可撤销。"), this};
        note->setWordWrap(true);
        root->addWidget(note);

        m_list = new QListWidget{this};
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

        m_selectAll  = new QPushButton{Base::uiText("全选"), this};
        m_selectNone = new QPushButton{Base::uiText("全不选"), this};
        m_selectAll->setObjectName(QStringLiteral("privacySelectAll"));
        m_selectNone->setObjectName(QStringLiteral("privacySelectNone"));
        buttons->addWidget(m_selectAll);
        buttons->addWidget(m_selectNone);
        buttons->addStretch(1);

        buttons->addWidget(new QLabel{Base::uiText("打码方式"), this});
        m_style = new QComboBox{this};
        m_style->setObjectName(QStringLiteral("privacyStyle"));
        m_style->setAccessibleName(Base::uiText("打码方式"));
        m_style->addItem(Base::uiText("马赛克"));
        m_style->addItem(Base::uiText("高斯模糊"));
        buttons->addWidget(m_style);

        auto *cancel = new QPushButton{Base::uiText("取消"), this};
        cancel->setObjectName(QStringLiteral("privacyCancel"));
        auto *apply = new QPushButton{Base::uiText("应用打码"), this};
        apply->setObjectName(QStringLiteral("privacyApply"));
        // 回车等同应用：用户逐项确认后最常见的动作是直接落笔，默认按钮让键盘路径一致
        apply->setDefault(true);
        buttons->addWidget(cancel);
        buttons->addWidget(apply);

        QObject::connect(m_selectAll, &QPushButton::clicked, this, [this] { setAllCheckStates(Qt::Checked); });
        QObject::connect(m_selectNone, &QPushButton::clicked, this, [this] { setAllCheckStates(Qt::Unchecked); });
        QObject::connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        QObject::connect(apply, &QPushButton::clicked, this, &QDialog::accept);
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

} // namespace GyanisScribe::Gui
