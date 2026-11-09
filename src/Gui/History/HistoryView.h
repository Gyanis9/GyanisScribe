/**
 * @file HistoryView.h
 * @brief 历史页：缩略图列表、搜索与类别筛选，以及「重新贴图 / 重新识别 / 删除」三个出口
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/History/HistoryRules.h"
#include "Core/History/HistoryStore.h"
#include "Core/History/HistoryTypes.h"

#include "Gui/Controls/Toast.h"
#include "Gui/History/ThumbnailPixels.h"

#include <QCache>
#include <QFutureWatcher>
#include <QIcon>
#include <QImage>
#include <QPersistentModelIndex>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <filesystem>
#include <optional>

class QEvent;
class QHideEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QComboBox;
class QShowEvent;
class QTimer;

namespace GyanisScribe::Gui
{
    class IconProvider;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::Gui
{

    /**
     * @brief 一条历史在列表里的文字
     * @param entry 记录
     * @param now 判「今天 / 昨天」用的时刻，用例里由调用方给
     * @return 「今天 14:03 · 截图 · 800×600」这样的单行文本；尺寸读不出时不带最后那一格
     * @details 相对日期放在最前：用户找的是「今天上午那几张」，绝对日期要先心算一次。
     *          尺寸为空是「这一轮没落下帧」这类读不出的现场，硬打 0×0 等于把不知道报成一个数。
     */
    [[nodiscard]] QString entryCaption(const Core::HistoryEntry &entry, const QDateTime &now);

    /**
     * @brief 筛选下拉的项文本，顺序与 kindFilterAt 互指
     * @return 「全部」「截图」「录屏」三项
     */
    [[nodiscard]] QStringList historyFilterLabels();

    /**
     * @brief 筛选下拉的一档换成类别
     * @param index 下拉序号
     * @return 类别；第 0 档表示不限类别，序号越界也按不限处理
     */
    [[nodiscard]] std::optional<Core::HistoryKind> kindFilterAt(int index);

    /**
     * @brief 按搜索词与类别筛出一页记录
     * @param entries 全量记录（时间倒序）
     * @param query 搜索词，空串表示不过滤文本
     * @param kindFilter 类别，空表示不限
     * @return 保持原次序的可见记录
     */
    [[nodiscard]] QVector<Core::HistoryEntry> visibleEntries(const QVector<Core::HistoryEntry> &entries, const QString &query, const std::optional<Core::HistoryKind> &kindFilter);

    /**
     * @brief 历史页控件
     * @details 视图不认贴图也不认识识别：三个出口只发信号，路径交给持有各模块的控制器。
     *          缩略图按路径、修改时间与文件大小缓存，解码逐张交给工作线程，看不见的时候不解。
     */
    class HistoryView : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造历史页，只搭控件不读清单
         * @details 这一页挂在设置窗里且通常不是第一页；构造期读清单会顺带解两百张缩略图，
         *          设置窗还没露面就先卡住。清单交给第一次显示去读。
         * @param store 历史清单，刷新时从它读
         * @param policy 保留策略，读清单时裁剪
         * @param icons 图标供给器，读不到缩略图时的兜底图标由它给
         * @param parent 父控件
         * @note 主题不进构造：列表配色全走 QSS，这里没有需要现取的主题色，多接一个引用就是死字段
         */
        HistoryView(Core::HistoryStore &store, const Core::HistoryRetentionPolicy &policy, Gui::IconProvider &icons, QWidget *parent = nullptr);

        /**
         * @brief 换一份保留策略，下一次 refresh() 按它裁剪
         * @param policy 新的条数与天数上限
         * @details 只记策略不自己重读：设置页改完条数或天数后由主窗口紧接着调 refresh()，
         *          两件事拆开才看得见「策略已换、清单还没重读」这个中间态。
         */
        void setPolicy(const Core::HistoryRetentionPolicy &policy);

        /**
         * @brief 重读清单并按当前条件重填列表
         * @details 显式调用时隐藏着也照常读出条目并摆好行；缩略图要等这一页显示出来才解。
         *          有坏记录时列表照常列出好记录，跳过数量变化才弹一次危险提示。
         */
        void refresh();

        /**
         * @brief 当前列出的条数
         * @return 列表项数量，用例据此判断筛选与刷新有没有生效
         */
        [[nodiscard]] int rowCount() const;

        /**
         * @brief 第一行的文字
         * @return 一条记录文案；列表为空时返回空串
         */
        [[nodiscard]] QString firstCaption() const;

        /**
         * @brief 选中项对应的成品路径
         * @return 路径；没有选中项时返回空
         */
        [[nodiscard]] std::optional<std::filesystem::path> selectedPath() const;

        /**
         * @brief 已经真正落到列表上的缩略图张数
         * @return 结果换上列表才计数；缓存命中与直接摆上的占位图标不计，
         *         隐藏前已发起的那次解码仍会在工作线程上跑完并退回队列，不计，
         *         因代际过期或行已失效而被丢弃的结果也不计
         * @details 「刷新会不会又解一遍」只有这个计数看得见：图标换没换在列表上看不出新旧。
         */
        [[nodiscard]] int thumbnailDecodeCountForTesting() const;

        /**
         * @brief 最近一次落地的缩略图是不是在界面线程之外解出的
         * @return 解码函数运行线程的判断结果；还没有结果落地时为 false
         * @details 读文件、缩放与圆角搬出界面线程这件事在列表上看不见，用例只能靠这枚读数确认。
         */
        [[nodiscard]] bool lastDecodeRanOffGuiThreadForTesting() const noexcept;

    signals:
        /**
         * @brief 要求把这条成品重新贴回屏幕
         * @param filePath 成品文件路径
         */
        void pinRequested(const std::filesystem::path &filePath);

        /**
         * @brief 要求对这条成品做一次文字识别
         * @param filePath 成品文件路径
         */
        void ocrRequested(const std::filesystem::path &filePath);

        /**
         * @brief 要求从历史清单里去掉这条（成品文件保留）
         * @param filePath 成品文件路径
         */
        void removeRequested(const std::filesystem::path &filePath);

        /**
         * @brief 请求在文件管理器里打开这条记录成品所在的那个目录
         * @param filePath 选中的那条记录，目录由它的父级给出
         */
        void openFolderRequested(const std::filesystem::path &filePath);

        /**
         * @brief 请求清空整份历史清单（不动成品文件）
         */
        void clearAllRequested();

        /**
         * @brief 一条要落进气泡的提示
         * @param message 中文正文
         * @param kind 语义种类
         */
        void messageForUser(const QString &message, Gui::ToastKind kind);

    protected:
        /// 列表视口改尺寸时把空态提示重新摆到中央
        bool eventFilter(QObject *watched, QEvent *event) override;

        /**
         * @brief 显示时补上首次读清单与隐藏期间的刷新请求
         * @param event 显示事件
         * @details 第一次显示才读清单；隐藏期间来过 refresh() 的，在这里补读一次——
         *          那一次只摆了行，没解缩略图。读清单失败也照样把上次没解完的队列重新排上。
         */
        void showEvent(QShowEvent *event) override;

        /**
         * @brief 隐藏时停掉缩略图解码
         * @param event 隐藏事件
         * @details 看不见的卡片不值得继续解；队列留着，下次显示接着跑。
         */
        void hideEvent(QHideEvent *event) override;

    private:
        /// 一条等着解码的列表项；列表重建后 QPersistentModelIndex 自己失效
        struct PendingThumbnail
        {
            QPersistentModelIndex index;          ///< 目标列表项
            Core::HistoryEntry    entry;          ///< 要解的那条成品
            QString               cacheKey;       ///< 发起解码前取的缓存键
            int                   generation = 0; ///< 发起解码时的代际号
        };

        /// 按当前搜索词与类别重填列表，不动 m_entries
        void refillList();

        /// 把空态提示铺满列表视口的内侧（居中由标签自己的对齐做）
        void placeEmptyHint();

        /// 某条记录读不出缩略图时的类别图标
        [[nodiscard]] QIcon fallbackIcon(const Core::HistoryEntry &entry) const;

        /// 可见、未挂起、队列非空且没有在飞任务时启动一次单次定时器
        void scheduleThumbnails();

        /// 定时器触发后取一条有效条目交给工作线程，队列里没有活时不再空转
        void startNextThumbnail();

        /// 工作线程交回结果：代际与显示状态都合适时组装图标并写缓存
        void applyDecodedThumbnail();

        /// 删除选中项：先去掉记录，再刷新列表
        void removeSelected();

        Core::HistoryStore               &m_store;                          ///< 历史清单
        Core::HistoryRetentionPolicy      m_policy;                         ///< 读清单时用的保留策略
        Gui::IconProvider                &m_icons;                          ///< 图标供给器
        QVector<Core::HistoryEntry>       m_entries;                        ///< 本次读到的全量记录，时间倒序
        QLineEdit                        *m_search           = nullptr;     ///< 搜索框
        QComboBox                        *m_filter           = nullptr;     ///< 类别筛选下拉
        QListWidget                      *m_list             = nullptr;     ///< 缩略图列表
        QLabel                           *m_emptyHint        = nullptr;     ///< 列表为空/筛不出时的引导句
        QTimer                           *m_thumbnailTimer   = nullptr;     ///< 0ms 单次定时器，每响一次启动一张工作线程解码
        QFutureWatcher<DecodedThumbnail> *m_thumbnailWatcher = nullptr;     ///< 工作线程结果的回接点
        QCache<QString, QIcon>            m_thumbnailCache;                 ///< 缩略图缓存，键见 thumbnailCacheKey
        QVector<PendingThumbnail>         m_pendingThumbnails;              ///< 还没解的列表项
        std::optional<PendingThumbnail>   m_inFlightThumbnail;              ///< 正在工作线程上解的那条，结果落地才清
        int                               m_thumbnailDecodeCount   = 0;     ///< 真正落到列表上的张数（缓存命中、隐藏退回、代际/失效丢弃都不计）
        int                               m_thumbnailGeneration    = 0;     ///< 列表重建代际，旧代际的结果作废
        bool                              m_thumbnailSuspended     = false; ///< 隐藏期间挂起，结果回来先退回队列
        bool                              m_lastDecodeOffGuiThread = false; ///< 最近一次落地结果是否在工作线程上解出
        bool                              m_loaded                 = false; ///< 是否已经成功读过一次清单
        bool                              m_refreshWhileHidden     = false; ///< 上次刷新发生在隐藏时，显示后要补读
        int                               m_lastSkippedRecords     = 0;     ///< 上次提示过的坏记录条数，相同数量不再重复弹
    };

} // namespace GyanisScribe::Gui
