/**
 * @file RecordingSinks.h
 * @brief 录屏帧的去处：接口、PNG 序列实现与丢弃用的空实现
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QImage>

#include <filesystem>

namespace GyanisScribe::Record
{

    /**
     * @brief 帧落地的去处
     * @details 会话层只认识这个接口：用例可以塞一个只数帧数的假去处，
     *          真实现负责写盘。把「什么时候该写一帧」与「帧怎么落盘」分开，
     *          前者才有可判定的用例。
     */
    class IRecordingSink
    {
    public:
        IRecordingSink()                                  = default;
        virtual ~IRecordingSink()                         = default;
        IRecordingSink(const IRecordingSink &)            = delete;
        IRecordingSink &operator=(const IRecordingSink &) = delete;

        /**
         * @brief 写一帧
         * @param frame 抓到的画面（物理像素）
         * @param index 帧序号，从 0 开始且连续
         * @return 成功返回空载荷；失败返回带路径与原因的错误
         */
        [[nodiscard]] virtual Core::Expected<void> write(const QImage &frame, int index) = 0;

        /**
         * @brief 收尾：把这一轮写的东西定下来
         * @return 成功返回空载荷；失败返回原因
         */
        [[nodiscard]] virtual Core::Expected<void> seal() = 0;

        /// 已经写下的帧数
        [[nodiscard]] virtual int writtenFrames() const = 0;
    };

    /**
     * @brief 按 PNG 序列落盘的去处
     * @details 选 PNG 而不是直接管住 ffmpeg 的 stdin：一是逐帧写文件让「暂停」变成「不写」这种简单事，
     *          二是编码失败时源帧还在，可以换参数重编码而不用重录一遍。
     * @warning 磁盘开销按「帧数 × 单帧大小」涨：1080p 一帧 PNG 约 1-3 MB，15 帧率录十分钟就是 GB 级。
     *          上限由 RecorderConfig::maximumDurationSeconds 兜着，真要做长录屏得换成管道喂 rawvideo。
     */
    class PngSequenceSink final : public IRecordingSink
    {
    public:
        /**
         * @brief 指定落盘目录
         * @param directory 帧序列目录，须已存在或由 write 首次调用时创建
         */
        explicit PngSequenceSink(std::filesystem::path directory);

        /**
         * @brief 写一帧 PNG，文件名为定宽序号
         * @param frame 画面
         * @param index 帧序号
         * @return 成功返回空载荷；目录建不出或写失败返回 FileWriteFailed
         */
        [[nodiscard]] Core::Expected<void> write(const QImage &frame, int index) override;

        /**
         * @brief 收尾：检查至少写过一帧
         * @return 一帧都没有时返回 InternalError，别把一个空目录交给编码器
         */
        [[nodiscard]] Core::Expected<void> seal() override;

        /// 已写帧数
        [[nodiscard]] int writtenFrames() const override;

        /// 帧序列目录
        [[nodiscard]] const std::filesystem::path &directory() const noexcept;

    private:
        std::filesystem::path m_directory; ///< 帧序列目录
        int                   m_written{}; ///< 已写帧数
    };

} // namespace GyanisScribe::Record
