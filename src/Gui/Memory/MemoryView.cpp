#include "Gui/Memory/MemoryView.h"

#include "Base/Paths.h"
#include "Base/Text.h"
#include "Core/History/HistoryThumbnail.h"
#include "Core/Memory/DuplicateHash.h"
#include "Gui/Controls/Button.h"
#include "Gui/Controls/CheckBox.h"
#include "Gui/Controls/ComboBox.h"
#include "Gui/Controls/LineEdit.h"
#include "Gui/Controls/ListWidget.h"
#include "Gui/Controls/SurfaceWidget.h"
#include "Gui/Memory/ImportProgressDialog.h"
#include "Gui/Memory/MemoryExcludeDialog.h"
#include "Gui/Resources/IconProvider.h"
#include "Gui/Theme/DesignTokens.h"

#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <utility>

namespace
{
    using GyanisScribe::Core::MemoryResult;

    /// 列表项里存条目主键的那个角色
    constexpr int kIdRole = Qt::UserRole + 1;

    /// 列表项里存原图显示路径的那个角色
    constexpr int kPathRole = Qt::UserRole + 2;

    /// 列表项里存「原图已不在」标记的那个角色
    constexpr int kMissingRole = Qt::UserRole + 3;

    /// 列表项里存重复组号的那个角色；非组头为 -1
    constexpr int kGroupRole = Qt::UserRole + 4;

    /// 一张缩略图含 1x/2x/3x 三档，按 3x 算约 0.5 MiB；128 张约 64 MiB，覆盖一屏加几屏滚动
    constexpr int kMemoryThumbnailCacheCapacity = 128;

    /// 时间下拉的档位，单位天；0 表示不限
    constexpr int kTimeFilterAll   = 0;
    constexpr int kTimeFilterToday = 1;
    constexpr int kTimeFilterWeek  = 7;
    constexpr int kTimeFilterMonth = 30;

    /// 空进程名在应用筛选里的保留取值
    [[nodiscard]] QString unknownProcessFilter()
    {
        return QString::fromLatin1(GyanisScribe::Core::kUnknownProcessFilter.data(), static_cast<qsizetype>(GyanisScribe::Core::kUnknownProcessFilter.size()));
    }

    /// 应用显示名：空进程名代表导入条目，别把一格空白当应用名
    [[nodiscard]] QString appLabel(const QString &processName)
    {
        return processName.isEmpty() ? GyanisScribe::Base::uiText("未知应用") : processName;
    }

    /// 相对日期：今天与昨天带时刻，更早的写绝对日期
    [[nodiscard]] QString momentLabel(const QDateTime &moment)
    {
        const auto dayDifference = moment.date().daysTo(QDate::currentDate());
        if (dayDifference == 0)
        {
            return GyanisScribe::Base::uiText("今天 ") + moment.toString(QStringLiteral("HH:mm"));
        }
        if (dayDifference == 1)
        {
            return GyanisScribe::Base::uiText("昨天 ") + moment.toString(QStringLiteral("HH:mm"));
        }
        return moment.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    }

    /**
     * @brief 一张结果卡片的文字
     * @param result 结果行
     * @return 第一行是 OCR 摘要，第二行是应用、标题、时间、尺寸与状态
     * @details 摘要为空就明说「没有识别到文字」：留一截空白会被读成加载中或界面坏了。
     */
    [[nodiscard]] QString cardText(const MemoryResult &result)
    {
        QStringList lines;
        lines.append(result.ocrSnippet.isEmpty() ? GyanisScribe::Base::uiText("没有识别到文字") : result.ocrSnippet);

        QStringList meta;
        meta.append(appLabel(result.processName));
        if (!result.windowTitle.isEmpty())
        {
            meta.append(result.windowTitle);
        }
        meta.append(momentLabel(result.capturedAt));
        if (!result.size.isEmpty())
        {
            meta.append(QStringLiteral("%1×%2").arg(result.size.width()).arg(result.size.height()));
        }
        if (result.fileMissing)
        {
            meta.append(GyanisScribe::Base::uiText("文件已不在"));
        } else if (result.ocrStatus == QStringLiteral("pending"))
        {
            meta.append(GyanisScribe::Base::uiText("待识别"));
        } else if (result.ocrStatus == QStringLiteral("failed"))
        {
            meta.append(GyanisScribe::Base::uiText("识别失败"));
        }
        lines.append(meta.join(QStringLiteral(" · ")));
        return lines.join(QChar{u'\n'});
    }

