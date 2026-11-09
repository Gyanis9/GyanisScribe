/**
 * @file MemoryIndex.h
 * @brief 截图记忆的 SQLite/FTS5 本地索引
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Memory/MemoryTypes.h"

#include <QString>

#include <filesystem>
#include <optional>
#include <vector>

namespace GyanisScribe::Core
{
    /**
     * @brief 记忆条目的本地索引
     * @details 元数据存普通列，可检索文本经中文逐字预处理后进 FTS5 外部内容表；
     *          连接由本对象独占，只能在创建它的线程上使用。
     */
    class MemoryIndex
    {
    public:
        /**
         * @brief 指向一份记忆索引数据库
         * @param databasePath 数据库文件路径；打开时创建所在目录
         */
        explicit MemoryIndex(std::filesystem::path databasePath);

        /**
         * @brief 关闭并移除本对象独占的数据库连接
         */
        ~MemoryIndex();

        MemoryIndex(const MemoryIndex &)            = delete;
        MemoryIndex &operator=(const MemoryIndex &) = delete;

        /**
         * @brief 关闭并移除数据库连接，之后可再次 open()
         * @details 文件改名之前必须先关闭：Windows 上占用中的数据库与 -wal、-shm 不能改名。
         *          关闭后对象仍然有效，open() 会重新建立连接并迁移 schema。
         */
        void close();

        /**
         * @brief 默认数据库位置
         * @return 应用数据目录下的 memory-index.sqlite
         */
        [[nodiscard]] static std::filesystem::path defaultStoragePath();

        /**
         * @brief 取路径的规范化身份
         * @param path 原始文件路径
         * @return 归一化路径的 UTF-8 大小写折叠形式，分隔符统一为斜杠
         */
        [[nodiscard]] static QString pathKey(const std::filesystem::path &path);

        /**
         * @brief 打开数据库、设置参数并迁移到当前 schema 版本
         * @return 成功返回空载荷；失败返回带原因与下一步的中文 Error
         * @note 数据库版本高于本程序支持的版本时返回 PlatformUnsupported，不降级写入
         */
        [[nodiscard]] Base::Expected<void> open();

        /**
         * @brief 数据库是否已经打开
         * @return 打开成功且尚未析构时为 true
         */
        [[nodiscard]] bool isOpen() const noexcept;

        /**
         * @brief 插入或刷新一条待识别条目
         * @param entry 路径、来源、时间、尺寸与窗口元数据
         * @return 该条目在 items 表里的主键；失败返回中文 Error
         * @details 同一 path_key 再次写入会更新元数据并把识别状态重置为 pending，
         *          因为同路径的文件内容可能已被替换，旧识别结果不再可信。
         */
        [[nodiscard]] Base::Expected<qint64> upsertPending(const MemoryUpsert &entry);

        /**
         * @brief 写回一条 OCR 结果并同步可检索文本
         * @param id 条目主键
         * @param text 识别文本，按 UTF-8 字节截到 64 KiB
         * @param dhash 64 位感知哈希的十六进制串，可为空
         * @param status done、failed 或 pending
         * @param error 失败原因，按 UTF-8 字节截到 2 KiB
         * @param truncated 调用方是否已判定文本超限
         * @return 成功返回空载荷；状态非法或 SQL 失败返回中文 Error
         * @note 条目已被删除时按幂等成功处理
         */
        [[nodiscard]] Base::Expected<void> updateOcr(qint64 id, QString text, QString dhash, QString status, QString error, bool truncated);

        /**
         * @brief 只写失败状态与失败原因，不动既有识别结果
         * @param id 条目主键
         * @param error 失败原因，按 UTF-8 字节截到 2 KiB
         * @return 成功返回空载荷；失败返回中文 Error
         * @details 供曾经识别成功、重新识别又失败的行使用：ocr_text、search_text、truncated 与 dhash 原样保留，
         *          旧文本在下次成功识别覆盖之前仍然可搜。条目已被删除时按幂等成功处理。
         */
        [[nodiscard]] Base::Expected<void> markOcrFailed(qint64 id, QString error);

        /**
         * @brief 把条目重置为待识别
         * @param id 条目主键
         * @return 成功返回空载荷；失败返回中文 Error
         * @note 旧识别文本保留到新结果覆盖，期间搜索仍能命中它
         */
        [[nodiscard]] Base::Expected<void> resetOcr(qint64 id);

        /**
         * @brief 按主键读一条索引
         * @param id 条目主键
         * @return 命中返回该条结果；未命中返回空的 optional；SQL 失败返回中文 Error
         * @details 只查一行，不受搜索结果单页条数上限影响。
         */
        [[nodiscard]] Base::Expected<std::optional<MemoryResult>> findById(qint64 id) const;

        /**
         * @brief 按关键词、应用与时间检索
         * @param query 查询条件；关键词超过 256 字符时在绑定前截断
         * @return 按 bm25 或时间倒序排列的结果，条数不超过 query.limit 与 200 中较小者
         */
        [[nodiscard]] Base::Expected<std::vector<MemoryResult>> search(const MemoryQuery &query) const;

        /**
         * @brief 统计每个应用的条目数
         * @return 按条目数倒序的应用列表，条数相同时按进程名升序
         */
        [[nodiscard]] Base::Expected<std::vector<MemoryAppCount>> apps() const;

        /**
         * @brief 读取页脚统计
         * @param excludedProcessCount 设置里排除的应用数，原样带回
         * @return 总条数、待识别数、失败数、数据库文件与 WAL 字节数
         */
        [[nodiscard]] Base::Expected<MemoryStats> stats(int excludedProcessCount) const;

        /**
         * @brief 列出待识别条目
         * @param limit 最多返回条数
         * @return 截图来源优先，其次按加入时间先后
         */
        [[nodiscard]] Base::Expected<std::vector<MemoryResult>> pendingItems(int limit) const;

        /**
         * @brief 按路径删除一条索引
         * @param path 原始文件路径，按 pathKey 匹配
         * @return 成功返回空载荷；条目不存在也算成功
         * @note 只删索引不动原文件
         */
        [[nodiscard]] Base::Expected<void> removeByPath(const std::filesystem::path &path);

        /**
         * @brief 删除全部截图来源的条目
         * @return 成功返回空载荷；没有可删条目也算成功
         * @note 导入来源的条目保留
         */
        [[nodiscard]] Base::Expected<void> removeCaptureEntries();

        /**
         * @brief 删除某个应用的全部条目
         * @param processName 进程名，比较不区分大小写
         * @return 成功返回空载荷；没有可删条目或进程名为空也算成功
         * @note 空进程名代表导入条目，不受排除操作影响
         */
        [[nodiscard]] Base::Expected<void> removeProcess(QString processName);

        /**
         * @brief 删除全部索引并回收空闲页
         * @return 成功返回空载荷；失败返回中文 Error
         */
        [[nodiscard]] Base::Expected<void> clear();

        /**
         * @brief 删除原图已不在磁盘上的条目
         * @return 删除的条目数
         */
        [[nodiscard]] Base::Expected<int> pruneMissing();

        /**
         * @brief 超出容量时淘汰旧条目
         * @param maxEntries 允许保留的最大条数
         * @return 删除的条目数
         * @details 先删最新一行之外最旧的导入条目，导入删完后才删最旧的截图条目。
         */
        [[nodiscard]] Base::Expected<int> enforceCapacity(int maxEntries);

    private:
        std::filesystem::path m_databasePath;   ///< 数据库文件位置
        QString               m_connectionName; ///< 本对象独占的 QSqlDatabase 连接名
        bool                  m_open = false;   ///< 是否已成功打开
    };
} // namespace GyanisScribe::Core
