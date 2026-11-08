/**
 * @file RewindConfig.h
 * @brief 回溯截图配置的读取与排除名单解析
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QString>
#include <QStringList>

namespace GyanisScribe::Core
{
    class ISettingsStore;

    inline constexpr int kDefaultRewindBufferSeconds       = 10;
    inline constexpr int kMinimumRewindBufferSeconds       = 5;
    inline constexpr int kMaximumRewindBufferSeconds       = 30;
    inline constexpr int kDefaultRewindFrameRate           = 5;
    inline constexpr int kMinimumRewindFrameRate           = 2;
    inline constexpr int kMaximumRewindFrameRate           = 10;
    inline constexpr int kRewindMemoryBudgetMb             = 100;
    inline constexpr int kRewindMaximumExclusionTextLength = 2048;

    /// 回溯配置；字节预算目前固定，不暴露设置项
    struct RewindConfig
    {
        bool        enabled       = false;                       ///< 是否开启
        int         bufferSeconds = kDefaultRewindBufferSeconds; ///< 缓冲时长
        int         frameRate     = kDefaultRewindFrameRate;     ///< 目标帧率
        QStringList excludedProcesses;                           ///< 排除的可执行文件名，已小写去重
        int         memoryBudgetMb = kRewindMemoryBudgetMb;      ///< 像素预算
    };

    /**
     * @brief 读配置；坏时长/帧率回默认，排除名单非法时返回错误
     * @param settings 配置存储
     * @return 成功返回配置；排除名单非法时返回带可操作原因的错误
     */
    [[nodiscard]] Base::Expected<RewindConfig> readRewindConfig(const ISettingsStore &settings);

    /**
     * @brief 解析逗号分隔的排除名单
     * @param text 用户原文
     * @return 成功返回小写、去重、保留顺序的文件名；路径/通配符/超长返回错误
     */
    [[nodiscard]] Base::Expected<QStringList> parseRewindExclusions(const QString &text);

    /**
     * @brief 进程名是否命中排除名单
     * @param processName 平台给出的可执行文件名，可为空
     * @param excludedProcesses 已解析的名单
     * @return 命中返回 true；空进程名一律 false
     */
    [[nodiscard]] bool isExcludedProcessName(const QString &processName, const QStringList &excludedProcesses) noexcept;

} // namespace GyanisScribe::Core
