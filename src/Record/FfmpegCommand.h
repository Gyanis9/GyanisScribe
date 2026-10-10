/**
 * @file FfmpegCommand.h
 * @brief 编码命令行：把录屏参数翻成 ffmpeg 的实参表，并负责找得到这个外部程序
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

    /// 帧序列的文件名格式：六位数够九十九万帧（15 帧率下约 18 小时），再长就该分段而不是加位数
    inline constexpr const char *kFrameFilePattern = "%06d.png";

    /**
     * @brief 第 index 帧的文件名
     * @param index 帧序号，从 0 开始
     * @return 形如 000000.png 的文件名
     * @details ffmpeg 的图像序列输入按同一个模板认文件，序号必须定宽补零，
     *          否则第 10 帧会排到第 2 帧前面，成品顺序就乱了。
     */
    [[nodiscard]] std::filesystem::path frameFileName(int index);

    /**
     * @brief 拼 MP4 的实参表
     * @param config 录屏参数，帧率与是否录声音从这儿取
     * @param frameDirectory 帧序列所在目录
     * @param outputFile 成品文件路径
     * @param audioWav 配音文件；空路径表示不录声音
     * @return 传给 ffmpeg 的参数序列（不含可执行文件名本身）
     * @details 不录声音时显式加 -an：让「没有声轨」成为一个说得出的决定，
     *          而不是 ffmpeg 自己发现的意外。另外按 x264 的硬要求把尺寸凑成偶数
     *          （奇数高度会直接报 requested width/height for inplane are not multiples of 2）。
     */
    [[nodiscard]] QStringList mp4Arguments(const RecorderConfig &config, const std::filesystem::path &frameDirectory, const std::filesystem::path &outputFile,
                                           const std::filesystem::path &audioWav);

    /**
     * @brief GIF 第一遍：生成调色板
     * @param config 录屏参数
     * @param frameDirectory 帧序列所在目录
     * @param paletteFile 调色板输出文件
     * @return 传给 ffmpeg 的参数序列
     */
    [[nodiscard]] QStringList gifPaletteArguments(const RecorderConfig &config, const std::filesystem::path &frameDirectory, const std::filesystem::path &paletteFile);

    /**
     * @brief GIF 第二遍：按调色板编码成品
     * @param config 录屏参数
     * @param frameDirectory 帧序列所在目录
     * @param paletteFile 第一遍生成的调色板
     * @param outputFile 成品文件路径
     * @return 传给 ffmpeg 的参数序列
     */
    [[nodiscard]] QStringList gifEncodeArguments(const RecorderConfig &config, const std::filesystem::path &frameDirectory, const std::filesystem::path &paletteFile,
                                                 const std::filesystem::path &outputFile);

    /**
     * @brief 找 ffmpeg 可执行文件
     * @return 成功返回它的绝对路径；找不到返回 DependencyMissing，文案给出装法
     * @details 用 QStandardPaths 而不是自己剥 PATH：Windows 上还要照顾 PATHEXT 的 .exe 后缀，
     *          容器里则要跟着 PATH 找无后缀的 ELF，这两条 Qt 都已经做好了。
     */
    [[nodiscard]] Core::Expected<QString> locateFfmpeg();

    /**
     * @brief 拼「取声到 wav」那条命令的实参表
     * @param microphoneDevice 麦克风那一路的 dshow 标识，空表示不取
     * @param systemSoundDevice 系统声音那一路的标识，空表示不取
     * @param wavFile 中间 wav 的落点，编码那一步再把它并进成品
     * @return 成功返回参数序列（不含可执行文件名）；两路都空时返回 InvalidArgument
     * @details 标识原样交给 `audio=`：本机用 dshow 的稳定标识实测过——一路出 384010 字节、
     *          两路 amix 出 381774 字节的 2 秒 48k 双声道 wav，都不需要额外转义。
     *          中间格式定 pcm_s16le：与编码那一步解耦，取声失败时画面那一半还能独立出片。
     */
    [[nodiscard]] Core::Expected<QStringList> audioCaptureArguments(const QString &microphoneDevice, const QString &systemSoundDevice, const std::filesystem::path &wavFile);

} // namespace GyanisScribe::Record