    /// 重复组头：条数与最新一张的时间
    [[nodiscard]] QString groupText(const int count, const QDateTime &latest)
    {
        return GyanisScribe::Base::uiText("%1 张重复/相似 · 最新 %2").arg(count).arg(momentLabel(latest));
    }
} // namespace

namespace GyanisScribe::Gui
{
    MemoryView::MemoryView(Core::MemoryIndex &index, Gui::IconProvider &icons, QWidget *parent) : QWidget(parent), m_index(index), m_icons(icons)
    {
        setAccessibleName(Base::uiText("记忆库"));
        m_thumbnailCache.setMaxCost(kMemoryThumbnailCacheCapacity);

        auto *root = new QVBoxLayout{this};

        // 顶部横幅：功能未开启、识别不可用与数据库错误都挤在这一条里，正常时整条收起
        auto *bannerSurface = new SurfaceWidget{Core::CornerRole::Panel, SurfaceFillRole::Surface, this};
        m_bannerFrame       = bannerSurface;
        m_bannerFrame->setObjectName(QStringLiteral("memoryBannerFrame"));
        m_bannerFrame->setVisible(false);
        auto *bannerLayout = new QHBoxLayout{m_bannerFrame};
        bannerLayout->setContentsMargins(kSpacingMedium, kSpacingSmall, kSpacingMedium, kSpacingSmall);
        bannerLayout->setSpacing(kSpacingSmall);
        m_banner = new QLabel{m_bannerFrame};
        m_banner->setObjectName(QStringLiteral("memoryBanner"));
        m_banner->setWordWrap(true);
        m_banner->setAccessibleName(Base::uiText("记忆库状态提示"));
        bannerLayout->addWidget(m_banner, 1);

        m_enableButton = new Button{Base::uiText("开启记忆"), m_bannerFrame};
        m_enableButton->setObjectName(QStringLiteral("memoryEnable"));
        m_enableButton->setAccessibleName(Base::uiText("开启记忆"));
        m_enableButton->setVisible(false);
        m_icons.applyIcon(*m_enableButton, "Power");
        bannerLayout->addWidget(m_enableButton);

        m_rebuildButton = new Button{Base::uiText("重建索引"), m_bannerFrame};
        m_rebuildButton->setObjectName(QStringLiteral("memoryRebuild"));
        m_rebuildButton->setAccessibleName(Base::uiText("重建索引"));
        m_rebuildButton->setVisible(false);
        m_icons.applyIcon(*m_rebuildButton, "RebuildIndex");
        bannerLayout->addWidget(m_rebuildButton);
        root->addWidget(m_bannerFrame);

        auto *tools = new QHBoxLayout;
        root->addLayout(tools);

        m_search = new Gui::LineEdit{this};
        m_search->setObjectName(QStringLiteral("memorySearch"));
        m_search->setPlaceholderText(Base::uiText("搜索截图里的文字…"));
        m_search->setClearButtonEnabled(true);
        m_search->setAccessibleName(Base::uiText("记忆库搜索"));
        tools->addWidget(m_search, 1);

        m_appFilter = new Gui::ComboBox{this};
        m_appFilter->setObjectName(QStringLiteral("memoryAppFilter"));
        m_appFilter->setAccessibleName(Base::uiText("按应用筛选"));
        m_appFilter->addItem(Base::uiText("全部应用"), QString{});
        tools->addWidget(m_appFilter);

        m_timeFilter = new Gui::ComboBox{this};
        m_timeFilter->setObjectName(QStringLiteral("memoryTimeFilter"));
        m_timeFilter->setAccessibleName(Base::uiText("按时间筛选"));
        m_timeFilter->addItem(Base::uiText("全部"), kTimeFilterAll);
        m_timeFilter->addItem(Base::uiText("今天"), kTimeFilterToday);
        m_timeFilter->addItem(Base::uiText("7 天"), kTimeFilterWeek);
        m_timeFilter->addItem(Base::uiText("30 天"), kTimeFilterMonth);
        tools->addWidget(m_timeFilter);

        m_mergeToggle = new Gui::CheckBox{this};
        m_mergeToggle->setObjectName(QStringLiteral("memoryMergeToggle"));
        m_mergeToggle->setText(Base::uiText("合并重复图"));
        m_mergeToggle->setAccessibleName(Base::uiText("合并重复图"));
        m_mergeToggle->setChecked(true);
        tools->addWidget(m_mergeToggle);

        m_importButton = new Button{Base::uiText("导入旧截图"), this};
        m_importButton->setObjectName(QStringLiteral("memoryImport"));
        m_importButton->setToolTip(m_importButton->text());
        m_importButton->setAccessibleName(m_importButton->text());
        m_icons.applyIcon(*m_importButton, "Import");
        tools->addWidget(m_importButton);

        auto *actions = new QHBoxLayout;
        root->addLayout(actions);

        m_pinButton        = new Button{Base::uiText("贴图"), this};
        m_openFileButton   = new Button{Base::uiText("打开文件"), this};
        m_openFolderButton = new Button{Base::uiText("打开所在目录"), this};
        m_retryButton      = new Button{Base::uiText("重新识别"), this};
        m_removeButton     = new Button{Base::uiText("从记忆库移除"), this};
        m_pinButton->setObjectName(QStringLiteral("memoryPin"));
        m_openFileButton->setObjectName(QStringLiteral("memoryOpenFile"));
        m_openFolderButton->setObjectName(QStringLiteral("memoryOpenFolder"));
        m_retryButton->setObjectName(QStringLiteral("memoryRetry"));
        m_removeButton->setObjectName(QStringLiteral("memoryRemove"));
        const std::array<std::pair<QPushButton *, std::string_view>, 5> actionButtons = {
                std::pair{m_pinButton, "Pin"},   std::pair{m_openFileButton, "OpenFile"}, std::pair{m_openFolderButton, "Folder"},
                std::pair{m_retryButton, "Ocr"}, std::pair{m_removeButton, "Delete"},
        };
        for (const auto &[button, iconName]: actionButtons)
        {
            // 每一枚都要图标 + tooltip + 无障碍名，缺一件就是键盘与读屏用户找不到门
            m_icons.applyIcon(*button, iconName);
            button->setToolTip(button->text());
            button->setAccessibleName(button->text());
            actions->addWidget(button);
        }
        actions->addStretch(1);

        m_list = new Gui::ListWidget{this};
        m_list->setObjectName(QStringLiteral("memoryList"));
        m_list->setViewMode(QListView::IconMode);
        m_list->setIconSize(QSize{Core::kThumbnailEdge, Core::kThumbnailEdge});
        // 卡片两行、组头一行，高度不一：统一项尺寸会让其中一种被裁掉
        m_list->setUniformItemSizes(false);
        m_list->setResizeMode(QListView::Adjust);
        m_list->setMovement(QListView::Static);
        m_list->setSpacing(8);
        // 卡片宽按两行正文定：默认图标格宽会把应用名、时间与尺寸折成四五截，看着像另一种字体
        m_list->setGridSize(QSize{220, 200});
        m_list->setWordWrap(true);
        m_list->setSelectionMode(QAbstractItemView::SingleSelection);
        m_list->setAccessibleName(Base::uiText("记忆结果"));
        // 卡片上的时间与尺寸是读数：锁等宽后整列数字不随条目左右跳
        applyTabularFigures(*m_list, TextScript::Mixed);
        root->addWidget(m_list, 1);

        // 空列表给一句引导：一片白卡读起来像索引丢了或窗体坏了。
        // 提示挂在视口上并居中、不吃鼠标：列表的卡片底与圆角由 Gui::ListWidget 自绘，提示只是压在视口上的一层字
        m_emptyHint = new QLabel{m_list->viewport()};
        m_emptyHint->setObjectName(QStringLiteral("memoryEmptyHint"));
        m_emptyHint->setAccessibleName(Base::uiText("记忆库空态提示"));
        m_emptyHint->setAlignment(Qt::AlignCenter);
        m_emptyHint->setWordWrap(true);
        m_emptyHint->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_list->viewport()->installEventFilter(this);

        auto *footer = new QHBoxLayout;
        root->addLayout(footer);
        m_stats = new QLabel{this};
        m_stats->setObjectName(QStringLiteral("memoryStats"));
        m_stats->setAccessibleName(Base::uiText("记忆库统计"));
        footer->addWidget(m_stats, 1);

        // 后端恢复后要有出口：只有「重新识别」一条一条点，积压的待识别行就永远清不完
        m_retryPendingButton = new Button{Base::uiText("重试待处理"), this};
        m_retryPendingButton->setObjectName(QStringLiteral("memoryRetryPending"));
        m_retryPendingButton->setToolTip(m_retryPendingButton->text());
        m_retryPendingButton->setAccessibleName(m_retryPendingButton->text());
        m_icons.applyIcon(*m_retryPendingButton, "Refresh");
        m_retryPendingButton->setVisible(false);
        footer->addWidget(m_retryPendingButton);

        m_excludeButton = new Button{Base::uiText("排除应用…"), this};
        m_excludeButton->setObjectName(QStringLiteral("memoryExclude"));
        m_excludeButton->setToolTip(m_excludeButton->text());
        m_excludeButton->setAccessibleName(m_excludeButton->text());
        m_icons.applyIcon(*m_excludeButton, "Block");
        footer->addWidget(m_excludeButton);

        m_clearButton = new Button{Base::uiText("清空索引"), this};
        m_clearButton->setObjectName(QStringLiteral("memoryClear"));
        m_clearButton->setToolTip(m_clearButton->text());
        m_clearButton->setAccessibleName(m_clearButton->text());
        m_icons.applyIcon(*m_clearButton, "Eraser");
        footer->addWidget(m_clearButton);

        m_searchTimer = new QTimer{this};
        m_searchTimer->setSingleShot(true);
        m_searchTimer->setInterval(kMemorySearchDebounceMs);
        QObject::connect(m_searchTimer, &QTimer::timeout, this, &MemoryView::refresh);

        QObject::connect(m_search, &QLineEdit::textChanged, m_searchTimer, qOverload<>(&QTimer::start));
        QObject::connect(m_appFilter, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        QObject::connect(m_timeFilter, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        QObject::connect(m_mergeToggle, &QCheckBox::toggled, this, [this] { refresh(); });
        QObject::connect(m_importButton, &QPushButton::clicked, this, &MemoryView::importRequested);
        QObject::connect(m_enableButton, &QPushButton::clicked, this, &MemoryView::enableRequested);
        QObject::connect(m_rebuildButton, &QPushButton::clicked, this, &MemoryView::rebuildRequested);
        QObject::connect(m_retryPendingButton, &QPushButton::clicked, this, &MemoryView::retryPendingRequested);
        QObject::connect(m_excludeButton, &QPushButton::clicked, this, &MemoryView::openExcludeDialog);
        QObject::connect(m_clearButton, &QPushButton::clicked, this, &MemoryView::requestClearIndex);

        QObject::connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *, QListWidgetItem *) { updateActionState(); });
        QObject::connect(m_list, &QListWidget::itemClicked, this,
                         [this](QListWidgetItem *item)
                         {
                             const int groupIndex = item->data(kGroupRole).toInt();
                             // 组头不是结果行：点它只切换这一组的展开状态，不改变选择
                             if (groupIndex >= 0 && groupIndex < static_cast<int>(m_groupExpanded.size()))
                             {
                                 m_groupExpanded[static_cast<std::size_t>(groupIndex)] = !m_groupExpanded[static_cast<std::size_t>(groupIndex)];
                                 refillList();
                             }
                         });

        QObject::connect(m_pinButton, &QPushButton::clicked, this,
                         [this]
                         {
                             if (const auto path = selectedPath(); path && !selectedFileMissing())
                             {
                                 emit pinRequested(*path);
                             }
                         });
        QObject::connect(m_openFileButton, &QPushButton::clicked, this,
                         [this]
                         {
                             if (const auto path = selectedPath(); path && !selectedFileMissing())
                             {
                                 emit openFileRequested(*path);
                             }
                         });
        QObject::connect(m_openFolderButton, &QPushButton::clicked, this,
                         [this]
                         {
                             if (const auto path = selectedPath(); path)
                             {
                                 emit openFolderRequested(*path);
                             }
                         });
        QObject::connect(m_retryButton, &QPushButton::clicked, this,
                         [this]
                         {
                             if (const auto id = selectedId(); id > 0)
                             {
                                 emit retryOcrRequested(id);
                             }
                         });
        QObject::connect(m_removeButton, &QPushButton::clicked, this,
                         [this]
                         {
                             if (const auto id = selectedId(); id > 0)
                             {
                                 emit removeIndexRequested(id);
                             }
                         });

        // 缩略图逐张解：0ms 单次定时器每响一次挑一条交给工作线程，列表先摆出来，图随后一张张换上去
        m_thumbnailWatcher = new QFutureWatcher<DecodedThumbnail>{this};
        QObject::connect(m_thumbnailWatcher, &QFutureWatcher<DecodedThumbnail>::finished, this, &MemoryView::applyDecodedThumbnail);
        m_thumbnailTimer = new QTimer{this};
        m_thumbnailTimer->setSingleShot(true);
        m_thumbnailTimer->setInterval(0);
        QObject::connect(m_thumbnailTimer, &QTimer::timeout, this, &MemoryView::startNextThumbnail);

        updateActionState();
        updateBanner();
        updateRetryPendingState();
        updateEmptyHint();
    }

