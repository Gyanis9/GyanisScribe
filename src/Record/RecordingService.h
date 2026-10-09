/**
 * @file RecordingService.h
 * @brief 录屏编排：定时抓帧、状态推进、收尾编码与临时帧清理
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Capture/IScreenCaptureSource.h"
#include "Record/FfmpegRecordingEncoder.h"
#include "Record/RecorderConfig.h"
#include "Record/RecordingSession.h"
#include "Record/RecordingSinks.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <memory>

namespace GyanisScribe::Record
{

    /**
     * @brief 录屏服务
     * @details 这层只做胶水：定时器跳一次就抓一帧交给会话，会话决定写不写；
     *          「什么时候写、暂停怎么算、时长上限」全在 RecordingSession 里，那部分有用例。
     *          本类的端到端行为（真的抓到屏幕像素并编出文件）需要一个真实桌面会话，容器里只能验到编译。
     */
    class RecordingService : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 建服务
         * @param source 抓帧来源，所有权在宿主
         * @param encoder 编码器，所有权在宿主
         * @param parent Qt 父对象
         */
        RecordingService(Capture::IScreenCaptureSource &source, IRecordingEncoder &encoder, QObject *parent = nullptr);

        /**
         * @brief 开始一轮录屏
         * @param config 参数，先过校验，不合格直接拒绝
         * @param countdownSeconds 倒计时秒数，0 表示立即开始
         * @return 成功返回空载荷；参数不合法、上一轮还在跑或建不出帧目录时返回原因
         */
        [[nodiscard]] Core::Expected<void> start(const RecorderConfig &config, int countdownSeconds = 3);

        /**
         * @brief 暂停
         */
        void pause();

        /**
         * @brief 继续
         */
        void resume();

        /**
         * @brief 停止并编码成品
         * @return 成功返回成品文件路径；失败返回原因
         * @details 编码成功就删掉临时帧序列（那是 GB 级的中间产物）；失败时保留，
         *          用户可以换参数重编码而不用重录一遍。
         */
        [[nodiscard]] Core::Expected<std::filesystem::path> stop();

        /// 当前状态
        [[nodiscard]] RecordingState state() const noexcept;

        /// 已经录了多少整秒（不含倒计时与暂停）
        [[nodiscard]] int recordedSeconds() const;

    signals:
        /// 倒计时剩余整秒数变了
        void countdownChanged(int remainingSeconds);
        /// 存下了一帧
        void frameStored(int frameIndex);
        /// 出错了，文案是可以直接给用户看的那句
        void failed(const QString &reason);
        /// 录屏结束并编出成品
        void finished(const QString &outputPath);

    private:
        /// 定时器一跳：抓一帧并推进会话
        void onTick();

        /// 按模式抓到这一跳的画面
        [[nodiscard]] Core::Expected<Core::CapturedImage> grabFrame();

        Capture::IScreenCaptureSource    &m_source;         ///< 抓帧来源，不拥有
        IRecordingEncoder                &m_encoder;        ///< 编码器，不拥有
        QTimer                            m_timer;          ///< 抓帧定时器
        QElapsedTimer                     m_clock;          ///< 单调时钟，会话的时间轴都从它取
        std::unique_ptr<RecordingSession> m_session;        ///< 当前会话
        std::unique_ptr<IRecordingSink>   m_sink;           ///< 当前帧去处（PNG 序列）
        RecorderConfig                    m_config;         ///< 本轮参数
        std::filesystem::path             m_frameDirectory; ///< 本轮临时帧目录
    };

} // namespace GyanisScribe::Record
