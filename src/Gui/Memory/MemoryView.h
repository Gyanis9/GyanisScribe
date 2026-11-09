/**
 * @file MemoryView.h
 * @brief 记忆库页：搜索、应用与时间筛选、重复图合并、结果卡片与页脚统计
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Memory/MemoryIndex.h"
#include "Core/Memory/MemoryTypes.h"
#include "Gui/Controls/Toast.h"
#include "Gui/History/ThumbnailPixels.h"

#include <QCache>
#include <QFutureWatcher>
#include <QIcon>
#include <QPersistentModelIndex>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <filesystem>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QEvent;
class QHideEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QShowEvent;
class QTimer;

namespace GyanisScribe::Gui
{
    class IconProvider;
    class ImportProgressDialog;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::Gui
{
    /// 搜索框防抖时长，毫秒：每次键入都查库会把输入拖成一串卡顿
    inline constexpr int kMemorySearchDebounceMs = 150;

    /**
     * @brief 记忆库页
     * @details 视图只认 Core::MemoryIndex：搜索、应用列表与统计都在界面线程上现查，
     *          贴图、打开、重新识别与移除只发信号，路径与主键交给持有协调器的控制器。
     *          缩略图按路径、修改时间与文件大小缓存，解码逐张交给工作线程。
     */
    class MemoryView : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造记忆库页，只搭控件不查库
         * @param index 记忆索引，刷新与统计都从它读
         * @param icons 图标供给器，按钮与读不出的缩略图由它给图标
         * @param parent 父控件
         * @details 这一页通常不是第一页；构造期查库会顺带解两百张缩略图，窗口还没露面就先卡住。
         */
        MemoryView(Core::MemoryIndex &index, Gui::IconProvider &icons, QWidget *parent = nullptr);

        /**
         * @brief 按当前条件重查并重填列表与统计
         * @details 显式调用时隐藏着也照常查库并摆好行；缩略图要等这一页显示出来才解。
         */
        void refresh();

        /**
         * @brief 记录功能开关并更新顶部横幅
         * @param enabled 记忆库是否已开启
         * @note 关闭只停新条目的建立；已有索引仍可搜索，列表不清空
         * @note 这是功能开关，和 QWidget::setEnabled 的整页禁用不是一回事
         */
        void setMemoryEnabled(bool enabled);

        /**
         * @brief 记录识别后端可用性并更新横幅
         * @param available 是否可用
         * @param reason 不可用时的中文原因与替代做法
         */
        void setOcrAvailability(bool available, const QString &reason);

        /**
         * @brief 记录数据库错误并更新横幅与重建入口
         * @param reason 中文原因；空串表示错误已恢复
         */
        void setDatabaseError(const QString &reason);

        /**
         * @brief 显示或更新导入进度对话框
         * @param progress 已扫描、已入库、跳过、失败与取消状态
         * @details 首次调用时创建并显示对话框；待识别条数从统计现取，读不出时不显示这一行。
         */
        void setImportProgress(const Core::MemoryImportProgress &progress);

        /// 关闭导入进度对话框；没有创建过时是空操作
        void hideImportProgress();

        /**
         * @brief 记录设置里的排除应用名单
         * @param processes 完整名单；排除对话框按它决定初始勾选
         */
        void setExcludedProcesses(QStringList processes);

        /// 当前列出的行数，重复组头也算一行
        [[nodiscard]] int rowCount() const;

        /// 第一行的文字；列表为空时返回空串
        [[nodiscard]] QString firstCaption() const;

    signals:
        /**
         * @brief 要求把这条截图重新贴回屏幕
         * @param filePath 原图路径
         */
        void pinRequested(const std::filesystem::path &filePath);

        /**
         * @brief 要求用系统默认程序打开这条原图
         * @param filePath 原图路径
         */
        void openFileRequested(const std::filesystem::path &filePath);

        /**
         * @brief 要求在文件管理器里打开原图所在目录
         * @param filePath 原图路径，目录由它的父级给出
         */
        void openFolderRequested(const std::filesystem::path &filePath);

        /**
         * @brief 要求重新识别一条
         * @param id 条目主键
         */
        void retryOcrRequested(qint64 id);

        /**
         * @brief 要求从索引里删掉一条（原文件保留）
         * @param id 条目主键
         */
        void removeIndexRequested(qint64 id);

        /// 请求选择文件夹并导入旧截图
        void importRequested();

        /// 请求清空整个索引（原文件保留）
        void clearIndexRequested();

        /**
         * @brief 排除名单已由用户在对话框里确认
         * @param processes 完整名单，未排除的应用不在其中
         */
        void excludeProcessesChanged(QStringList processes);

        /// 用户点了启用引导上的「开启记忆」
        void enableRequested();

        /// 用户点了数据库错误横幅上的「重建索引」
        void rebuildRequested();

        /// 用户在导入进度对话框里点了取消
        void importCancelRequested();

        /**
         * @brief 一条要落进气泡的提示
         * @param message 中文正文
         * @param kind 语义种类
         */
        void messageForUser(const QString &message, Gui::ToastKind kind);

    protected:
        /**
         * @brief 列表视口改尺寸时把空态提示重新摆到中央
         * @param watched 观察对象
         * @param event 事件
         * @return 与基类同解，未处理的事件原样交回
         */
        bool eventFilter(QObject *watched, QEvent *event) override;

        /**
         * @brief 显示时开始解码未完成的缩略图
         * @param event 显示事件
         * @details 隐藏期间只摆行、不解图；这里把队列重新排上。
         */
        void showEvent(QShowEvent *event) override;

        /**
         * @brief 隐藏时停掉缩略图解码
         * @param event 隐藏事件
         * @details 队列留着，下次显示接着跑。
         */
        void hideEvent(QHideEvent *event) override;

    private:
        /// 一条等着解码的列表项；列表重建后 QPersistentModelIndex 自己失效
        struct PendingThumbnail
        {
            QPersistentModelIndex index;          ///< 目标列表项
            QString               displayPath;    ///< 要读的显示路径
            QString               cacheKey;       ///< 发起解码前取的缓存键
            int                   generation = 0; ///< 发起解码时的代际号
        };

        /// 从应用与时间下拉拼出本次查询条件
        [[nodiscard]] Core::MemoryQuery currentQuery() const;

        /// 按 index.apps() 重填应用下拉，尽量保留当前选择
        void rebuildAppFilter();

        /// 按当前排除名单读页脚统计
        void refreshStats();

        /// 按启用、识别可用与数据库错误三路状态重写顶部横幅
        void updateBanner();

        /// 按当前选中行启用或灰掉五枚动作按钮
        void updateActionState();

        /// 把 m_groups 铺进列表：重复组收着时只列组头与最新一条，展开后列出全部成员
        void refillList();

        /// 往列表追加一条结果卡片
        void appendCard(const Core::MemoryResult &result);

        /// 往列表追加一个重复组头
        void appendGroupHeader(int groupIndex, const std::vector<Core::MemoryResult> &group);

        /// 把空态提示铺满列表视口的内侧
        void placeEmptyHint();

        /// 按启用状态与筛选条件选择空态文案
        void updateEmptyHint();

        /// 选中行是否是一张结果卡片
        [[nodiscard]] bool hasSelectedResult() const;

        /// 选中行对应的原图路径；没有选中卡片时返回空
        [[nodiscard]] std::optional<std::filesystem::path> selectedPath() const;

        /// 选中行对应的主键；没有选中卡片时返回 0
        [[nodiscard]] qint64 selectedId() const;

        /// 选中行的原图是否已不在磁盘上
        [[nodiscard]] bool selectedFileMissing() const;

        /// 某条结果读不出缩略图时的兜底图标
        [[nodiscard]] QIcon fallbackIcon() const;

        /// 可见、未挂起、队列非空且没有在飞任务时启动一次单次定时器
        void scheduleThumbnails();

        /// 定时器触发后取一条有效条目交给工作线程
        void startNextThumbnail();

        /// 工作线程交回结果：代际与显示状态都合适时组装图标并写缓存
        void applyDecodedThumbnail();

        /// 打开排除应用对话框，确认后发完整名单
        void openExcludeDialog();

        /// 确认后请求清空索引，确认框里写明总条数
        void requestClearIndex();

        Core::MemoryIndex                           &m_index;               ///< 记忆索引
        Gui::IconProvider                           &m_icons;               ///< 图标供给器
        QStringList                                  m_excludedProcesses;   ///< 设置里的排除应用名单
        std::vector<std::vector<Core::MemoryResult>> m_groups;              ///< 本次结果按重复图分好的组
        std::vector<bool>                            m_groupExpanded;       ///< 每个重复组是否展开
        bool                                         m_enabled      = true; ///< 功能是否已开启
        bool                                         m_ocrAvailable = true; ///< 识别后端是否可用
        QString                                      m_ocrReason;           ///< 识别不可用的中文原因
        QString                                      m_databaseError;       ///< 数据库错误的中文原因，空表示正常
        int                                          m_totalItems  = 0;     ///< 最近一次统计的总条数，清空确认框用
        int                                          m_resultCount = 0;     ///< 最近一次搜索返回的结果条数，判 200 条上限用

        QWidget     *m_bannerFrame      = nullptr; ///< 顶部横幅容器
        QLabel      *m_banner           = nullptr; ///< 横幅正文
        QPushButton *m_enableButton     = nullptr; ///< 横幅上的「开启记忆」
        QPushButton *m_rebuildButton    = nullptr; ///< 横幅上的「重建索引」
        QLineEdit   *m_search           = nullptr; ///< 搜索框
        QComboBox   *m_appFilter        = nullptr; ///< 应用筛选下拉
        QComboBox   *m_timeFilter       = nullptr; ///< 时间筛选下拉
        QCheckBox   *m_mergeToggle      = nullptr; ///< 合并重复图开关
        QPushButton *m_importButton     = nullptr; ///< 导入旧截图
        QPushButton *m_pinButton        = nullptr; ///< 贴图
        QPushButton *m_openFileButton   = nullptr; ///< 打开文件
        QPushButton *m_openFolderButton = nullptr; ///< 打开所在目录
        QPushButton *m_retryButton      = nullptr; ///< 重新识别
        QPushButton *m_removeButton     = nullptr; ///< 从记忆库移除
        QListWidget *m_list             = nullptr; ///< 结果列表
        QLabel      *m_emptyHint        = nullptr; ///< 列表为空或筛不出时的引导句
        QLabel      *m_stats            = nullptr; ///< 页脚统计
        QPushButton *m_excludeButton    = nullptr; ///< 排除应用
        QPushButton *m_clearButton      = nullptr; ///< 清空索引
        QTimer      *m_searchTimer      = nullptr; ///< 搜索防抖定时器

        ImportProgressDialog *m_importDialog = nullptr; ///< 导入进度对话框，首次显示时创建

        QTimer                           *m_thumbnailTimer   = nullptr;  ///< 0ms 单次定时器，每响一次启动一张解码
        QFutureWatcher<DecodedThumbnail> *m_thumbnailWatcher = nullptr;  ///< 工作线程结果的回接点
        QCache<QString, QIcon>            m_thumbnailCache;              ///< 缩略图缓存，键见 thumbnailCacheKey
        QVector<PendingThumbnail>         m_pendingThumbnails;           ///< 还没解的列表项
        std::optional<PendingThumbnail>   m_inFlightThumbnail;           ///< 正在工作线程上解的那条
        int                               m_thumbnailGeneration = 0;     ///< 列表重建代际，旧代际的结果作废
        bool                              m_thumbnailSuspended  = false; ///< 隐藏期间挂起，结果回来先退回队列

        Q_DISABLE_COPY(MemoryView)
    };

} // namespace GyanisScribe::Gui
