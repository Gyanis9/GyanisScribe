#include "Gui/History/HistoryView.h"

#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/History/HistoryThumbnail.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/DesignTokens.h"

#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QImageReader>
#include <QLabel>
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
    using GyanisScribe::Core::HistoryEntry;
    using GyanisScribe::Core::HistoryKind;

    /// 列表项里存成品路径的那个角色
    constexpr int kPathRole = Qt::UserRole + 1;

    // 路径与界面字符串的换算沿用 Base::Paths::toDisplayText / fromDisplayText，这里不再抄一份
} // namespace

namespace GyanisScribe::Gui
{
    QString entryCaption(const Core::HistoryEntry &entry, const QDateTime &now)
    {
        // 相对日期放最前：用户找的是「今天上午那几张」，绝对日期要先在心里换算一次
        const auto dayDifference = entry.moment.date().daysTo(now.date());
        QString    dayText;
        if (dayDifference == 0)
        {
            dayText = GyanisScribe::Base::uiText("今天 ") + entry.moment.toString(QStringLiteral("HH:mm"));
        } else if (dayDifference == 1)
        {
            dayText = GyanisScribe::Base::uiText("昨天 ") + entry.moment.toString(QStringLiteral("HH:mm"));
        } else
        {
            dayText = entry.moment.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        }

        // 尺寸那一格只在两侧都落到 1 以上的时候写：默认构造是 (-1,-1)，而旧清单里还躺着 0×0 的记录，
        // 两种都是「读不出」，硬打一个 0×0 等于把不知道报成一个数
        QString caption = dayText + QStringLiteral(" · ") + Core::historyKindLabel(entry.kind);
        if (!entry.size.isEmpty())
        {
            caption += QStringLiteral(" · ") + QString::number(entry.size.width()) + QStringLiteral("×") + QString::number(entry.size.height());
        }
        return caption;
    }

    QStringList historyFilterLabels()
    {
        // 类别名沿用 Core::historyKindLabel：改中文说法时下拉与列表一起改，不会只改一半
        return {GyanisScribe::Base::uiText("全部"), Core::historyKindLabel(Core::HistoryKind::Screenshot), Core::historyKindLabel(Core::HistoryKind::Recording)};
    }

    std::optional<Core::HistoryKind> kindFilterAt(const int index)
    {
        switch (index)
        {
            case 1:
                return Core::HistoryKind::Screenshot;
            case 2:
                return Core::HistoryKind::Recording;
            default:
                // 第 0 档与越界都按「不限」：列表宁可多给几行，也不该出现一个谁都不选的空白
                return {};
        }
    }

    QVector<Core::HistoryEntry> visibleEntries(const QVector<Core::HistoryEntry> &entries, const QString &query, const std::optional<Core::HistoryKind> &kindFilter)
    {
        QVector<Core::HistoryEntry> visible;
        for (const auto &entry: entries)
        {
            if (Core::matchesFilter(entry, query, kindFilter))
            {
                visible.append(entry);
            }
        }
        return visible;
    }

