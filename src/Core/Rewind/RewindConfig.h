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

    /// 缓冲时长的缺省档，秒：十秒够回看刚滑过去的一段操作，占用的帧数也不至于太多
    inline constexpr int kDefaultRewindBufferSeconds = 10;

    /// 缓冲时长的下限，秒：再短只留得住几帧，选择器上没有什么可挑的
    inline constexpr int kMinimumRewindBufferSeconds = 5;

    /// 缓冲时长的上限，秒：与固定的字节预算配套，再长就会频繁淘汰旧帧
    inline constexpr int kMaximumRewindBufferSeconds = 30;

    /// 目标帧率的缺省档，帧每秒：看得出操作过程，帧数又不会成倍增长
    inline constexpr int kDefaultRewindFrameRate = 5;

    /// 目标帧率的下限，帧每秒：再稀就接近定时截屏，回溯时连不上动作
    inline constexpr int kMinimumRewindFrameRate = 2;

    /// 目标帧率的上限，帧每秒：再密对回看帮助不大，只会更快吃满字节预算
    inline constexpr int kMaximumRewindFrameRate = 10;

    /// 像素与缩略图共用的字节预算，MB：固定不开放设置，超预算时由环形缓冲淘汰最旧的帧
    inline constexpr int kRewindMemoryBudgetMb = 100;

    /// 排除名单原文的长度上限，字符：只装逗号分隔的文件名，超长多半是误粘贴
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
     * @return 成功返回小写、去重、保留顺序的文件名；路径、通配符、Windows 文件名不允许的字符或超长文本返回错误
     */
    [[nodiscard]] Base::Expected<QStringList> parseRewindExclusions(const QString &text);

    /**
     * @brief 进程名是否命中排除名单
     * @param processName 平台给出的可执行文件名，可为空
     * @param excludedProcesses 已解析的名单
     * @return 命中返回 true；空进程名一律 false
     * @details 比对时两边都去掉结尾的 .exe 并忽略大小写：名单里写 keepass 还是 keepass.exe 都命中同一个进程。
     */
    [[nodiscard]] bool isExcludedProcessName(const QString &processName, const QStringList &excludedProcesses) noexcept;

} // namespace GyanisScribe::Core
