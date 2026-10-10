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
#include <QPoint>
#include <QObject>
#include <QTimer>

#include <memory>

class QProcess;

namespace GyanisScribe::Record
{

    /**
     * @brief 本轮取声的结果
     * @details 界面据此决定出片通知要不要带「没声」那句：
     *          只记日志等于让用户拿到一个安静的成品，再自己发现没录上。
     */
    enum class AudioCaptureOutcome
    {
        NotRequested, ///< 没要求取声
        Captured,     ///< 取到了，成品里有音轨
        Failed,       ///< 要求了但中途没成：成品仍在，只是没声
    };

    /**
     * @brief 录屏服务
     * @details 这层只做胶水：定时器跳一次就抓一帧交给会话，会话决定写不写；
     *          「什么时候写、暂停怎么算、时长上限」全在 RecordingSession 里，那部分有用例。
     *          本类直测的只有两条窄判据：一份配置能不能开始、取声在该跳过的档位上真跳过了；
     *          真抓屏像素与真编出文件需要一个真实桌面会话，那一段仍然留给验收。
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
         * @brief 析构：收掉还在跑的取声进程
         */
        ~RecordingService() override;

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

        /// 本轮取声的结果，stop() 之后读才有终值
        [[nodiscard]] AudioCaptureOutcome audioOutcome() const noexcept;

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

        /**
         * @brief 按参数把光标圈叠在这一帧上
         * @param frame 刚抓好的帧，就地改
         * @param frameGlobalOriginLogical 这一帧左上角在屏幕全局逻辑坐标里的位置
         * @details 没开就直接返回。画不出来时只报第一条日志：同一个原因会每帧重复一次，
         *          15 帧率录十分钟能把别的信息全淹掉。
         */
        void overlayCursorHighlight(Core::CapturedImage &frame, const QPoint &frameGlobalOriginLogical);

        /**
         * @brief 起取声进程（一路或两路）
         * @return 成功返回空载荷；起不动时返回原因，本轮录屏就地拒绝开始
         * @details 宁可在开始时拒绝，也不要在结束时交出一个安静的成品。等 started 是有界的
         *          （几秒），设备被别的程序独占时 ffmpeg 会立刻退掉。
         */
        [[nodiscard]] Core::Expected<void> startAudioCapture();

        /**
         * @brief 收尾取声进程
         * @return 取到声时返回 wav 路径，否则返回空路径
         * @details 用 stdin 里一个 q 让 ffmpeg 正常收尾（它自己写 wav 的长度头），
         *          强杀会留下一个头信息不对的 wav。
         */
        [[nodiscard]] std::filesystem::path finishAudioCapture();

        Capture::IScreenCaptureSource    &m_source;                                                    ///< 抓帧来源，不拥有
        IRecordingEncoder                &m_encoder;                                                   ///< 编码器，不拥有
        QTimer                            m_timer;                                                     ///< 抓帧定时器
        QElapsedTimer                     m_clock;                                                     ///< 单调时钟，会话的时间轴都从它取
        std::unique_ptr<RecordingSession> m_session;                                                   ///< 当前会话
        std::unique_ptr<IRecordingSink>   m_sink;                                                      ///< 当前帧去处（PNG 序列）
        std::unique_ptr<QProcess>         m_audio;                                                     ///< 取声进程，空表示本轮不取声
        RecorderConfig                    m_config;                                                    ///< 本轮参数
        std::filesystem::path             m_frameDirectory;                                            ///< 本轮临时帧目录
        std::filesystem::path             m_audioWav;                                                  ///< 本轮取声落在哪份 wav 上
        AudioCaptureOutcome               m_audioOutcome          = AudioCaptureOutcome::NotRequested; ///< 取声结果，stop 之后读
        bool                              m_cursorHighlightWarned = false;                             ///< 光标高亮已经报过一次原因，本轮不再重复
    };

} // namespace GyanisScribe::Record