    void MemoryView::refresh()
    {
        rebuildAppFilter();
        auto results = m_index.search(currentQuery());

        if (!results)
        {
            // 库这次没读出来不能把列表清空了事：留着上一次的行，用户还能顺着路径找文件。
            // 错误也进数据库横幅：只弹气泡的话，库打不开时用户找不到重建入口
            emit messageForUser(QString::fromUtf8(results.error().message.c_str()), Gui::ToastKind::Danger);
            refreshStats();
            setRuntimeDatabaseError(QString::fromUtf8(results.error().message.c_str()));
            return;
        }

        // 这次读得出来，之前记下的查询错误就已经过期：横幅与重建入口随之收起
        setRuntimeDatabaseError(QString{});

        m_groups.clear();
        if (m_mergeToggle->isChecked())
        {
            m_groups = Core::groupDuplicates(std::move(*results));
        } else
        {
            // 关闭合并时每一条自成一组：列表面貌与分组路径只剩一处差别
            m_groups.reserve(results->size());
            for (auto &result: *results)
            {
                m_groups.push_back({std::move(result)});
            }
        }
        m_resultCount = 0;
        for (const auto &group: m_groups)
        {
            m_resultCount += static_cast<int>(group.size());
        }
        m_groupExpanded.assign(m_groups.size(), false);
        refillList();
        refreshStats();
    }

