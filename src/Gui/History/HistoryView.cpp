#include "Gui/History/HistoryView.h"

#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/History/HistoryThumbnail.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/DesignTokens.h"

#include <QComboBox>
#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QListWidget>
#include <QMessageBox>
#include <QListWidgetItem>
#include <QPushButton>
#include <QShowEvent>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace
{
    using GyanisScribe::Core::HistoryEntry;
    using GyanisScribe::Core::HistoryKind;

    /// 列表项里存成品路径的那个角色
    constexpr int kPathRole = Qt::UserRole + 1;

    /// 缩略图备到 3x 屏那一档：再高的缩放比按 3 档放大，1x/2x/3x 覆盖现有屏幕。
    /// 三档一律生成、不做按屏裁剪，每张的位图合计约 0.5 MiB，这是缓存容量按张数计的代价来源
    constexpr int kLargestThumbnailDevicePixelRatio = 3;

    /**
     * @brief 缩略图缓存最多存多少张
     * @details 一张含 1x/2x/3x 三档，按 3x 计算约 0.5 MiB，128 张的上限约 64 MiB；
     *          默认保留上限是 200 条，128 覆盖一屏加几屏滚动。按张数算容量是为了淘汰次序可预期——
     *          三个 DPR 档的像素数差四倍，按字节算会让「同一张图在不同屏幕上占的额度」跟着屏幕走。
     *          淘汰按最近使用次序：列表按时间倒序装填，一轮解码不查缓存，所以两百条时最先被挤掉的
     *          是最新的 72 张；下一次刷新会先补解它们，正好是屏幕上最靠前的几张。这个次序是有意的
     *          取舍，不是漏算：常在眼前滚动的几张重解一遍，换掉离屏的旧图占的额度。
     */
    constexpr int kThumbnailCacheCapacity = 128;

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

        // 空列表给一句引导：一片白卡读起来像记录丢了或窗体坏了。
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

        // 缩略图逐张解：0ms 单次定时器每响一次挑一条交给工作线程，列表先摆出来，图随后一张张换上去
        m_thumbnailWatcher = new QFutureWatcher<DecodedThumbnail>{this};
        QObject::connect(m_thumbnailWatcher, &QFutureWatcher<DecodedThumbnail>::finished, this, &HistoryView::applyDecodedThumbnail);

        m_thumbnailTimer = new QTimer{this};
        m_thumbnailTimer->setSingleShot(true);
        m_thumbnailTimer->setInterval(0);
        QObject::connect(m_thumbnailTimer, &QTimer::timeout, this, &HistoryView::startNextThumbnail);
        m_thumbnailCache.setMaxCost(kThumbnailCacheCapacity);
    }

    void HistoryView::setPolicy(const Core::HistoryRetentionPolicy &policy)
    {
        m_policy = policy;
    }

    int HistoryView::thumbnailDecodeCountForTesting() const
    {
        return m_thumbnailDecodeCount;
    }

    bool HistoryView::lastDecodeRanOffGuiThreadForTesting() const noexcept
    {
        return m_lastDecodeOffGuiThread;
    }

    void HistoryView::showEvent(QShowEvent *event)
    {
        QWidget::showEvent(event);

        m_thumbnailSuspended = false;

        // 第一次显示才读清单；隐藏期间来过刷新请求的，这一次补读并解缩略图
        if (!m_loaded || m_refreshWhileHidden)
        {
            refresh();
        }
        // 读清单失败时上一次没解完的队列还留着：这里照样把它排上，不能跟着 refresh 一起提前返回
        scheduleThumbnails();
    }

    void HistoryView::hideEvent(QHideEvent *event)
    {
        // 定时器停了队列还留着：在飞的那张回来后会退回队首，下次显示接着解
        m_thumbnailTimer->stop();
        m_thumbnailSuspended = true;
        QWidget::hideEvent(event);
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
        m_loaded  = true;
        refillList();

        // 隐藏时只摆行，不解缩略图；标记下来让第一次显示补读一次清单
        m_refreshWhileHidden = !isVisible();
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

        // 整批重建：旧下标全部失效，未解的那批也一并作废。代际号让在飞任务的结果落地时作废，
        // 但不去打断它手里的读文件——那份像素已经读出来了，只是不再属于这一批行
        ++m_thumbnailGeneration;
        m_thumbnailTimer->stop();
        m_pendingThumbnails.clear();
        m_list->clear();
        for (const auto &entry: visible)
        {
            auto *item = new QListWidgetItem{entryCaption(entry, QDateTime::currentDateTime())};
            item->setData(kPathRole, GyanisScribe::Base::Paths::toDisplayText(entry.filePath));
            item->setToolTip(GyanisScribe::Base::Paths::toDisplayText(entry.filePath) + GyanisScribe::Base::uiText("（删除只从历史里去掉这条，成品文件留在原处）"));
            m_list->addItem(item);

            // 先查缓存再谈占位图：命中时省下那一次图标查找，未命中才需要一枚先顶上
            const auto key = thumbnailCacheKey(entry);
            if (const auto *cached = m_thumbnailCache.object(key); cached != nullptr)
            {
                item->setIcon(*cached);
                continue;
            }

            // 没命中先摆类别图标占位，真图等定时器那一批解出来再换
            item->setIcon(fallbackIcon(entry));
            m_pendingThumbnails.append(PendingThumbnail{
                    .index = QPersistentModelIndex{m_list->model()->index(m_list->count() - 1, 0)},
                    .entry = entry,
            });
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

        scheduleThumbnails();
    }

    QIcon HistoryView::fallbackIcon(const Core::HistoryEntry &entry) const
    {
        // 视频与读不了的图都给一枚类别图标：列表里出现一片空白方块比图标更难看
        return m_icons.icon(entry.kind == Core::HistoryKind::Recording ? "Record" : "CaptureRegion");
    }

    QString HistoryView::thumbnailCacheKey(const Core::HistoryEntry &entry)
    {
        // 修改时间进毫秒：同一路径被新内容覆盖时，键跟着变，旧图自然被当成另一张
        const QFileInfo info{GyanisScribe::Base::Paths::toDisplayText(entry.filePath)};
        return GyanisScribe::Base::Paths::toDisplayText(entry.filePath) + QChar{u'|'} + QString::number(info.lastModified().toMSecsSinceEpoch()) + QChar{u'|'} +
               QString::number(info.size());
    }

    HistoryView::DecodedThumbnail HistoryView::decodeThumbnailPixels(const QString &displayPath)
    {
        DecodedThumbnail decoded;
        decoded.decodedOffGuiThread = QThread::currentThread() != QCoreApplication::instance()->thread();

        QImageReader reader{displayPath};
        reader.setAutoTransform(true);

        // 读一次就够：先按最大那档 DPR 的覆盖尺寸预缩，三档缩略图都从这一份像素上裁
        if (const auto source = reader.size(); source.isValid() && !source.isEmpty())
        {
            const auto edge  = Core::kThumbnailEdge * kLargestThumbnailDevicePixelRatio;
            const auto ratio = std::max(static_cast<double>(edge) / static_cast<double>(source.width()), static_cast<double>(edge) / static_cast<double>(source.height()));
            reader.setScaledSize(
                    QSize{static_cast<int>(std::ceil(static_cast<double>(source.width()) * ratio)), static_cast<int>(std::ceil(static_cast<double>(source.height()) * ratio))});
        }

        const auto image = reader.read();
        if (image.isNull())
        {
            decoded.readFailed = true;
            return decoded;
        }

        decoded.oneX   = Core::makeThumbnail(image, QSize{Core::kThumbnailEdge, Core::kThumbnailEdge}, Core::kThumbnailCornerRadius);
        decoded.twoX   = Core::makeThumbnail(image, QSize{Core::kThumbnailEdge * 2, Core::kThumbnailEdge * 2}, Core::kThumbnailCornerRadius * 2);
        decoded.threeX = Core::makeThumbnail(image, QSize{Core::kThumbnailEdge * kLargestThumbnailDevicePixelRatio, Core::kThumbnailEdge * kLargestThumbnailDevicePixelRatio},
                                             Core::kThumbnailCornerRadius * kLargestThumbnailDevicePixelRatio);
        return decoded;
    }

    void HistoryView::scheduleThumbnails()
    {
        if (!isVisible() || m_thumbnailSuspended || m_pendingThumbnails.isEmpty() || m_inFlightThumbnail.has_value() || m_thumbnailTimer->isActive())
        {
            return;
        }
        m_thumbnailTimer->start();
    }

    void HistoryView::startNextThumbnail()
    {
        if (m_inFlightThumbnail.has_value() || !isVisible() || m_thumbnailSuspended)
        {
            return;
        }

        while (!m_pendingThumbnails.isEmpty())
        {
            auto pending = m_pendingThumbnails.takeFirst();
            if (!pending.index.isValid() || m_list->itemFromIndex(pending.index) == nullptr)
            {
                // 列表已经重建过：这个下标指向的项不在了，旧图也不必再解
                continue;
            }

            // 键在读之前取：读一张大图要上百毫秒，期间文件被换掉时，这一份像素对应的仍是旧键
            pending.cacheKey    = thumbnailCacheKey(pending.entry);
            pending.generation  = m_thumbnailGeneration;
            m_inFlightThumbnail = pending;

            const auto displayPath = GyanisScribe::Base::Paths::toDisplayText(pending.entry.filePath);
            m_thumbnailWatcher->setFuture(QtConcurrent::run([displayPath] { return decodeThumbnailPixels(displayPath); }));
            return;
        }

        // 队列里只剩失效条目：这里没活可干，交给调度收尾，不再空转定时器
        scheduleThumbnails();
    }

    void HistoryView::applyDecodedThumbnail()
    {
        // 结果只由 finished 触发，正常顺序下一定有一条在飞条目；这里没有就无处可落，也无需再排一次
        if (!m_inFlightThumbnail.has_value())
        {
            return;
        }
        auto pending = std::move(*m_inFlightThumbnail);
        m_inFlightThumbnail.reset();

        const auto decoded = m_thumbnailWatcher->result();

        // 列表在解码期间重建过：结果对应的已是另一批行，不计数也不写缓存
        if (pending.generation != m_thumbnailGeneration)
        {
            scheduleThumbnails();
            return;
        }

        // 看不见的时候不解：这一条退回队首，下次显示接着来
        if (!isVisible() || m_thumbnailSuspended)
        {
            m_pendingThumbnails.prepend(pending);
            return;
        }

        auto *item = pending.index.isValid() ? m_list->itemFromIndex(pending.index) : nullptr;
        if (item == nullptr)
        {
            scheduleThumbnails();
            return;
        }

        QIcon icon;
        if (!decoded.readFailed)
        {
            const auto addTier = [&icon](const QImage &image, const int ratio)
            {
                if (image.isNull())
                {
                    return;
                }
                auto bitmap = QPixmap::fromImage(image);
                // 缩放比写在位图上：列表按 96 逻辑像素摆，取图时引擎才会挑到这一档
                bitmap.setDevicePixelRatio(ratio);
                icon.addPixmap(bitmap);
            };
            addTier(decoded.oneX, 1);
            addTier(decoded.twoX, 2);
            addTier(decoded.threeX, kLargestThumbnailDevicePixelRatio);
        }
        if (icon.isNull())
        {
            icon = fallbackIcon(pending.entry);
        }

        item->setIcon(icon);
        // 键取自发起解码前的那份在飞记录：读的这几百毫秒里文件换了名或换了内容，这次结果也不算到新键上。
        // 兜底图标也记在键下：视频与坏图每次刷新都重开一遍，等于每敲一个字就白读一批
        m_thumbnailCache.insert(pending.cacheKey, new QIcon{icon}, 1);
        ++m_thumbnailDecodeCount;
        m_lastDecodeOffGuiThread = decoded.decodedOffGuiThread;
        scheduleThumbnails();
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
