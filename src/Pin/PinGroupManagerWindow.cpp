#include "Pin/PinGroupManagerWindow.h"

#include "Core/Text.h"
#include "Pin/PinManager.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace GyanisScribe::Pin
{
    namespace
    {
        /// 组号存在第一列的那个 item 上：行序会随刷新变，按钮要认的是组号而不是行号
        inline constexpr int kGroupNumberRole = Qt::UserRole;

        inline constexpr int kNameColumn   = 0; ///< 第一列：组名（没起过名字就是「分组 N」）
        inline constexpr int kCountColumn  = 1; ///< 第二列：在册张数
        inline constexpr int kActiveColumn = 2; ///< 第三列：是不是此刻摆在桌上的那一组
        inline constexpr int kColumnCount  = 3; ///< 列数，改列时这一格跟着改，表与判据同一处口径

        /// 没起过名字的那一组显示成什么（与右键菜单里那一份同一句式）
        [[nodiscard]] QString displayGroupName(const PinGroupSummary &summary)
        {
            return summary.name.isEmpty() ? GyanisScribe::Core::uiText("分组 %1").arg(summary.group) : summary.name;
        }
    } // namespace

    PinGroupManagerWindow::PinGroupManagerWindow(PinManager &pins, QWidget *parent) : QWidget(parent), m_pins(pins)
    {
        setWindowTitle(GyanisScribe::Core::uiText("贴图组管理"));
        setAccessibleName(GyanisScribe::Core::uiText("贴图组管理窗"));
        // 关掉即销毁：控制器拿的是 QPointer，这一档让它那份指针当场变空，下一次开的是新读的一份表
        setAttribute(Qt::WA_DeleteOnClose);

        m_table = new QTableWidget(0, kColumnCount, this);
        m_table->setAccessibleName(GyanisScribe::Core::uiText("贴图组清单"));
        m_table->setHorizontalHeaderLabels({GyanisScribe::Core::uiText("分组"), GyanisScribe::Core::uiText("张数"), GyanisScribe::Core::uiText("摆在桌上")});
        m_table->verticalHeader()->setVisible(false);
        // 整行选中：四个动作针对的是一组，选到某一格上会让人以为只改那一格
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->setColumnWidth(kNameColumn, 220);
        m_table->setColumnWidth(kCountColumn, 80);
        m_table->setColumnWidth(kActiveColumn, 90);
        m_table->horizontalHeader()->setStretchLastSection(true);

        m_showButton   = new QPushButton(GyanisScribe::Core::uiText("摆出这一组"), this);
        m_renameButton = new QPushButton(GyanisScribe::Core::uiText("重命名…"), this);
        m_voidButton   = new QPushButton(GyanisScribe::Core::uiText("腾空这一组"), this);
        m_deleteButton = new QPushButton(GyanisScribe::Core::uiText("删掉这一组"), this);
        m_closeButton  = new QPushButton(GyanisScribe::Core::uiText("关闭"), this);
        m_showButton->setAccessibleName(GyanisScribe::Core::uiText("摆出这一组"));
        m_renameButton->setAccessibleName(GyanisScribe::Core::uiText("重命名这一组"));
        m_voidButton->setAccessibleName(GyanisScribe::Core::uiText("腾空这一组"));
        m_deleteButton->setAccessibleName(GyanisScribe::Core::uiText("删掉这一组"));
        m_deleteButton->setToolTip(GyanisScribe::Core::uiText("删掉的是「这一组」这个标签：组里的图并回第一组，不会被扔掉。第一组是并回去的落点，删不掉"));

        m_hint = new QLabel(this);
        m_hint->setAccessibleName(GyanisScribe::Core::uiText("贴图组管理提示"));
        m_hint->setWordWrap(true);

        auto *buttons = new QHBoxLayout;
        buttons->addWidget(m_showButton);
        buttons->addWidget(m_renameButton);
        buttons->addWidget(m_voidButton);
        buttons->addWidget(m_deleteButton);
        buttons->addStretch();
        buttons->addWidget(m_closeButton);

        auto *layout = new QVBoxLayout(this);
        layout->addWidget(m_table);
        layout->addLayout(buttons);
        layout->addWidget(m_hint);

        QObject::connect(m_table, &QTableWidget::currentCellChanged, this, [this](int, int, int, int) { updateButtonStates(); });
        QObject::connect(m_showButton, &QPushButton::clicked, this, [this] { showGroupRow(selectedGroup()); });
        QObject::connect(m_renameButton, &QPushButton::clicked, this, &PinGroupManagerWindow::askAndRenameSelectedGroup);
        QObject::connect(m_voidButton, &QPushButton::clicked, this, [this] { voidGroupRow(selectedGroup()); });
        QObject::connect(m_deleteButton, &QPushButton::clicked, this, [this] { deleteGroupRow(selectedGroup()); });
        QObject::connect(m_closeButton, &QPushButton::clicked, this, &QWidget::close);

        // 表上的数会从这里变：贴图被关掉或贴出来、组被改名或删掉、有图挪进别组，都重读一遍
        QObject::connect(&m_pins, &PinManager::pinCountChanged, this, [this] { refresh(); });
        QObject::connect(&m_pins, &PinManager::pinGroupRenamed, this, [this] { refresh(); });
        QObject::connect(&m_pins, &PinManager::pinGroupDeleted, this, [this] { refresh(); });
        QObject::connect(&m_pins, &PinManager::pinMovedToGroup, this, [this] { refresh(); });

        updateButtonStates();
    }

    PinGroupManagerWindow::~PinGroupManagerWindow() = default;

    void PinGroupManagerWindow::openAndRefresh()
    {
        refresh();
        show();
        raise();
        activateWindow();
    }

    QStringList PinGroupManagerWindow::tableReadout() const
    {
        QStringList rows;
        // 读的是表上那三格摆着的文本，不是现从管理器算一份：判据要钉的是界面摆出来的那一句。
        // 换成重算就把「刷新」那一步整个绕过去了——活动组改了而表没重读，这种错正好落在缝里
        for (auto row = 0; row < m_table->rowCount(); ++row)
        {
            const auto *nameItem   = m_table->item(row, kNameColumn);
            const auto *countItem  = m_table->item(row, kCountColumn);
            const auto *activeItem = m_table->item(row, kActiveColumn);
            if (nameItem == nullptr || countItem == nullptr || activeItem == nullptr)
            {
                continue;
            }
            rows << QStringLiteral("%1|%2|%3|%4").arg(nameItem->data(kGroupNumberRole).toInt()).arg(nameItem->text()).arg(countItem->text()).arg(activeItem->text());
        }
        return rows;
    }

    QString PinGroupManagerWindow::hintText() const
    {
        return m_hint->text();
    }

    void PinGroupManagerWindow::refresh()
    {
        const auto summaries = m_pins.groupSummaries();
        m_table->setRowCount(static_cast<int>(summaries.size()));
        for (auto row = 0; row < summaries.size(); ++row)
        {
            const auto &summary = summaries[row];

            auto *nameItem = new QTableWidgetItem(displayGroupName(summary));
            nameItem->setData(kGroupNumberRole, summary.group);
            m_table->setItem(row, kNameColumn, nameItem);
            m_table->setItem(row, kCountColumn, new QTableWidgetItem(GyanisScribe::Core::uiText("%1 张").arg(summary.pinCount)));
            // 活动组那一格写「是」，别的留空：满表都是「否」会把眼睛从要读的那一格上引开
            m_table->setItem(row, kActiveColumn, new QTableWidgetItem(summary.isActive ? GyanisScribe::Core::uiText("是") : QString{}));
        }
        if (summaries.isEmpty())
        {
            // 一张贴图都没有时桌上没有一组在册，这句要说出来：空表读起来像窗体坏了
            say(GyanisScribe::Core::uiText("现在一张贴图都没有，也就没有分组。贴出一张图，再把另一张「移动到分组 → 新建分组」，这里才会多出第二行。"));
        }
        updateButtonStates();
    }

    int PinGroupManagerWindow::selectedGroup() const
    {
        const auto row = m_table->currentRow();
        if (row < 0)
        {
            return 0;
        }
        auto *item = m_table->item(row, kNameColumn);
        if (item == nullptr)
        {
            return 0;
        }
        return item->data(kGroupNumberRole).toInt();
    }

    void PinGroupManagerWindow::updateButtonStates()
    {
        // 四个动作的可用态只认「选中了哪一行」：没选中时按下也不知道该管哪一组，不如先按不住
        const auto hasRow = selectedGroup() > 0;
        m_showButton->setEnabled(hasRow);
        m_renameButton->setEnabled(hasRow);
        m_voidButton->setEnabled(hasRow);
        m_deleteButton->setEnabled(hasRow);
    }

    void PinGroupManagerWindow::showGroupRow(const int group)
    {
        if (group == 0)
        {
            return;
        }
        const auto shown = m_pins.showGroup(group);
        if (!shown)
        {
            say(QString::fromUtf8(shown.error().message.c_str()));
            return;
        }
        // 活动组换了但管理器不发信号（它只在张数与名单变动时出声）：「摆在桌上」那一列得自己重读，
        // 否则表上还指着上一组写着「是」
        refresh();
        say(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("桌上是第 {} 组。"), group).c_str()));
    }

    void PinGroupManagerWindow::askAndRenameSelectedGroup()
    {
        const auto group = selectedGroup();
        if (group == 0)
        {
            return;
        }
        bool       accepted = false;
        const auto text     = QInputDialog::getText(this, GyanisScribe::Core::uiText("给分组起名字"), GyanisScribe::Core::uiText("名字（留空就是取消命名）"), QLineEdit::Normal,
                                                    m_pins.groupName(group), &accepted);
        // 取消输入框是正常结果：什么都不做，也不把上一句回声顶掉
        if (!accepted)
        {
            return;
        }
        renameGroupRow(group, text);
    }

    void PinGroupManagerWindow::renameGroupRow(const int group, const QString &name)
    {
        if (group == 0)
        {
            return;
        }
        const auto renamed = m_pins.renameGroup(group, name);
        if (!renamed)
        {
            say(QString::fromUtf8(renamed.error().message.c_str()));
            return;
        }
        const auto trimmed = name.trimmed();
        say(trimmed.isEmpty() ? GyanisScribe::Core::uiText("已取消命名，这一组回到「分组 N」那种叫法。")
                              : QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("第 {} 组改名为「{}」。"), group, trimmed.toStdString()).c_str()));
    }

    void PinGroupManagerWindow::voidGroupRow(const int group)
    {
        if (group == 0)
        {
            return;
        }
        const auto closed = m_pins.closeGroup(group);
        if (closed == 0)
        {
            say(GyanisScribe::Core::uiText("这一组本来就没有贴图。"));
            return;
        }
        // 说清还能按回来几张：走的仍是「关闭」那一档，而那份册子只留最近这么多张
        say(QString::fromUtf8(
                GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("已关掉这一组的 {} 张。托盘的「重新贴出最近关掉的贴图」还能按回最近 {} 张，更早的找不回来。"), closed,
                                             static_cast<int>(kClosedPinHistorySize))
                        .c_str()));
    }

    void PinGroupManagerWindow::deleteGroupRow(const int group)
    {
        if (group == 0)
        {
            return;
        }
        const auto deleted = m_pins.deleteGroup(group);
        if (!deleted)
        {
            // 第一组删不掉：这条拒绝住在管理器里，界面上不另写一套「哪些组能删」
            say(QString::fromUtf8(deleted.error().message.c_str()));
            return;
        }
        say(QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("第 {} 组已经没了，{} 张并回第一组。"), group, *deleted).c_str()));
    }

    void PinGroupManagerWindow::say(const QString &message)
    {
        m_hint->setText(message);
    }

} // namespace GyanisScribe::Pin