    void MemoryView::setMemoryEnabled(const bool enabled)
    {
        m_enabled = enabled;
        updateBanner();
        updateRetryPendingState();
        updateEmptyHint();
    }

    void MemoryView::setOcrAvailability(const bool available, const QString &reason)
    {
        m_ocrAvailable = available;
        m_ocrReason    = reason;
        updateBanner();
        updateRetryPendingState();
    }

    void MemoryView::setDatabaseError(const QString &reason)
    {
        m_externalDatabaseError = reason;
        if (reason.isEmpty())
        {
            // 协调器说库恢复了：本页此前记下的查询失败也已过期，不能留着旧原因继续亮重建按钮
            m_runtimeDatabaseError.clear();
        }
        updateBanner();
        updateEmptyHint();
    }

    void MemoryView::setRuntimeDatabaseError(const QString &reason)
    {
        if (m_runtimeDatabaseError == reason)
        {
            return;
        }

        m_runtimeDatabaseError = reason;
        updateBanner();
        updateEmptyHint();
    }

    QString MemoryView::databaseErrorText() const
    {
        return m_externalDatabaseError.isEmpty() ? m_runtimeDatabaseError : m_externalDatabaseError;
    }

    void MemoryView::setImportProgress(const Core::MemoryImportProgress &progress)
    {
        if (m_importDialog == nullptr)
        {
            m_importDialog = new ImportProgressDialog{this};
            QObject::connect(m_importDialog, &ImportProgressDialog::cancelRequested, this, &MemoryView::importCancelRequested);
        }
        // 待识别条数从统计现取：进度结构里没有这一格，而页脚用的同一个索引查询
        if (const auto stats = m_index.stats(static_cast<int>(m_excludedProcesses.size())); stats)
        {
            m_importDialog->setPendingOcr(stats->pendingOcr);
        } else
        {
            m_importDialog->setPendingOcr(-1);
        }
        m_importDialog->setProgress(progress);
        if (!m_importDialog->isVisible())
        {
            m_importDialog->show();
        }
    }