    HistoryView::HistoryView(Core::HistoryStore &store, const Core::HistoryRetentionPolicy &policy, Gui::IconProvider &icons, QWidget *parent) :
        QWidget(parent), m_store(store), m_policy(policy), m_icons(icons)
    {
        auto *root = new QVBoxLayout{this};

        auto *tools = new QHBoxLayout;
        root->addLayout(tools);

        m_search = new QLineEdit{this};
        m_search->setPlaceholderText(GyanisScribe::Base::uiText("按文件名、类别或日期搜索"));
        m_search->setClearButtonEnabled(true);
        m_search->setAccessibleName(GyanisScribe::Base::uiText("历史搜索"));
        tools->addWidget(m_search, 1);

        m_filter = new QComboBox{this};
        m_filter->addItems(historyFilterLabels());
        m_filter->setAccessibleName(GyanisScribe::Base::uiText("历史类别"));
        tools->addWidget(m_filter);

        auto                                                           *pinButton    = new QPushButton{GyanisScribe::Base::uiText("重新贴图"), this};
        auto                                                           *ocrButton    = new QPushButton{GyanisScribe::Base::uiText("重新识别"), this};
        auto                                                           *folderButton = new QPushButton{GyanisScribe::Base::uiText("打开所在目录"), this};
        auto                                                           *deleteButton = new QPushButton{GyanisScribe::Base::uiText("删除"), this};
        auto                                                           *clearButton  = new QPushButton{GyanisScribe::Base::uiText("清空列表"), this};
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
        m_list->setIconSize(QSize{Core::kThumbnailEdge, Core::kThumbnailEdge});
        m_list->setUniformItemSizes(true);
        m_list->setResizeMode(QListView::Adjust);
        m_list->setMovement(QListView::Static);
        m_list->setSpacing(8);
        m_list->setAccessibleName(GyanisScribe::Base::uiText("历史列表"));
        // 卡片上的时间与尺寸是读数：锁等宽后整列数字不随条目左右跳
        applyTabularFigures(*m_list, TextScript::Mixed);
        root->addWidget(m_list, 1);

        // 空列表给一句引导（界面审查 UI-64）：一片白卡读起来像记录丢了或窗体坏了。
        // 提示挂在视口上并居中、不吃鼠标：列表的卡片底与圆角仍由 QSS 画
        m_emptyHint = new QLabel{m_list->viewport()};
        m_emptyHint->setObjectName(QStringLiteral("pageHint"));
        m_emptyHint->setAccessibleName(GyanisScribe::Base::uiText("历史空态提示"));
        m_emptyHint->setAlignment(Qt::AlignCenter);
        m_emptyHint->setWordWrap(true);
        m_emptyHint->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_list->viewport()->installEventFilter(this);
        m_emptyHint->setVisible(m_list->count() == 0);

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
                             emit messageForUser(GyanisScribe::Base::uiText("先在历史里选一张图"), Gui::ToastKind::Information);
                         });
        QObject::connect(ocrButton, &QPushButton::clicked, this,
                         [this]
                         {
                             if (const auto path = selectedPath(); path)
                             {
                                 emit ocrRequested(*path);
                                 return;
                             }
                             emit messageForUser(GyanisScribe::Base::uiText("先在历史里选一张图"), Gui::ToastKind::Information);
                         });
        QObject::connect(folderButton, &QPushButton::clicked, this,
                         [this]
                         {
                             if (const auto path = selectedPath(); path)
                             {
                                 emit openFolderRequested(*path);
                                 return;
                             }
                             emit messageForUser(GyanisScribe::Base::uiText("先在历史里选一张图"), Gui::ToastKind::Information);
                         });
        QObject::connect(deleteButton, &QPushButton::clicked, this, &HistoryView::removeSelected);
        QObject::connect(clearButton, &QPushButton::clicked, this,
                         [this]
                         {
                             if (m_entries.isEmpty())
                             {
                                 emit messageForUser(GyanisScribe::Base::uiText("历史列表本来就是空的"), Gui::ToastKind::Information);
                                 return;
                             }
                             // 清空是一下去掉一整批：先问一句，别让人一次点击丢掉整个清单
                             const auto answer = QMessageBox::question(this, GyanisScribe::Base::uiText("清空历史列表"),
                                                                       GyanisScribe::Base::uiText("要把历史列表里的记录全部去掉吗？成品文件本身不会被删除，仍在各自目录里。"),
                                                                       QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                             if (answer == QMessageBox::Yes)
                             {
                                 emit clearAllRequested();
                             }
                         });

        // 建好就自读一次：程序重启后第一次打开这一页时盘上没有新增，靠外部触发要等到下一次保存才出得来，
        // 而用户在那之前看到的是空列表——那读起来跟「记录丢了」一模一样
        refresh();
    }

    void HistoryView::refresh()
    {
        auto loaded = m_store.load(m_policy, QDateTime::currentDateTime());
        if (!loaded)
        {
            // 清单坏了不能把列表清空了事：留着上一次的行，用户还能找到那批文件
            Base::Logger::warning(std::format("历史清单没能读出来：{}", loaded.error().message));
            emit messageForUser(
                    QString::fromUtf8(
                            GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("历史清单没能读出来：{}。列表先显示上一次的记录"), loaded.error().message).c_str()),
                    Gui::ToastKind::Danger);
            return;
        }

        m_entries = *loaded;
        refillList();
    }

    bool HistoryView::eventFilter(QObject *watched, QEvent *event)
    {
        if (watched == m_list->viewport() && event->type() == QEvent::Resize)
        {
            placeEmptyHint();
        }
        return QWidget::eventFilter(watched, event);
    }

    void HistoryView::placeEmptyHint()
    {
        const QRect area = m_list->viewport()->rect();
        m_emptyHint->setGeometry(area.adjusted(kSpacingLarge, kSpacingLarge, -kSpacingLarge, -kSpacingLarge));
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
        return GyanisScribe::Base::Paths::fromDisplayText(text);
    }

    void HistoryView::refillList()
    {
        const auto visible = visibleEntries(m_entries, m_search->text(), kindFilterAt(m_filter->currentIndex()));

        m_list->clear();
        for (const auto &entry: visible)
        {
            auto *item = new QListWidgetItem{iconFor(entry), entryCaption(entry, QDateTime::currentDateTime())};
            item->setData(kPathRole, GyanisScribe::Base::Paths::toDisplayText(entry.filePath));
            item->setToolTip(GyanisScribe::Base::Paths::toDisplayText(entry.filePath) + GyanisScribe::Base::uiText("（删除只从历史里去掉这条，成品文件留在原处）"));
            m_list->addItem(item);
        }

        // 两种「列表是空的」分开说：真没有记录，与筛选后没有命中——后者说「还没有记录」就是撒谎
        m_emptyHint->setText(m_entries.isEmpty() ? GyanisScribe::Base::uiText("还没有记录。截一张图或录一段屏，成品会出现在这里。")
                                                 : GyanisScribe::Base::uiText("没有符合条件的记录，换个关键词或类别试试。"));
        m_emptyHint->setVisible(m_list->count() == 0);
        if (m_list->count() == 0)
        {
            placeEmptyHint();
            m_emptyHint->raise();
        }
    }

    QIcon HistoryView::iconFor(const Core::HistoryEntry &entry) const
    {
        QImageReader reader{GyanisScribe::Base::Paths::toDisplayText(entry.filePath)};
        reader.setAutoTransform(true);
        const auto image = reader.read();
        if (image.isNull())
        {
            // 视频与读不了的图都给一枚类别图标：列表里出现一片空白方块比图标更难看
            return m_icons.icon(entry.kind == Core::HistoryKind::Recording ? "Record" : "CaptureRegion");
        }
        return QIcon{QPixmap::fromImage(Core::makeThumbnail(image, QSize{Core::kThumbnailEdge, Core::kThumbnailEdge}, Core::kThumbnailCornerRadius))};
    }

    void HistoryView::removeSelected()
    {
        const auto path = selectedPath();
        if (!path)
        {
            emit messageForUser(GyanisScribe::Base::uiText("先在历史里选一张图"), Gui::ToastKind::Information);
            return;
        }
        emit removeRequested(*path);
    }

} // namespace GyanisScribe::Gui
