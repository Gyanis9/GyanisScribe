/**
 * @file FfmpegRecordingEncoder.h
 * @brief 录屏的编码步：把帧序列交给 ffmpeg，等它出成品
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"
#include "Record/RecorderConfig.h"

#include <QStringList>

#include <filesystem>

namespace GyanisScribe::Record
{

    /**
     * @brief 编码接口
     * @details 会话层只说「把这些帧编成成品」，不认识 ffmpeg 也不知道参数怎么拼：
     *          用例可以塞一个假编码器验收尾顺序，真实现负责起进程。
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
         * @param config 录屏参数，输出格式与帧率从这儿取
         * @param frameDirectory 帧序列目录
         * @param frameCount 帧数，用于估编码超时
         * @param audioWav 配音文件，空路径表示没有声音
         * @return 成功返回成品文件路径；失败返回原因（含 ffmpeg 的末段输出）
         */
        [[nodiscard]] virtual Core::Expected<std::filesystem::path> encode(const RecorderConfig &config, const std::filesystem::path &frameDirectory, int frameCount,
                                                                           const std::filesystem::path &audioWav) = 0;
    };

    /**
     * @brief 调 ffmpeg 命令行程序的编码器
     * @details 用外部进程而不是 libavformat：后者会把 GPL 与一大坨动态库带进包里，
     *          而录屏不是每次都用的功能，缺了它截图与贴图照常工作。
     * @note 进程是同步等的（在录屏的工作线程上跑），超时按帧数估；
     *       真要做「边录边编」得换成持续写入 stdin 的 rawvideo 管道。
     */
    class FfmpegRecordingEncoder final : public IRecordingEncoder
    {
    public:
        /**
         * @brief 编码一帧的预算时长，毫秒
         * @param frameRate 目标帧率
         * @param frameCount 帧数
         * @return 允许的等待时长
         * @details 取「实际时长的 4 倍 + 30 秒底」：veryfast 预设通常比实时快得多，
         *          4 倍是留给磁盘卡顿与后台负载的余量，30 秒底保证几秒钟的短片不会被超时误杀。
         */
        [[nodiscard]] static int encodeTimeoutMs(int frameRate, int frameCount);

        /**
         * @brief 帧序列 → 成品，见 IRecordingEncoder::encode()
         * @param config 录屏参数
         * @param frameDirectory 帧序列目录
         * @param frameCount 帧数
         * @param audioWav 配音文件路径，空表示无声
         * @return 成品路径或原因
         */
        [[nodiscard]] Core::Expected<std::filesystem::path> encode(const RecorderConfig &config, const std::filesystem::path &frameDirectory, int frameCount,
                                                                   const std::filesystem::path &audioWav) override;

    private:
        /**
         * @brief 跑一次 ffmpeg 并等它结束
         * @param arguments 实参表
         * @param timeoutMs 允许的等待时长
         * @param stepName 这一步的名字，出错时进文案
         * @return 成功返回空载荷；失败返回带退出码与末段输出的原因
         */
        [[nodiscard]] static Core::Expected<void> run(const QStringList &arguments, int timeoutMs, std::string_view stepName);
    };

} // namespace GyanisScribe::Record