    void MemoryView::hideImportProgress()
    {
        if (m_importDialog != nullptr)
        {
            // 关闭而不是销毁：下一次导入复用同一个对话框，进度读数从新的一轮重新写
            m_importDialog->close();
        }
    }

    void MemoryView::setExcludedProcesses(QStringList processes)
    {
        m_excludedProcesses = std::move(processes);
        refreshStats();
    }

    int MemoryView::rowCount() const
    {
        return m_list == nullptr ? 0 : m_list->count();
    }

    QString MemoryView::firstCaption() const
    {
        const auto *item = m_list->item(0);
        return item == nullptr ? QString{} : item->text();
    }

    bool MemoryView::eventFilter(QObject *watched, QEvent *event)
    {
        if (watched == m_list->viewport() && event->type() == QEvent::Resize)
        {
            placeEmptyHint();
        }
        return QWidget::eventFilter(watched, event);
    }

    void MemoryView::showEvent(QShowEvent *event)
    {
        m_thumbnailSuspended = false;
        QWidget::showEvent(event);
        scheduleThumbnails();
    }

    void MemoryView::hideEvent(QHideEvent *event)
    {
        // 定时器停了队列还留着：在飞的那张回来后会退回队首，下次显示接着解
        m_thumbnailTimer->stop();
        m_thumbnailSuspended = true;
        QWidget::hideEvent(event);
    }

