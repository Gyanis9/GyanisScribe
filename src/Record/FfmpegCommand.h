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
     * @brief 这一轮实际达成的输入帧率，写成交给 ffmpeg 的那一句
     * @param frameCount 帧序列里实际存下的帧数
     * @param recordedDurationMs 这一轮录了多久，毫秒（暂停的那一段不计）
     * @param nominalFrameRate 配置里的目标帧率
     * @return 形如 70000/6000 的有理数；帧数或时长量不出来时退回目标帧率的整数写法
     * @details 机器跟不上目标帧率时帧序列就是短的：按目标帧率编会把 6 秒编成 2.33 秒（实测），
     *          画面加速、音轨还要被 -shortest 截掉尾巴。达成率按「帧数 × 1000 / 毫秒」写成分数，
     *          成品时长就和录的那一段一致。用分数而不是四舍五入的整数：11.5 帧率取整到 12 会把十分钟拧成九分半。
     */
    [[nodiscard]] QString achievedFrameRateArgument(int frameCount, qint64 recordedDurationMs, int nominalFrameRate);

    /**
     * @brief 拼 MP4 的实参表
     * @param config 录屏参数，帧率取的是「量不出来时退回的那一档」，画面码率也从这儿取
     * @param frameCount 帧序列里实际存下的帧数
     * @param recordedDurationMs 这一轮录了多久，毫秒
     * @param frameDirectory 帧序列所在目录
     * @param outputFile 成品文件路径
     * @param audioWav 配音文件；空路径表示不录声音
     * @param videoEncoder 编码器名，取值来自 Record::kPreferredVideoEncoders，由调用方探出来再交进来
     * @return 传给 ffmpeg 的参数序列（不含可执行文件名本身）
     * @details 不录声音时显式加 -an：让「没有声轨」成为一个说得出的决定，
     *          而不是 ffmpeg 自己发现的意外。尺寸按 H.264 一类的硬要求凑成偶数
     *          （奇数高度会直接报 requested width/height for inplane are not multiples of 2）。
     */
    [[nodiscard]] QStringList mp4Arguments(const RecorderConfig &config, int frameCount, qint64 recordedDurationMs, const std::filesystem::path &frameDirectory,
                                           const std::filesystem::path &outputFile, const std::filesystem::path &audioWav, std::string_view videoEncoder);

    /**
     * @brief GIF 第一遍：生成调色板
     * @param config 录屏参数，只在帧数量不出来时提供退回的那一档
     * @param frameCount 帧序列里实际存下的帧数
     * @param recordedDurationMs 这一轮录了多久，毫秒
     * @param frameDirectory 帧序列所在目录
     * @param paletteFile 调色板输出文件
     * @return 传给 ffmpeg 的参数序列
     */
    [[nodiscard]] QStringList gifPaletteArguments(const RecorderConfig &config, int frameCount, qint64 recordedDurationMs, const std::filesystem::path &frameDirectory,
                                                  const std::filesystem::path &paletteFile);

    /**
     * @brief GIF 第二遍：按调色板编码成品
     * @param config 录屏参数，只在帧数量不出来时提供退回的那一档
     * @param frameCount 帧序列里实际存下的帧数
     * @param recordedDurationMs 这一轮录了多久，毫秒
     * @param frameDirectory 帧序列所在目录
     * @param paletteFile 第一遍生成的调色板
     * @param outputFile 成品文件路径
     * @return 传给 ffmpeg 的参数序列
     */
    [[nodiscard]] QStringList gifEncodeArguments(const RecorderConfig &config, int frameCount, qint64 recordedDurationMs, const std::filesystem::path &frameDirectory,
                                                 const std::filesystem::path &paletteFile, const std::filesystem::path &outputFile);

    /**
     * @brief 随包那份 ffmpeg 可执行文件
     * @param applicationDirectory 可执行体所在目录
     * @return 目录里有 ffmpeg（Windows 上带 .exe 后缀）时返回它的绝对路径，否则返回空串
     * @details 安装包自带这一件，用户不需要再往系统里装一份；找不到随包那份才回落到 PATH。
     */
    [[nodiscard]] QString bundledFfmpegPath(const QString &applicationDirectory);

    /**
     * @brief 找 ffmpeg 可执行文件
     * @return 成功返回它的绝对路径；找不到返回 DependencyMissing，文案给出补法
     * @details 顺序是「程序目录里随包那份 → PATH」：前者保证装完即用，后者留给自带构建的高级用户。
     *          PATH 那一半用 QStandardPaths 而不是自己剥：Windows 上还要照顾 PATHEXT 的 .exe 后缀，
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
