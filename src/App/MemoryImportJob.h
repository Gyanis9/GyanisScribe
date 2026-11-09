/**
 * @file MemoryImportJob.h
 * @brief 把一个文件夹里的旧截图分批写进记忆索引
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Memory/MemoryTypes.h"

#include <QObject>
#include <QString>

#include <filesystem>
#include <memory>

class QDirIterator;

namespace GyanisScribe::Core
{
    class MemoryIndex;
} // namespace GyanisScribe::Core

namespace GyanisScribe::App
{
    /**
     * @brief 一次旧图导入任务
     * @details 递归扫描文件夹里的图片，逐条写进索引；扫描与入库在界面线程上分块跑，
     *          每块之间让出事件循环，进度对话框才能画面。不复制文件，也不改历史清单。
     *          同一时间只跑一个任务；取消后不再调度新文件，已入库的行保留。
     */
    class MemoryImportJob : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 绑定一个索引
         * @param index 目标索引；本类不拥有它，它必须活到本对象析构之后
         * @param parent Qt 父对象，可空
         */
        explicit MemoryImportJob(Core::MemoryIndex &index, QObject *parent = nullptr);

        /// 析构：停止后续分块，不再发信号
        ~MemoryImportJob() override;

        MemoryImportJob(const MemoryImportJob &)            = delete;
        MemoryImportJob &operator=(const MemoryImportJob &) = delete;

        /**
         * @brief 开始扫描并导入
         * @param directory 要扫描的文件夹；不存在或不是文件夹时以零读数结束
         * @details 任务跑起来后同一对象不再接受第二次 start()。
         */
        void start(const std::filesystem::path &directory);

        /// 请求取消：下一块开始前停下，已入库的行保留
        void cancel();

    signals:
        /**
         * @brief 扫描与入库进度
         * @param progress 已扫描、已入库、跳过、失败与取消状态
         * @note 每 25 个文件发一次，结束时再发一次最终读数
         */
        void progress(const GyanisScribe::Core::MemoryImportProgress &progress);

        /**
         * @brief 一条旧图已写进索引
         * @param id 该条目在索引里的主键
         * @details 协调器据此把它排进导入 OCR 队列。
         */
        void inserted(qint64 id);

        /**
         * @brief 本次导入结束
         * @param report 扫描、入库、跳过、失败与取消的合计读数
         */
        void finished(const GyanisScribe::Core::MemoryImportReport &report);

    private:
        /// 处理一块文件；处理后让出事件循环再排下一块
        void processChunk();

        /// 收尾：发最终进度与结束报告，之后不再处理文件
        void finish();

        /// 处理一个候选文件；不合格的只计数，不进库
        void processFile(const QString &filePath);

        Core::MemoryIndex            &m_index;                 ///< 目标索引，不拥有
        std::unique_ptr<QDirIterator> m_iterator;              ///< 递归扫描器
        Core::MemoryImportProgress    m_progress;              ///< 累计读数
        int                           m_sinceProgress = 0;     ///< 距离上次发进度又扫了几个
        bool                          m_running       = false; ///< 是否还有后续分块
        bool                          m_finished      = false; ///< 是否已经发过结束报告
        bool                          m_cancelled     = false; ///< 用户是否已取消
    };
} // namespace GyanisScribe::App