    Core::MemoryQuery MemoryView::currentQuery() const
    {
        Core::MemoryQuery query;
        query.text        = m_search->text();
        query.processName = m_appFilter->currentData().toString();
        query.limit       = Core::kMemorySearchLimit;

        const int days = m_timeFilter->currentData().toInt();
        if (days > 0)
        {
            // 今天、7 天、30 天都从当天零点往前数：用户在「今天」里找的是今天这一整天
            query.from = QDateTime{QDate::currentDate().addDays(-(days - 1)), QTime{0, 0}};
        }
        return query;
    }

    void MemoryView::rebuildAppFilter()
    {
        const QString previous = m_appFilter->currentData().toString();

        // 重填期间静音 currentIndexChanged：清空与恢复选择各会改一次下标，不该各查一遍库
        const QSignalBlocker blocker{m_appFilter};
        m_appFilter->clear();
        m_appFilter->addItem(Base::uiText("全部应用"), QString{});

        if (const auto apps = m_index.apps(); apps)
        {
            for (const auto &app: *apps)
            {
                if (app.processName.isEmpty())
                {
                    m_appFilter->addItem(Base::uiText("未知应用"), unknownProcessFilter());
                } else
                {
                    m_appFilter->addItem(app.processName, app.processName);
                }
            }
        }

        const int restored = m_appFilter->findData(previous);
        m_appFilter->setCurrentIndex(restored >= 0 ? restored : 0);
    }

    void MemoryView::refreshStats()
    {
        const auto stats = m_index.stats(static_cast<int>(m_excludedProcesses.size()));
        if (!stats)
        {
            m_stats->setText(Base::uiText("统计信息暂时无法读取"));
            setRuntimeDatabaseError(QString::fromUtf8(stats.error().message.c_str()));
            return;
        }

        m_totalItems      = stats->totalItems;
        m_pendingOcr      = stats->pendingOcr;
        QString statsText = Base::uiText("共 %1 条 · 待识别 %2 · 失败 %3 · 数据库 %4 · 排除应用 %5 个")
                                    .arg(stats->totalItems)
                                    .arg(stats->pendingOcr)
                                    .arg(stats->failedOcr)
                                    .arg(QLocale{}.formattedDataSize(stats->databaseBytes))
                                    .arg(stats->excludedProcessCount);
        // 搜索固定只回一页；顶到上限时明说，用户才知道更早的结果要靠收窄条件翻出来
        if (m_resultCount >= Core::kMemorySearchLimit)
        {
            statsText += Base::uiText(" · 只显示前 %1 条，收窄关键词可查看更早的结果").arg(Core::kMemorySearchLimit);
        }
        m_stats->setText(statsText);
        updateRetryPendingState();
    }

    void MemoryView::updateBanner()
    {
        QStringList   reasons;
        const QString databaseError = databaseErrorText();
        if (!databaseError.isEmpty())
        {
            reasons.append(Base::uiText("记忆库暂时不可用：") + databaseError);
        }
        if (!m_enabled)
        {
            reasons.append(Base::uiText("记忆库尚未开启。开启后仅索引手动截图，数据仅保存在本机，可随时关闭并清空。"));
        }
        if (!m_ocrAvailable && m_enabled)
        {
            const QString reason = m_ocrReason.isEmpty() ? Base::uiText("未检测到可用的识别引擎") : m_ocrReason;
            reasons.append(Base::uiText("文字识别暂不可用：") + reason + Base::uiText("。截图仍会存入记忆库，可稍后重新识别。"));
        }

        m_banner->setText(reasons.join(QChar{u'\n'}));
        m_enableButton->setVisible(!m_enabled);
        m_rebuildButton->setVisible(!databaseError.isEmpty());
        m_bannerFrame->setVisible(!reasons.isEmpty());
    }

