/**
 * @file IRecordingEncoder.h
 * @brief 录屏编码步的接口：会话层只说「把这些帧编成成品」
 * @author Gyanis
 * @date 2026-10-16
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Record/RecorderConfig.h"

#include <filesystem>

namespace GyanisScribe::Core
{

    /**
     * @brief 编码接口
     * @details 会话层只说「把这些帧编成成品」，不认识 ffmpeg 也不知道参数怎么拼：
     *          用例可以塞一个假编码器验收尾顺序，真实现负责起进程。
     * @note 实现住在 Plugins/Recording（FfmpegRecordingEncoder），由装配层注入。
     */
    class IRecordingEncoder
    {
    public:
        IRecordingEncoder()                                     = default;
        virtual ~IRecordingEncoder()                            = default;
        IRecordingEncoder(const IRecordingEncoder &)            = delete;
        IRecordingEncoder &operator=(const IRecordingEncoder &) = delete;

        /**
         * @brief 编码一段帧序列
         * @param config 录屏参数，输出格式与「量不出时长时退回的那一档帧率」从这儿取
         * @param frameDirectory 帧序列目录
         * @param frameCount 实际存下的帧数，用于估编码超时与算达成帧率
         * @param recordedDurationMs 这一轮录了多久，毫秒：成品时长按「帧数 / 这个时长」铺开
         * @param audioWav 配音文件，空路径表示没有声音
         * @return 成功返回成品文件路径；失败返回原因（含 ffmpeg 的末段输出）
         * @details 帧数与时长必须成对交进来：机器跟不上目标帧率时帧序列是短的，
         *          只按 config 里的目标帧率编会把六秒编成两秒（实测），画面加速、音轨被截尾。
         */
        [[nodiscard]] virtual Base::Expected<std::filesystem::path> encode(const RecorderConfig &config, const std::filesystem::path &frameDirectory, int frameCount,
                                                                           qint64 recordedDurationMs, const std::filesystem::path &audioWav) = 0;
    };

} // namespace GyanisScribe::Core
