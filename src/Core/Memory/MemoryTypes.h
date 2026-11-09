/**
 * @file MemoryTypes.h
 * @brief 截图记忆库的公共值类型与来源标记
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QSize>
#include <QString>

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace GyanisScribe::Core
{
    /// 记忆条目的来源
    enum class MemorySource
    {
        Capture, ///< 用户主动截图进入历史
        Import,  ///< 用户手动导入的旧图
    };

    /// 单次搜索返回的条数上限；界面据此提示用户收窄关键词
    inline constexpr int kMemorySearchLimit = 200;

    /// 64 位 dHash 的合并阈值：距离不超过它即视为重复或相似
    inline constexpr int kDuplicateHammingThreshold = 4;

    /// 索引容量上限，超出后淘汰最旧的条目
    inline constexpr int kMaxMemoryEntries = 10000;

    /// 单条 OCR 文本按 UTF-8 字节计的上限
    inline constexpr int kMaxOcrTextBytes = 65536;

    /// 应用筛选里未知应用的保留取值，不用空串当通配
    inline constexpr std::string_view kUnknownProcessFilter = "__unknown__";

    /**
     * @brief 取来源在数据库里使用的稳定标记
     * @param source 条目来源
     * @return capture 或 import
     */
    [[nodiscard]] std::string_view memorySourceToken(MemorySource source);

    /**
     * @brief 把数据库里的来源标记解析回枚举
     * @param token 来源标记
     * @return 识别成功返回来源；未知或空标记返回空
     */
    [[nodiscard]] std::optional<MemorySource> parseMemorySource(std::string_view token);

    /// 一条入库请求
    struct MemoryUpsert
    {
        std::filesystem::path filePath;                       ///< 原图路径，导入不复制文件
        MemorySource          source = MemorySource::Capture; ///< 条目来源
        QDateTime             capturedAt;                     ///< 截图时刻或文件修改时间
        QSize                 size;                           ///< 图像像素尺寸
        QString               processName;                    ///< 命中窗口的进程名，可为空
        QString               windowTitle;                    ///< 命中窗口的标题，可为空
    };

    /// 搜索结果里的一行
    struct MemoryResult
    {
        qint64                id = 0;                         ///< 数据库主键，0 表示尚未入库
        std::filesystem::path filePath;                       ///< 原图路径
        MemorySource          source = MemorySource::Capture; ///< 条目来源
        QDateTime             capturedAt;                     ///< 截图时刻或文件修改时间
        QSize                 size;                           ///< 图像像素尺寸
        QString               processName;                    ///< 命中窗口的进程名，可为空
        QString               windowTitle;                    ///< 命中窗口的标题，可为空
        QString               ocrSnippet;                     ///< 命中的 OCR 片段
        QString               ocrStatus;                      ///< pending、done 或 failed
        QString               dhash;                          ///< 16 位十六进制感知哈希，空表示未算出
        bool                  fileMissing = false;            ///< 搜索时原图已不在磁盘上
    };

    /// 查询条件
    struct MemoryQuery
    {
        QString                  text;                       ///< 用户关键词原文，空表示只按筛选条件列出
        QString                  processName;                ///< 空表示不限；kUnknownProcessFilter 表示未知应用
        std::optional<QDateTime> from;                       ///< 时间下界，空表示不限
        std::optional<QDateTime> to;                         ///< 时间上界，空表示不限
        int                      limit = kMemorySearchLimit; ///< 单次返回上限
    };

    /// 应用筛选下拉的一行
    struct MemoryAppCount
    {
        QString processName; ///< 进程名，可为空
        int     count = 0;   ///< 该应用的条目数
    };

    /// 页脚统计
    struct MemoryStats
    {
        int    totalItems           = 0; ///< 总条数
        int    pendingOcr           = 0; ///< 待识别条数
        int    failedOcr            = 0; ///< 识别失败条数
        qint64 databaseBytes        = 0; ///< 数据库文件字节，含 WAL
        int    excludedProcessCount = 0; ///< 排除应用数
    };

    /// 导入进度
    struct MemoryImportProgress
    {
        int  scanned   = 0;     ///< 已扫描文件数
        int  inserted  = 0;     ///< 已入库条数
        int  skipped   = 0;     ///< 已在库或读不出而跳过的条数
        int  failed    = 0;     ///< 失败条数
        bool cancelled = false; ///< 用户是否已取消
    };

    /// 导入结束报告
    struct MemoryImportReport
    {
        int  scanned   = 0;     ///< 扫描总数
        int  inserted  = 0;     ///< 入库总数
        int  skipped   = 0;     ///< 跳过总数
        int  failed    = 0;     ///< 失败总数
        bool cancelled = false; ///< 是否因取消提前结束
    };
} // namespace GyanisScribe::Core

Q_DECLARE_METATYPE(GyanisScribe::Core::MemoryStats)
Q_DECLARE_METATYPE(GyanisScribe::Core::MemoryImportProgress)
Q_DECLARE_METATYPE(GyanisScribe::Core::MemoryImportReport)