    void MemoryView::updateRetryPendingState()
    {
        // 后端不可用时给恢复入口，有积压时给批量重试；功能关着时重试不会执行，入口一并收起
        const bool needed = m_enabled && (!m_ocrAvailable || m_pendingOcr > 0);
        m_retryPendingButton->setVisible(needed);
        m_retryPendingButton->setEnabled(needed);
    }

    void MemoryView::updateActionState()
    {
        const bool hasResult = hasSelectedResult();
        const bool missing   = hasResult && selectedFileMissing();

        // 文件已不在时贴图与打开都没有落点：按钮灰掉比点了没反应更像一句说明
        m_pinButton->setEnabled(hasResult && !missing);
        m_openFileButton->setEnabled(hasResult && !missing);
        m_openFolderButton->setEnabled(hasResult);
        m_retryButton->setEnabled(hasResult);
        m_removeButton->setEnabled(hasResult);
    }

    void MemoryView::refillList()
    {
        // 整批重建：旧下标全部失效，未解的那批也一并作废。代际号让在飞任务的结果落地时作废
        ++m_thumbnailGeneration;
        m_thumbnailTimer->stop();
        m_pendingThumbnails.clear();
        m_list->clear();

        for (std::size_t groupIndex = 0; groupIndex < m_groups.size(); ++groupIndex)
        {
            const auto &group = m_groups[groupIndex];
            if (group.size() == 1)
            {
                appendCard(group.front());
                continue;
            }

            appendGroupHeader(static_cast<int>(groupIndex), group);
            if (!m_groupExpanded[groupIndex])
            {
                // 收着的组也要露出最新一条：组头只说明还有几张，贴图与打开的动作都长在卡片上
                appendCard(group.front());
                continue;
            }
            for (const auto &result: group)
            {
                appendCard(result);
            }
        }

        updateActionState();
        updateEmptyHint();
        if (m_list->count() == 0)
        {
            placeEmptyHint();
            m_emptyHint->raise();
        }
        scheduleThumbnails();
    }

    void MemoryView::appendCard(const MemoryResult &result)
    {
        const auto displayPath = Base::Paths::toDisplayText(result.filePath);
        auto      *item        = new QListWidgetItem{cardText(result)};
        item->setData(kIdRole, result.id);
        item->setData(kPathRole, displayPath);
        item->setData(kMissingRole, result.fileMissing);
        item->setData(kGroupRole, -1);
        item->setToolTip(displayPath);
        m_list->addItem(item);

        // 先查缓存再谈占位图：命中时省下那一次图标查找，未命中才需要一枚先顶上
        const auto key = thumbnailCacheKey(displayPath);
        if (const auto *cached = m_thumbnailCache.object(key); cached != nullptr)
        {
            item->setIcon(*cached);
            return;
        }

        item->setIcon(fallbackIcon());
        m_pendingThumbnails.append(PendingThumbnail{
                .index       = QPersistentModelIndex{m_list->model()->index(m_list->count() - 1, 0)},
                .displayPath = displayPath,
                .cacheKey    = key,
        });
    }

    void MemoryView::appendGroupHeader(const int groupIndex, const std::vector<MemoryResult> &group)
    {
        auto *item = new QListWidgetItem{groupText(static_cast<int>(group.size()), group.front().capturedAt)};
        item->setData(kGroupRole, groupIndex);
        // 组头只承接点击展开，不参与选中：选中态留给真正能贴图的结果行
        item->setFlags(Qt::ItemIsEnabled);
        item->setToolTip(Base::uiText("点击展开或收起该组重复或相似截图。"));
        auto font = item->font();
        // DemiBold 是这一套界面的层级字重：整行 Bold 在中文里会糊成一团，也会显得和旁边卡片不是一套字体
        font.setWeight(QFont::DemiBold);
        item->setFont(font);
        m_list->addItem(item);
    }

    void MemoryView::placeEmptyHint()
    {
        const QRect area = m_list->viewport()->rect();
        m_emptyHint->setGeometry(area.adjusted(kSpacingLarge, kSpacingLarge, -kSpacingLarge, -kSpacingLarge));
    }

