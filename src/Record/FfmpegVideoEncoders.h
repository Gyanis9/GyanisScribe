/**
 * @file FfmpegVideoEncoders.h
 * @brief MP4 视频编码器的候选表、各家的参数形状与「这台机器到底用哪个」的选型
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <array>
#include <string>
#include <string_view>

namespace GyanisScribe::Record
{
    /// 候选编码器的偏好顺序：软的排最前（不需要显卡也能跑），硬件编码器在后，最后一个是各构建都自带的 mpeg4
    inline constexpr std::array<std::string_view, 5> kPreferredVideoEncoders{
            "libx264", "h264_nvenc", "h264_qsv", "h264_amf", "mpeg4",
    };

    /**
     * @brief 某一个编码器自己的质量参数
     * @param encoder 编码器名，取值来自 kPreferredVideoEncoders
     * @param videoBitrateKbps 画面码率，0 表示按质量优先
     * @return 交给 ffmpeg 的那几条参数
     * @details 各家不通用：`-crf` 只有 x264 认，`-cq` 与 `-preset p4` 是 NVENC 的说法，
     *          `-q:v` 是 mpeg4 的说法。挑错的参数不是画质变差，而是整条编码开不起来。
     *          表外的名字返回空参数，由调用方的探测决定它到底能不能用。
     */
    [[nodiscard]] QStringList videoEncoderQualityArguments(std::string_view encoder, int videoBitrateKbps);

    /**
     * @brief 一条只跑三帧的探测命令
     * @param encoder 要试的编码器
     * @param videoBitrateKbps 画面码率，与真编码同一档
     * @param width 真录屏的帧宽，像素
     * @param height 真录屏的帧高，像素
     * @param frameRate 真录屏的帧率
     * @return 探测命令的参数，末了是 -f null -，不落盘
     * @details 尺寸与帧率都按真录屏那一档给：硬件编码器有帧尺寸上限，用 64×48 探出来的「能用」
     *          到 4K 上不作数，那条绿是假的。
     */
    [[nodiscard]] QStringList videoEncoderProbeArguments(std::string_view encoder, int videoBitrateKbps, int width, int height, int frameRate);

    /**
     * @brief 按偏好顺序选出第一个在这台机器上真开得起来的编码器
     * @param run 执行一条 ffmpeg 命令，成功返回 true
     * @param width 真录屏的帧宽，像素
     * @param height 真录屏的帧高，像素
     * @param frameRate 真录屏的帧率
     * @param videoBitrateKbps 画面码率，0 表示按质量优先
     * @param pinnedEncoder 设置里钉死的那一档；空表示走全自动
     * @return 成功返回选中的编码器名；一个都不开得来返回中文原因，里面列出试过哪几个
     * @details 判据是真跑一次而不是读 `ffmpeg -encoders` 的名单：本机实测那份名单里的
     *          h264_qsv 与 h264_amf 都在（缺 Intel/AMD 的运行库时当场开不起来），
     *          h264_mf 在名单里也开得出，跑到一半却自己崩了。
     *          钉了某一档就先探它：探得过就用它；探不过退回自动档（调用方比较返回值与钉子，
     *          把「钉的那一档这次没开起来」写进日志）——到了编码这一步帧已经录完，
     *          宁可按自动档编出来，也不让这一段白录。钉子不在候选表里时直接退回自动。
     */
    [[nodiscard]] Base::Expected<std::string> selectVideoEncoder(const std::function<bool(const QStringList &)> &run, int width, int height, int frameRate, int videoBitrateKbps,
                                                                 std::string_view pinnedEncoder = {});
} // namespace GyanisScribe::Record
