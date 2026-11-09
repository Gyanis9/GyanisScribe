/**
 * @file MemoryCoordinator.h
 * @brief 截图记忆的开关、首次授权、后台识别与数据库维护
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Capture/SourceInfo.h"
#include "Core/Memory/MemoryIndex.h"
#include "Core/Memory/MemoryTypes.h"
#include "Gui/Controls/Toast.h"

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace GyanisScribe::Core
{
    class IOcrEngine;
    class ISettingsStore;
    class RecognitionRunner;
    struct OcrResult;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::App
{

    class MemoryImportJob;

    /**
     * @brief 截图记忆这一条链的协作对象
     * @details 管首次授权、开关、OCR 队列、数据库维护与统计；识别在后台线程，索引写入与设置读写都在界面线程。
     *          OCR 文本、窗口标题与进程名只写进本地数据库，不写日志，也不经本类的信号带出。
     */
    class MemoryCoordinator : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 生产构造：按设置与平台能力选一个识别后端
         * @param settings 配置来源，读开关、授权与 OCR 语言
         * @param integration 平台能力来源，后端选型用
         * @param databasePath 数据库文件路径；第一次 start() 时打开
         * @param maxEntries 条目容量上限，生产用默认值；用例可改小
         * @param parent Qt 父对象
         */
        MemoryCoordinator(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Platform::IPlatformIntegration &integration, std::filesystem::path databasePath,
                          int maxEntries = Core::kMaxMemoryEntries, QObject *parent = nullptr);

        /**
         * @brief 测试注入构造：跳过后端工厂，直接接管一个引擎
         * @param settings 配置来源
         * @param databasePath 数据库文件路径
         * @param engine 识别后端，所有权移入本对象；为空时队列按「后端不可用」停住
         * @param maxEntries 条目容量上限，生产用默认值；用例可改小
         * @param parent Qt 父对象
         */
        MemoryCoordinator(GyanisScribe::Core::ISettingsStore &settings, std::filesystem::path databasePath, std::unique_ptr<GyanisScribe::Core::IOcrEngine> engine,
                          int maxEntries = Core::kMaxMemoryEntries, QObject *parent = nullptr);

        /**
         * @brief 析构：先断开读图结果的回接，再等读图与识别收口
         * @details 识别后端被搬运工按引用使用，搬运工必须先于后端析构；析构体里等待的是读图那一侧的结果。
         */
        ~MemoryCoordinator() override;

        /**
         * @brief 打开数据库、清理失效行并恢复待识别条目
         * @details 只生效一次；库打不开时发 databaseError 与一条中文提示，后续截图不再建索引。
         */
        void start();

        /**
         * @brief 本对象使用的索引
         * @return 数据库索引；重建索引后仍是同一个对象，引用一直有效
         */
        [[nodiscard]] Core::MemoryIndex &index();

        /// 开关是否打开；打开即视为已同意首次授权
        [[nodiscard]] bool isEnabled() const;

        /// 是否还需要询问首次授权
        [[nodiscard]] bool needsConsent() const;

        /**
         * @brief 回答首次授权
         * @param enabled true 表示开启并索引等待中的那一条；false 表示暂不并丢弃它
         * @details 两种情况都会写下 ConsentAsked；开启时同时写下 Enabled。
         */
        void resolveConsent(bool enabled);

        /**
         * @brief 改开关
         * @param enabled true 时补写 ConsentAsked 并重试待识别行；false 时停队列但保留数据库
         * @details 关闭只停新条目的建立，已有索引仍可搜索，重新打开后可恢复处理待识别行。
         */
        void setEnabled(bool enabled);

        /**
         * @brief 记一张刚完成的截图
         * @param filePath 成品文件路径
         * @param size 像素尺寸
         * @param source 截图时的前台来源，进程名在这里归一
         * @details 未开启且未询问过授权时只攒一条等待用户回答；已开启时先写入待识别行，再排进截图队列。
         *          进程在排除名单里或数据库未打开时丢弃，并给一次中文说明。
         */
        void noteScreenshot(const std::filesystem::path &filePath, const QSize &size, const Core::CaptureSourceInfo &source);

        /**
         * @brief 把一个文件夹里的旧截图导入记忆库
         * @param directory 要扫描的文件夹
         * @details 未开启时只请求首次授权并返回，用户回答后可从记忆库页再次发起；
         *          同一时间只允许一个导入任务。扫描与入库分块进行，OCR 仍走本类的队列。
         */
        void importFolder(const std::filesystem::path &directory);

        /// 取消正在跑的导入；没有导入时是空操作
        void cancelImport();

        /**
         * @brief 改排除的应用名单
         * @param processes 进程名列表；保存前归一为小写、去掉 .exe 并去重
         * @details 新加入的进程会删掉已有索引行，之后同一进程的新截图不再入库；
         *          空进程名的导入行不受影响。之后不再排除的进程只影响新截图，旧行不回填。
         */
        void setExcludedProcesses(QStringList processes);

        /// 当前排除的应用名单，已归一化
        [[nodiscard]] QStringList excludedProcesses() const;

        /**
         * @brief 重新识别一条
         * @param id 条目主键
         * @details 状态重置为待识别并排到同类队列的队首；当前正在识别的同一条不重复排。
         */
        void retryOcr(qint64 id);

        /**
         * @brief 把库中全部待识别行重新入队
         * @details 启动、重新开启或后端恢复后调用；已在识别中的条目不会重复入队。
         */
        void retryPending();

        /// 按路径删一条索引，原文件保留
        void removePath(const std::filesystem::path &filePath);

        /// 删全部截图来源的索引，导入来源保留，原文件都保留
        void clearCaptureEntries();

        /// 删全部索引并回收空间，原文件保留
        void clearAll();

        /// 删除原图已不在磁盘上的索引行；有删除时发一次提示
        void pruneMissing();

        /**
         * @brief 重建索引：把旧库改名留档后新建空库
         * @details 索引对象本身不替换，只在原地关闭再打开，重建前后拿到的引用始终有效。
         *          旧库及其 -wal、-shm 改名为 memory-index.corrupt-<时间戳>.sqlite 及其侧车，
         *          不静默删除；改名或新建失败时发 databaseError，旧文件保持可找回。
         */
        void rebuildIndex();

    signals:
        /// 需要询问首次授权；同一时间只攒一条等待中的截图
        void consentRequested();

        /// 索引内容变了，界面据此重读
        void changed();

        /**
         * @brief 页脚统计更新
         * @param stats 条数、待识别数、失败数与数据库字节数
         */
        void statsChanged(const GyanisScribe::Core::MemoryStats &stats);

        /**
         * @brief 导入进度
         * @param progress 已扫描、已入库、跳过、失败与取消状态
         */
        void importProgress(const GyanisScribe::Core::MemoryImportProgress &progress);

        /**
         * @brief 导入结束
         * @param report 本次导入的合计读数
         */
        void importFinished(const GyanisScribe::Core::MemoryImportReport &report);

        /**
         * @brief 识别后端可用性变了
         * @param available 是否可用
         * @param reason 不可用时的中文原因与替代做法；可用时为空
         */
        void ocrAvailabilityChanged(bool available, const QString &reason);

        /**
         * @brief 数据库打不开或维护失败
         * @param message 中文原因与下一步
         */
        void databaseError(const QString &message);

        /// 要说给用户的一句话
        void messageForUser(const QString &message, GyanisScribe::Gui::ToastKind kind);

    private:
        /// 一条等待识别的任务
        struct QueueItem
        {
            qint64                id = 0;                               ///< 数据库主键
            std::filesystem::path filePath;                             ///< 原图路径
            QSize                 size;                                 ///< 像素尺寸
            Core::MemorySource    source = Core::MemorySource::Capture; ///< 截图还是导入
            QString               processName;                          ///< 归一后的进程名，导入为空
        };

        /// 工作线程读图与算指纹后的产物
        struct PreparedImage
        {
            QImage  image;            ///< 读出的图像，读失败时为空
            QString dhash;            ///< 64 位感知哈希的十六进制串
            bool    readable = false; ///< 文件是否成功读出
        };

        /// 建后端搬运工并接上读图与识别的结果信号
        void initialize();

        /// 确保数据库已打开；失败时只报一次
        [[nodiscard]] bool ensureIndexOpen();

        /// 发数据库错误与一条中文提示；同一次失败只报一次
        void reportDatabaseFailure(const Base::Error &error);

        /// 把一条元数据写入待识别行并排进截图队列
        void storeEntry(const Core::MemoryUpsert &entry);

        /// 导入任务入库一条后把它排进导入队列
        void enqueueImported(qint64 id);

        /// 超出容量时淘汰旧条目；导入期间只累计，截图路径立即提示
        void enforceCapacity(qint64 insertedId, bool duringImport);

        /// 把已排除进程的排队任务与等待授权的截图清掉
        void dropExcludedWork(const QStringList &excluded);

        /// 取下一个任务；截图队列优先，同类先进先出
        void startNextOcr();

        /// 读图与指纹已回到界面线程：交识别或记失败
        void handlePrepared();

        /// 识别成功：写回文本与指纹
        void handleOcrSucceeded(const Core::OcrResult &result);

        /// 识别失败：写回失败状态与原因
        void handleOcrFailed(const Base::Error &error);

        /// 写回一次成功的识别结果
        void writeOcr(qint64 id, QString text, QString dhash, QString status, QString error, bool truncated);

        /// 只写失败状态与原因，保留上一次成功的文本与指纹
        void writeOcrFailure(qint64 id, QString error);

        /// 收尾一条任务：清运行位、发变更与统计、继续下一条
        void finishTask();

        /// 清空内存队列与运行位；识别中的结果回来后按已失效丢弃
        void stopQueue();

        /// 从两条队列与运行位里去掉同一路径的任务
        void removeQueuedByPath(const QString &pathKey);

        /// 从两条队列、运行位与主键映射里去掉同一条任务
        void removeQueuedById(qint64 id);

        /// 找一个任务的内存元数据；队列里没有时按最近搜索结果回查
        [[nodiscard]] std::optional<QueueItem> findQueueItem(qint64 id) const;

        /// 检查后端可用性，状态变化时发一次信号
        void refreshOcrAvailability();

        /// 发页脚统计；数据库未打开时不发
        void emitStats();

        /// 进程是否在排除名单里
        [[nodiscard]] bool isExcludedProcess(const QString &processName) const;

        /// 本次识别使用的语言标签
        [[nodiscard]] QString ocrLanguage() const;

        GyanisScribe::Core::ISettingsStore                    &m_settings;                        ///< 配置来源，不拥有
        std::filesystem::path                                  m_databasePath;                    ///< 数据库文件位置
        GyanisScribe::Core::MemoryIndex                        m_index;                           ///< 索引本体；重建时原地关闭再打开，地址不变
        std::unique_ptr<GyanisScribe::Core::IOcrEngine>        m_engine;                          ///< 识别后端；排在搬运工之后析构
        std::unique_ptr<GyanisScribe::Core::RecognitionRunner> m_runner;                          ///< 线程搬运工，拿着后端的引用
        QFutureWatcher<PreparedImage>                          m_prepareWatcher;                  ///< 读图与指纹结果的回接点
        std::deque<QueueItem>                                  m_captureQueue;                    ///< 新截图队列，优先于导入
        std::deque<QueueItem>                                  m_importQueue;                     ///< 导入队列
        std::unordered_map<qint64, QueueItem>                  m_knownItems;                      ///< 主键到路径的映射，重试时用
        std::optional<QueueItem>                               m_running;                         ///< 正在读图或识别的任务
        QString                                                m_runningDhash;                    ///< 正在识别那一张的指纹
        std::optional<GyanisScribe::Core::MemoryUpsert>        m_pendingConsent;                  ///< 等待授权回答的那一条截图
        bool                                                   m_ocrAvailable            = true;  ///< 上一次看到的可用性
        bool                                                   m_runnerBusy              = false; ///< 搬运工手上是否有一条识别
        bool                                                   m_started                 = false; ///< start() 是否已经跑过
        bool                                                   m_databaseFailureReported = false; ///< 本次失败是否已经报过
        bool                                                   m_indexNoticeShown        = false; ///< 索引未打开提示是否已经说过
        bool                                                   m_excludedNoticeShown     = false; ///< 排除提示是否已经说过

        std::unique_ptr<MemoryImportJob> m_importJob;                                    ///< 当前导入任务，同一时间只有一个
        int                              m_capacityLimit      = Core::kMaxMemoryEntries; ///< 条目上限，用例可在构造时改小
        int                              m_importEvictedCount = 0;                       ///< 本次导入累计淘汰的条数，结束时合并提示
    };

} // namespace GyanisScribe::App