    void MemoryView::updateEmptyHint()
    {
        QString       text;
        const QString databaseError = databaseErrorText();
        if (!databaseError.isEmpty())
        {
            text = Base::uiText("记忆库暂时无法读取。请点击上方的「重建索引」，原图不会被删除。");
        } else if (!m_enabled)
        {
            text = Base::uiText("开启记忆后，此处会列出手动截图。");
        } else if (m_groups.empty())
        {
            const bool filtered = !m_search->text().trimmed().isEmpty() || !m_appFilter->currentData().toString().isEmpty() || m_timeFilter->currentData().toInt() > 0;
            text = filtered ? Base::uiText("没有搜到匹配的截图。请更换关键词，或放宽应用与时间范围。") : Base::uiText("还没有索引。首次截图后会自动建立索引，也可以导入旧截图。");
        }

        m_emptyHint->setText(text);
        m_emptyHint->setVisible(m_list->count() == 0 && !text.isEmpty());
    }

    bool MemoryView::hasSelectedResult() const
    {
        const auto *item = m_list->currentItem();
        return item != nullptr && item->data(kGroupRole).toInt() < 0;
    }

    std::optional<std::filesystem::path> MemoryView::selectedPath() const
    {
        if (!hasSelectedResult())
        {
            return {};
        }
        const auto text = m_list->currentItem()->data(kPathRole).toString();
        if (text.isEmpty())
        {
            return {};
        }
        return Base::Paths::fromDisplayText(text);
    }

    qint64 MemoryView::selectedId() const
    {
        if (!hasSelectedResult())
        {
            return 0;
        }
        return m_list->currentItem()->data(kIdRole).toLongLong();
    }

    bool MemoryView::selectedFileMissing() const
    {
        return hasSelectedResult() && m_list->currentItem()->data(kMissingRole).toBool();
    }

    QIcon MemoryView::fallbackIcon() const
    {
        return m_icons.icon("CaptureRegion");
    }

    void MemoryView::scheduleThumbnails()
    {
        if (!isVisible() || m_thumbnailSuspended || m_pendingThumbnails.isEmpty() || m_inFlightThumbnail.has_value() || m_thumbnailTimer->isActive())
        {
            return;
        }
        m_thumbnailTimer->start();
    }

    void MemoryView::startNextThumbnail()
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
            pending.cacheKey    = thumbnailCacheKey(pending.displayPath);
            pending.generation  = m_thumbnailGeneration;
            m_inFlightThumbnail = pending;

            const auto displayPath = pending.displayPath;
            m_thumbnailWatcher->setFuture(QtConcurrent::run([displayPath] { return decodeThumbnailPixels(displayPath); }));
            return;
        }

        // 队列里只剩失效条目：这里没活可干，交给调度收尾，不再空转定时器
        scheduleThumbnails();
    }

    void MemoryView::applyDecodedThumbnail()
    {
        // 结果只由 finished 触发，正常顺序下一定有一条在飞条目；这里没有就无处可落
        if (!m_inFlightThumbnail.has_value())
        {
            return;
        }
        auto pending = std::move(*m_inFlightThumbnail);
        m_inFlightThumbnail.reset();

        const auto decoded = m_thumbnailWatcher->result();

        // 列表在解码期间重建过：结果对应的已是另一批行，不写缓存
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
            icon = fallbackIcon();
        }

        item->setIcon(icon);
        m_thumbnailCache.insert(pending.cacheKey, new QIcon{icon}, 1);
        scheduleThumbnails();
    }

    void MemoryView::openExcludeDialog()
    {
        const auto apps = m_index.apps();
        if (!apps)
        {
            emit messageForUser(QString::fromUtf8(apps.error().message.c_str()), Gui::ToastKind::Danger);
            return;
        }

        MemoryExcludeDialog dialog{*apps, m_excludedProcesses, this};
        if (dialog.exec() != QDialog::Accepted)
        {
            return;
        }
        m_excludedProcesses = dialog.selectedProcesses();
        emit excludeProcessesChanged(m_excludedProcesses);
        refresh();
    }

    void MemoryView::requestClearIndex()
    {
        if (m_totalItems <= 0)
        {
            emit messageForUser(Base::uiText("记忆库当前为空"), Gui::ToastKind::Information);
            return;
        }

        // 清空是一下去掉整批：确认框写明条数，原图与历史记录不在删除范围内
        const auto answer = QMessageBox::question(this, Base::uiText("清空记忆索引"), Base::uiText("确定删除全部 %1 条索引吗？原图与历史记录不会被删除。").arg(m_totalItems),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes)
        {
            emit clearIndexRequested();
        }
    }

} // namespace GyanisScribe::Gui
