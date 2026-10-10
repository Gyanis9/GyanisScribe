#include "History/HistoryView.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Text.h"
#include "History/HistoryThumbnail.h"
#include "Ui/IconProvider.h"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QLineEdit>
#include <QPixmap>
#include <QListWidget>
#include <QMessageBox>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVariant>
#include <QVBoxLayout>

#include <array>
#include <utility>

#include <array>
#include <utility>

namespace
{
    using GyanisScribe::History::HistoryEntry;
    using GyanisScribe::History::HistoryKind;

    /// 列表项里存成品路径的那个角色
    constexpr int kPathRole = Qt::UserRole + 1;

    /// 筛选下拉的第 0 档：不限类别
    constexpr int kFilterIndexAll = 0;

    // 路径与界面字符串的换算沿用 Core::Paths::toDisplayText / fromDisplayText，这里不再抄一份
} // namespace

namespace GyanisScribe::History
{
    QString entryCaption(const HistoryEntry &entry, const QDateTime &now)
    {
        // 相对日期放最前：用户找的是「今天上午那几张」，绝对日期要先在心里换算一次
        const auto dayDifference = entry.moment.date().daysTo(now.date());
        QString    dayText;
        if (dayDifference == 0)
        {
            dayText = GyanisScribe::Core::uiText("今天 ") + entry.moment.toString(QStringLiteral("HH:mm"));
        } else if (dayDifference == 1)
        {
            dayText = GyanisScribe::Core::uiText("昨天 ") + entry.moment.toString(QStringLiteral("HH:mm"));
        } else
        {
            dayText = entry.moment.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        }

        return dayText + QStringLiteral(" · ") + historyKindLabel(entry.kind) + QStringLiteral(" · ") + QString::number(entry.size.width()) + QStringLiteral("×") +
               QString::number(entry.size.height());
    }

    QStringList historyFilterLabels()
    {
        // 类别名沿用 historyKindLabel：改中文说法时下拉与列表一起改，不会只改一半
        return {GyanisScribe::Core::uiText("全部"), historyKindLabel(HistoryKind::Screenshot), historyKindLabel(HistoryKind::Recording)};
    }

    std::optional<HistoryKind> kindFilterAt(const int index)
    {
        switch (index)
        {
            case 1:
                return HistoryKind::Screenshot;
            case 2:
                return HistoryKind::Recording;
            default:
                // 第 0 档与越界都按「不限」：列表宁可多给几行，也不该出现一个谁都不选的空白
                return {};
        }
    }

    QVector<HistoryEntry> visibleEntries(const QVector<HistoryEntry> &entries, const QString &query, const std::optional<HistoryKind> &kindFilter)
    {
        QVector<HistoryEntry> visible;
        for (const auto &entry: entries)
        {
            if (matchesFilter(entry, query, kindFilter))
            {
                visible.append(entry);
            }
        }
        return visible;
    }

