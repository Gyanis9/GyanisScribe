/**
 * @file RecorderConfig.h
 * @brief 录屏参数与它们的校验规则
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QRect>
#include <QString>

#include <filesystem>

namespace GyanisScribe::Record
{

    /// 录什么：整屏、选区还是一个窗口
    enum class RecordMode
    {
        FullScreen, ///< 整块屏幕
        Region,     ///< 屏幕内的一个矩形
        Window,     ///< 一个平台窗口
    };

    /// 输出容器：MP4 走 H.264，GIF 走两遍调色板
    enum class OutputFormat
    {
        Mp4, ///< 适合长片段，体积小
        Gif, ///< 适合分享的小动画
    };

    /**
     * @brief 一轮录屏的全部参数
     * @details 坐标一律是屏幕内的逻辑坐标，物理像素由抓帧那一层乘 devicePixelRatio；
     *          这里不放屏幕对象，参数要能原样存进配置与历史。
     */
    struct RecorderConfig
    {
        RecordMode            mode                   = RecordMode::FullScreen; ///< 录什么
        OutputFormat          format                 = OutputFormat::Mp4;      ///< 存成什么
        int                   frameRate              = 15;                     ///< 目标帧率，逻辑每秒帧数
        int                   maximumDurationSeconds = 600;                    ///< 最长录多久，到点自动收
        QRect                 region;                                          ///< Region 模式的选区，屏幕内逻辑坐标
        QString               screenName;                                      ///< 录哪块屏幕，全部模式必填
        quintptr              windowHandle = 0;                                ///< Window 模式的平台窗口句柄（Qt 的 WId）
        std::filesystem::path outputDirectory;                                 ///< 成品文件落在哪个目录
        bool                  cursorHighlight    = false;                      ///< 是否在帧上圈出光标位置
        bool                  captureSystemAudio = false;                      ///< 是否录系统声音，需要平台能力在位
    };

    /**
     * @brief 校验一轮录屏的参数
     * @param config 待校验的参数
     * @return 成功返回空载荷；任一项不成立返回 InvalidArgument 与中文原因
     * @details 全部规则在开始之前判，不边录边判：录到一半才发现帧率不合法，
     *          用户已经白等了十几秒，那比开始就拒绝更糟。
     */
    [[nodiscard]] Core::Expected<void> validateRecorderConfig(const RecorderConfig &config);

    /// MP4 允许的最高帧率：再高就是浪费，屏幕本身也只给到 60
    inline constexpr int kMaximumFrameRate = 60;

    /// GIF 允许的最高帧率：两遍调色板的耗时按帧数乘，且 24 帧以上肉眼已经看不出差别
    inline constexpr int kMaximumGifFrameRate = 24;

    /// 单次录屏的时长上限，秒：录屏最容易被忘在跑，超过两小时当场拒
    inline constexpr int kMaximumDurationSeconds = 7200;

    /// 默认帧率的缺省档：配置里没有、或存的值不合法时用它，与 RecorderConfig::frameRate 的初值同解
    inline constexpr int kDefaultFrameRate = 15;

    /**
     * @brief 解析配置里存的默认帧率
     * @param stored 配置读出的整数值
     * @return 可用的帧率；不在 1..kMaximumFrameRate 里时返回带范围与替代做法的中文原因
     * @details 帧率的上限按输出格式分两档（GIF 更低），但那要等格式定了才知道；
     *          这里只按两档共用的上限判，格式那一层仍由 validateRecorderConfig 把关。
     */
    [[nodiscard]] Core::Expected<int> parseFrameRateSetting(int stored);

} // namespace GyanisScribe::Record
