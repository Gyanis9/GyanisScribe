#include "Gui/Memory/MemoryExcludeDialog.h"

#include "Base/Text.h"
#include "Gui/Controls/Button.h"
#include "Gui/Controls/LineEdit.h"
#include "Gui/Controls/ListWidget.h"
#include "Gui/Theme/DesignTokens.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
    /// 清单行里存进程名的那个角色
    constexpr int kProcessRole = Qt::UserRole + 1;

    /// 名单比较不区分大小写：协调器保存前会把进程名归一成小写
    [[nodiscard]] bool sameProcess(const QString &left, const QString &right)
    {
        return QString::compare(left, right, Qt::CaseInsensitive) == 0;
    }

    [[nodiscard]] bool containsProcess(const QStringList &processes, const QString &process)
    {
        return std::ranges::any_of(processes, [&process](const QString &candidate) { return sameProcess(candidate, process); });
    }
} // namespace

namespace GyanisScribe::Gui
{
    MemoryExcludeDialog::MemoryExcludeDialog(std::vector<Core::MemoryAppCount> apps, const QStringList &excludedProcesses, QWidget *parent) :
        QDialog(parent), m_apps(std::move(apps)), m_excluded(excludedProcesses)
    {
        setObjectName(QStringLiteral("appRoot"));
        setWindowTitle(Base::uiText("排除应用"));
        // 顶层窗口自己铺底：样式表只给具名的根上底色
        setAttribute(Qt::WA_StyledBackground, true);

        auto *root = new QVBoxLayout{this};
        root->setContentsMargins(kSpacingLarge, kSpacingLarge, kSpacingLarge, kSpacingLarge);
        root->setSpacing(kSpacingMedium);

        auto *note = new QLabel{Base::uiText("勾选的应用将不再加入记忆库；保存时会删除这些应用已有的索引。导入的旧截图不受影响。"), this};
        note->setWordWrap(true);
        root->addWidget(note);

        m_list = new ListWidget{this};
        m_list->setObjectName(QStringLiteral("excludeList"));
        m_list->setAccessibleName(Base::uiText("排除应用清单"));
        m_list->setSelectionMode(QAbstractItemView::NoSelection);
        root->addWidget(m_list, 1);

        // 先按索引里的应用分布列，再补上已排除但当前没有条目的进程：后者也要能取消勾选
        for (const auto &app: m_apps)
        {
            if (app.processName.isEmpty())
            {
                // 空进程名代表导入条目，不受排除操作影响，不列出来
                continue;
            }
            auto *row = new QListWidgetItem{Base::uiText("%1（%2 张）").arg(app.processName).arg(app.count), m_list};
            row->setData(kProcessRole, app.processName);
            row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
            row->setCheckState(containsProcess(m_excluded, app.processName) ? Qt::Checked : Qt::Unchecked);
        }
        QStringList extras;
        for (const QString &process: m_excluded)
        {
            if (process.isEmpty())
            {
                continue;
            }
            const bool listed = std::ranges::any_of(m_apps, [&process](const Core::MemoryAppCount &app) { return sameProcess(app.processName, process); });
            if (!listed && !containsProcess(extras, process))
            {
                extras.append(process);
            }
        }
        for (const QString &process: extras)
        {
            auto *row = new QListWidgetItem{process, m_list};
            row->setData(kProcessRole, process);
            row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
            row->setCheckState(Qt::Checked);
        }

        // 还没出现过的进程也要能提前排除：输入后回车或点添加，勾一行新条目
        auto *addRow = new QHBoxLayout;
        addRow->setSpacing(kSpacingSmall);
        root->addLayout(addRow);
        auto *input = new LineEdit{this};
        input->setObjectName(QStringLiteral("excludeProcessInput"));
        input->setPlaceholderText(Base::uiText("输入要排除的进程名…"));
        input->setAccessibleName(Base::uiText("新增排除进程"));
        addRow->addWidget(input, 1);
        auto *add = new Button{Base::uiText("添加"), this};
        add->setObjectName(QStringLiteral("excludeAddProcess"));
        addRow->addWidget(add);
        const auto addTypedProcess = [this, input]
        {
            const QString process = input->text().trimmed();
            if (process.isEmpty())
            {
                return;
            }
            for (int row = 0; row < m_list->count(); ++row)
            {
                if (sameProcess(m_list->item(row)->data(kProcessRole).toString(), process))
                {
                    // 已经有这一行就只勾上，不再补一条重复的
                    m_list->item(row)->setCheckState(Qt::Checked);
                    input->clear();
                    return;
                }
            }
            auto *row = new QListWidgetItem{process, m_list};
            row->setData(kProcessRole, process);
            row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
            row->setCheckState(Qt::Checked);
            m_list->scrollToItem(row);
            input->clear();
        };
        QObject::connect(add, &QPushButton::clicked, this, addTypedProcess);
        QObject::connect(input, &QLineEdit::returnPressed, this, addTypedProcess);

        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(kSpacingSmall);
        root->addLayout(buttons);
        buttons->addStretch(1);

        auto *cancel = new Button{Base::uiText("取消"), this};
        cancel->setObjectName(QStringLiteral("excludeCancel"));
        auto *save = new Button{Base::uiText("保存"), this};
        save->setObjectName(QStringLiteral("excludeSave"));
        save->setDefault(true);
        buttons->addWidget(cancel);
        buttons->addWidget(save);

        QObject::connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        QObject::connect(save, &QPushButton::clicked, this, &MemoryExcludeDialog::confirmSelection);
    }

    QStringList MemoryExcludeDialog::selectedProcesses() const
    {
        return m_selected;
    }

    int MemoryExcludeDialog::pendingDeletionCount(const std::vector<Core::MemoryAppCount> &apps, const QStringList &previouslyExcluded, const QStringList &selected)
    {
        int count = 0;
        for (const auto &app: apps)
        {
            if (app.processName.isEmpty() || containsProcess(previouslyExcluded, app.processName) || !containsProcess(selected, app.processName))
            {
                continue;
            }
            count += app.count;
        }
        return count;
    }

    void MemoryExcludeDialog::confirmSelection()
    {
        m_selected.clear();
        for (int row = 0; row < m_list->count(); ++row)
        {
            if (m_list->item(row)->checkState() == Qt::Checked)
            {
                m_selected.append(m_list->item(row)->data(kProcessRole).toString());
            }
        }

        // 保存会删掉新排除应用已有的索引：先把条数摆出来，别让一次勾选变成意外删库
        const int deletionCount = pendingDeletionCount(m_apps, m_excluded, m_selected);
        if (deletionCount > 0)
        {
            const auto answer = QMessageBox::question(this, Base::uiText("排除应用"),
                                                      Base::uiText("保存后会从记忆库删除新排除应用已有的 %1 条索引，原图与历史记录不会被删除。是否继续？").arg(deletionCount),
                                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
            {
                return;
            }
        }
        accept();
    }

} // namespace GyanisScribe::Gui
