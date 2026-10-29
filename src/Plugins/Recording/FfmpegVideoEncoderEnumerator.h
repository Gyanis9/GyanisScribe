/**
 * @file FfmpegVideoEncoderEnumerator.h
 * @brief MP4 视频编码器的可用清单：逐候选真探一遍，异步回话
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>

class QProcess;

namespace GyanisScribe::Plugins
{

    /// 设置页那份清单的取样档：探测要按真录屏的尺寸才有意义（硬件编码器有帧尺寸上限），
    /// 而设置页拿不到用户将来的真实选区——按 1920×1080@30 这一档代表性最大的探，
    /// 真编码前还会按真实尺寸再探一次（探不过退回自动档并记日志），两道一起把关
    inline constexpr int kEncoderProbeWidth     = 1920;
    inline constexpr int kEncoderProbeHeight    = 1080;
    inline constexpr int kEncoderProbeFrameRate = 30;

    /**
     * @brief 逐个探测候选编码器，给出这台机器现在开得起来的那些
     * @details 判据与编码前的选型同一条：真跑三帧到 null 输出，不看 `ffmpeg -encoders` 的名单。
     *          一个进程一个候选地串行跑（同时挂五个只会让五个都抢 CPU）；一次只允许在飞一轮。
     *          列表按 kPreferredVideoEncoders 的偏好顺序给，界面上「自动」那一档就是照这个顺序挑。
     */
    class FfmpegVideoEncoderEnumerator : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 一条探测命令
         * @details 拆成程序与实参两段：生产实现用 locateFfmpeg 找程序，用例可以换成任何一条会卡住的命令。
         */
        struct ProbeCommand
        {
            QString     program;   ///< 可执行文件名或路径
            QStringList arguments; ///< 传给它的实参，不含程序名本身
        };

        /// 按候选编码器名拼一条探测命令；拼不出来时返回原因（生产实现里就是 ffmpeg 不在场）
        using ProbeCommandBuilder = std::function<Base::Expected<ProbeCommand>(const QString &encoderName)>;

        /**
         * @brief 建枚举器：探测命令走生产实现，单个候选超时 15 秒
         * @param parent QObject 父对象，默认无
         */
        explicit FfmpegVideoEncoderEnumerator(QObject *parent = nullptr);

        /**
         * @brief 建枚举器：探测命令与单人超时由调用方给
         * @param builder 按候选编码器名拼命令
         * @param probeTimeoutMs 单个候选的探测上限，毫秒；到点强杀进程并跳过这一档
         * @param parent QObject 父对象，默认无
         * @note 这一支只给用例：生产构造固定走 locateFfmpeg 与 videoEncoderProbeArguments。
         */
        FfmpegVideoEncoderEnumerator(ProbeCommandBuilder builder, int probeTimeoutMs, QObject *parent = nullptr);

        /**
         * @brief 析构：收掉还在跑的 ffmpeg
         */
        ~FfmpegVideoEncoderEnumerator() override;

        FfmpegVideoEncoderEnumerator(const FfmpegVideoEncoderEnumerator &)            = delete;
        FfmpegVideoEncoderEnumerator &operator=(const FfmpegVideoEncoderEnumerator &) = delete;

        /**
         * @brief 发起一轮探测，结果走 updated 或 failed
         * @details 上一轮还没回来时重复调用会被忽略：同时挂两串 ffmpeg 只会让两份清单互相盖。
         */
        void refresh();

    signals:
        /**
         * @brief 清单拿到了
         * @param usableEncoders 这台机器现在开得起来的编码器名，按偏好顺序；至少一个
         */
        void updated(const QStringList &usableEncoders);

        /**
         * @brief 一个候选都没探过、或 ffmpeg 本身不在场
         * @param error 带中文原因与替代做法
         */
        void failed(const GyanisScribe::Base::Error &error);

    private:
        /// 探下一个候选；候选探完了就在这里收口
        void probeNext();

        /// 当前候选的进程收尾：退出码 0 记为可用，继续下一个
        void handleFinished();

        /// 当前候选超时：强杀进程、记日志、跳过这一档，接着探下一个
        void handleProbeTimeout();

        ProbeCommandBuilder       m_probeCommandBuilder; ///< 按候选编码器名拼探测命令
        int                       m_probeTimeoutMs = 0;  ///< 单个候选的探测上限，毫秒
        QTimer                    m_probeTimeout;        ///< 当前候选的超时表，单次
        std::unique_ptr<QProcess> m_process;             ///< 正在跑的探测进程；空表示没在探
        QStringList               m_remaining;           ///< 还没探的候选，按偏好顺序
        QStringList               m_usable;              ///< 已经探通的候选
        bool                      m_busy = false;        ///< 是否已有一轮探测在飞
    };

} // namespace GyanisScribe::Plugins