    HistoryView::HistoryView(HistoryStore &store, const HistoryRetentionPolicy &policy, Ui::IconProvider &icons, QWidget *parent) :
        QWidget(parent), m_store(store), m_policy(policy), m_icons(icons)
    {
        auto *root = new QVBoxLayout{this};

        auto *tools = new QHBoxLayout;
        root->addLayout(tools);

        m_search = new QLineEdit{this};
        m_search->setPlaceholderText(GyanisScribe::Core::uiText("按文件名、类别或日期搜索"));
        m_search->setClearButtonEnabled(true);
        m_search->setAccessibleName(GyanisScribe::Core::uiText("历史搜索"));
        tools->addWidget(m_search, 1);

        m_filter = new QComboBox{this};
        m_filter->addItems(historyFilterLabels());
        m_filter->setAccessibleName(GyanisScribe::Core::uiText("历史类别"));
        tools->addWidget(m_filter);

        auto                                                           *pinButton    = new QPushButton{GyanisScribe::Core::uiText("重新贴图"), this};
        auto                                                           *ocrButton    = new QPushButton{GyanisScribe::Core::uiText("重新识别"), this};
        auto                                                           *folderButton = new QPushButton{GyanisScribe::Core::uiText("打开所在目录"), this};
        auto                                                           *deleteButton = new QPushButton{GyanisScribe::Core::uiText("删除"), this};
        auto                                                           *clearButton  = new QPushButton{GyanisScribe::Core::uiText("清空列表"), this};
        const std::array<std::pair<QPushButton *, std::string_view>, 5> buttons      = {
                std::pair{pinButton, "Pin"}, std::pair{ocrButton, "Ocr"}, std::pair{folderButton, "Folder"}, std::pair{deleteButton, "Delete"}, std::pair{clearButton, "Delete"},
        };
        for (const auto &[button, iconName]: buttons)
        {
            // 每一枚都要图标 + tooltip + 无障碍名，缺一件就是键盘与读屏用户找不到门
            m_icons.applyIcon(*button, iconName);
            button->setToolTip(button->text());
            button->setAccessibleName(button->text());
            tools->addWidget(button);
        }

        m_list = new QListWidget{this};
        m_list->setViewMode(QListView::IconMode);
        m_list->setIconSize(QSize{kThumbnailEdge, kThumbnailEdge});
        m_list->setUniformItemSizes(true);
        m_list->setResizeMode(QListView::Adjust);
        m_list->setMovement(QListView::Static);
        m_list->setSpacing(8);
        m_list->setAccessibleName(GyanisScribe::Core::uiText("历史列表"));
        root->addWidget(m_list, 1);

        QObject::connect(m_search, &QLineEdit::textChanged, this, [this] { refillList(); });
        QObject::connect(m_filter, &QComboBox::currentIndexChanged, this, [this] { refillList(); });
        QObject::connect(pinButton, &QPushButton::clicked, this,
                         [this]
                         {
                             if (const auto path = selectedPath(); path)
                             {
                                 emit pinRequested(*path);
                                 return;
                             }
                             emit messageForUser(GyanisScribe::Core::uiText("先在历史里选一张图"), Ui::ToastKind::Information);
                         });
        QObject::connect(ocrButton, &QPushButton::clicked, this,
                         [this]
                         {
                             if (const auto path = selectedPath(); path)
                             {
                                 emit ocrRequested(*path);
                                 return;
                             }
                             emit messageForUser(GyanisScribe::Core::uiText("先在历史里选一张图"), Ui::ToastKind::Information);
                         });
        QObject::connect(folderButton, &QPushButton::clicked, this,
                         [this]
                         {
                             if (const auto path = selectedPath(); path)
                             {
                                 emit openFolderRequested(*path);
                                 return;
                             }
                             emit messageForUser(GyanisScribe::Core::uiText("先在历史里选一张图"), Ui::ToastKind::Information);
                         });
        QObject::connect(deleteButton, &QPushButton::clicked, this, &HistoryView::removeSelected);
        QObject::connect(clearButton, &QPushButton::clicked, this,
                         [this]
                         {
                             if (m_entries.isEmpty())
                             {
                                 emit messageForUser(GyanisScribe::Core::uiText("历史列表本来就是空的"), Ui::ToastKind::Information);
                                 return;
                             }
                             // 清空是一下去掉一整批：先问一句，别让人一次点击丢掉整个清单
                             const auto answer = QMessageBox::question(this, GyanisScribe::Core::uiText("清空历史列表"),
                                                                       GyanisScribe::Core::uiText("要把历史列表里的记录全部去掉吗？成品文件本身不会被删除，仍在各自目录里。"),
                                                                       QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                             if (answer == QMessageBox::Yes)
                             {
                                 emit clearAllRequested();
                             }
                         });
    }

    void HistoryView::refresh()
    {
        auto loaded = m_store.load(m_policy, QDateTime::currentDateTime());
        if (!loaded)
        {
            // 清单坏了不能把列表清空了事：留着上一次的行，用户还能找到那批文件
            Core::Logger::warning(std::format("历史清单没能读出来：{}", loaded.error().message));
            emit messageForUser(
                    QString::fromUtf8(
                            GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("历史清单没能读出来：{}。列表先显示上一次的记录"), loaded.error().message).c_str()),
                    Ui::ToastKind::Danger);
            return;
        }

        m_entries = *loaded;
        refillList();
    }

    int HistoryView::rowCount() const
    {
        return m_list == nullptr ? 0 : m_list->count();
    }

    QString HistoryView::firstCaption() const
    {
        const auto *item = m_list->item(0);
        return item == nullptr ? QString{} : item->text();
    }

    std::optional<std::filesystem::path> HistoryView::selectedPath() const
    {
        const auto *item = m_list->currentItem();
        if (item == nullptr)
        {
            return {};
        }
        const auto text = item->data(kPathRole).toString();
        if (text.isEmpty())
        {
            return {};
        }
        return GyanisScribe::Core::Paths::fromDisplayText(text);
    }

    void HistoryView::refillList()
    {
        const auto visible = visibleEntries(m_entries, m_search->text(), kindFilterAt(m_filter->currentIndex()));

        m_list->clear();
        for (const auto &entry: visible)
        {
            auto *item = new QListWidgetItem{iconFor(entry), entryCaption(entry, QDateTime::currentDateTime())};
            item->setData(kPathRole, GyanisScribe::Core::Paths::toDisplayText(entry.filePath));
            item->setToolTip(GyanisScribe::Core::Paths::toDisplayText(entry.filePath) + GyanisScribe::Core::uiText("（删除只从历史里去掉这条，成品文件留在原处）"));
            m_list->addItem(item);
        }
    }

    QIcon HistoryView::iconFor(const HistoryEntry &entry) const
    {
        QImageReader reader{GyanisScribe::Core::Paths::toDisplayText(entry.filePath)};
        reader.setAutoTransform(true);
        const auto image = reader.read();
        if (image.isNull())
        {
            // 视频与读不了的图都给一枚类别图标：列表里出现一片空白方块比图标更难看
            return m_icons.icon(entry.kind == HistoryKind::Recording ? "Record" : "CaptureRegion");
        }
        return QIcon{QPixmap::fromImage(makeThumbnail(image, QSize{kThumbnailEdge, kThumbnailEdge}, kThumbnailCornerRadius))};
    }

    void HistoryView::removeSelected()
    {
        const auto path = selectedPath();
        if (!path)
        {
            emit messageForUser(GyanisScribe::Core::uiText("先在历史里选一张图"), Ui::ToastKind::Information);
            return;
        }
        emit removeRequested(*path);
    }

} // namespace GyanisScribe::History
