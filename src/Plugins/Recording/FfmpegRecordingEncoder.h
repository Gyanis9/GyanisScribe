/**
 * @file FfmpegRecordingEncoder.h
 * @brief 录屏的编码步：把帧序列交给 ffmpeg，等它出成品
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Record/RecorderConfig.h"
#include "Core/Record/IRecordingEncoder.h"
#include "Plugins/Recording/FfmpegVideoEncoders.h"

#include <QSize>
#include <QStringList>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace GyanisScribe::Plugins
{

    /**
     * @brief 调 ffmpeg 命令行程序的编码器
     * @details 用外部进程而不是 libavformat：后者会把 GPL 与一大坨动态库带进包里，
     *          而录屏不是每次都用的功能，缺了它截图与贴图照常工作。
     * @note 本方法自己同步等 ffmpeg 退出，等在哪条线程上由调用方决定：Plugins::RecordingService 把它放到
     *       Qt 的线程池上，界面线程不等它。编码之前先探一次编码器，探的费用也落在同一次等待里。
     *       真要做「边录边编」得换成持续写入 stdin 的 rawvideo 管道（README 的后续计划里那一格）。
     */
    class FfmpegRecordingEncoder final : public Core::IRecordingEncoder
    {
    public:
        /// 跑一条探测命令，成功返回 true；空函数表示生产实现真跑 ffmpeg
        using EncoderProbe = std::function<bool(const QStringList &)>;

        /**
         * @brief 建编码器
         * @param probe 探测命令的替身，空表示真跑 ffmpeg
         * @note 注入只给用例：自动档的探测缓存不依赖本机装没装 ffmpeg 就能验。
         */
        explicit FfmpegRecordingEncoder(EncoderProbe probe = {});

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
         * @brief 帧序列 → 成品，见 Core::IRecordingEncoder::encode()
         * @details 重写 Core::IRecordingEncoder::encode()：起一次 ffmpeg，先探编码器再按达成帧率铺开画面；
         *          GIF 走两遍（调色板与编码），MP4 走一遍。
         * @param config 录屏参数
         * @param frameDirectory 帧序列目录
         * @param frameCount 实际存下的帧数
         * @param recordedDurationMs 这一轮录了多久，毫秒——与帧数一起决定 -framerate 那一句
         * @param audioWav 配音文件路径，空表示无声
         * @return 成品路径或原因
         */
        [[nodiscard]] Base::Expected<std::filesystem::path> encode(const Core::RecorderConfig &config, const std::filesystem::path &frameDirectory, int frameCount,
                                                                   qint64 recordedDurationMs, const std::filesystem::path &audioWav) override;

        /**
         * @brief 走一遍选型，不做真编码
         * @param config 录屏参数，帧率、码率与编码器钉子从这儿取
         * @param frameSize 帧尺寸，像素
         * @return 成功返回选中的编码器名；候选全开不起来返回原因
         * @details 用例用：判的是缓存命中与失效，探测本身由构造注入的 EncoderProbe 回答。
         */
        [[nodiscard]] Base::Expected<std::string> selectEncoderForTesting(const Core::RecorderConfig &config, const QSize &frameSize);

        /// 用例用：直接拿到自动档探测缓存，验命中与失效
        [[nodiscard]] VideoEncoderProbeCache &probeCacheForTesting() noexcept;

    private:
        /**
         * @brief 跑一次 ffmpeg 并等它结束
         * @param arguments 实参表
         * @param timeoutMs 允许的等待时长
         * @param stepName 这一步的名字，出错时进文案
         * @return 成功返回空载荷；失败返回带退出码与末段输出的原因
         */
        [[nodiscard]] static Base::Expected<void> run(const QStringList &arguments, int timeoutMs, std::string_view stepName);

        /**
         * @brief 读出这一轮帧序列的尺寸
         * @param frameDirectory 帧序列目录
         * @return 成功返回首帧尺寸，像素；一帧都没写成或 PNG 读不出时返回中文原因
         */
        [[nodiscard]] static Base::Expected<QSize> firstFrameSize(const std::filesystem::path &frameDirectory);

        /**
         * @brief 按真录屏的尺寸探出这台机器上开得起来的编码器
         * @param config 录屏参数，帧率与画面码率从这儿取
         * @param frameSize 首帧尺寸，像素
         * @param servedFromCache 交回这次结果是否来自缓存（只有自动档可能为 true）
         * @return 成功返回编码器名；候选全开不起来返回原因
         * @details 自动档探通的结果按帧尺寸、帧率与码率记进缓存，同一档参数再录一轮直接复用；
         *          钉住编码器时不查也不写缓存，每次照旧真探。
         */
        [[nodiscard]] Base::Expected<std::string> selectEncoder(const Core::RecorderConfig &config, const QSize &frameSize, bool &servedFromCache);

        VideoEncoderProbeCache m_probeCache; ///< 自动档探测结果，按帧尺寸、帧率与码率分格
        EncoderProbe           m_probe;      ///< 探测命令的替身；空表示真跑 ffmpeg
    };

} // namespace GyanisScribe::Plugins
