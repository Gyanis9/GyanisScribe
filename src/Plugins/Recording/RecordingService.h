/**
 * @file RecordingService.h
 * @brief 录屏编排：定时抓帧、状态推进、收尾编码与临时帧清理
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Capture/IScreenCaptureSource.h"
#include "Platform/IPlatformIntegration.h"
#include "Core/Record/IRecordingEncoder.h"
#include "Core/Record/RecorderConfig.h"
#include "Core/Record/RecordingSession.h"
#include "Core/Record/RecordingSinks.h"

#include <QElapsedTimer>
#include <QFuture>
#include <QFutureWatcher>
#include <QPoint>
#include <QObject>
#include <QSize>
#include <QTimer>

#include <memory>
#include <optional>
#include <vector>

class QProcess;

namespace GyanisScribe::Plugins
{

    /**
     * @brief 从平台给的窗口清单里取「这一帧的左上角」在全局逻辑坐标里的位置
     * @param windows 平台报出的可见窗口快照
     * @param handle 本轮录的那扇窗的句柄
     * @return 客户区的左上角；清单里没有这扇窗、或它没给客户区读数时返回空
     * @details 只认客户区那一份读数：抓下来的那一帧就是客户区的像素（本机实测整窗 422×292 对
     *          客户区 420×260），拿整窗的左上角当帧原点会把高亮圈偏到标题栏上。
     * @note 取不到就交回空而不是「退而用整窗那一份」：圈画在错位置上比不画更难解释。
     */
    [[nodiscard]] std::optional<QPoint> windowFrameOrigin(const std::vector<Platform::ScreenWindow> &windows, quintptr handle);

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
     *          「什么时候写、暂停怎么算、时长上限」全在 Core::RecordingSession 里，那部分有用例。
     *          本类直测的是这几条：一份配置能不能开始、取声在该跳过的档位上真跳过了、编码交给工作线程
     *          而回接落在界面线程、失败时帧目录留着、取消时帧目录删掉而编码一次都不叫、析构会等手上的编码、
     *          窗口模式按平台给的客户区读数把光标高亮叠在正确的格子上（光标位置由 cursorGlobalPosition 钉住）；
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
         * @param integration 平台集成，用来问「录的这扇窗在屏幕上的哪儿」，所有权在宿主
         * @param parent Qt 父对象
         */
        RecordingService(Core::IScreenCaptureSource &source, Core::IRecordingEncoder &encoder, Platform::IPlatformIntegration &integration, QObject *parent = nullptr);

        /**
         * @brief 析构：收掉还在跑的取声进程，并等手上的编码跑完
         * @details 等编码是因为那份 ffmpeg 由本服务起，而帧目录的处置跟着它走；
         *          不等就是让工作线程拿着已析构的编码器与配置继续编。
         */
        ~RecordingService() override;

        /**
         * @brief 开始一轮录屏
         * @param config 参数，先过校验，不合格直接拒绝
         * @param countdownSeconds 倒计时秒数，0 表示立即开始
         * @return 成功返回空载荷；参数不合法、上一轮还在跑或建不出帧目录时返回原因
         */
        [[nodiscard]] Base::Expected<void> start(const Core::RecorderConfig &config, int countdownSeconds = 3);

        /**
         * @brief 暂停
         */
        void pause();

        /**
         * @brief 继续
         */
        void resume();

        /**
         * @brief 停止录制并开始编码收尾
         * @return 成功返回空载荷，成品稍后由 finished 送回；当前没有录制、或这一轮已经收过尾时返回原因
         * @details 编码要几秒到几十秒，同步等就是把整个程序停在那儿，因此这一段搬到工作线程上。
         *          帧目录与中间 wav 的处置跟着那一次编码走：不等它完成就删，用户换参数重编码的退路就没了；
         *          而期间重新开一轮用的是另一个带时间戳的目录，两轮互不影响。
         */
        [[nodiscard]] Base::Expected<void> stop();

        /**
         * @brief 取消本轮录屏：这一段不要了，不出成品
         * @return 成功返回空载荷；当前没有录制时返回原因
         * @details 与 stop() 的分别只在收尾：不封盘、不起编码，已落的临时帧与中间 wav 直接删掉，
         *          会话随之交回（state() 回到 Idle），所以下一轮不会被「上一轮还没收尾」拦住。
         */
        [[nodiscard]] Base::Expected<void> cancel();

        /// 当前状态
        [[nodiscard]] Core::RecordingState state() const noexcept;

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
        /**
         * @brief 录屏结束并编出成品
         * @param outputPath 成品路径
         * @param frameSize 这一轮实际落下的那一帧的物理尺寸，一张都没落时为空
         * @details 尺寸跟着这一轮一起交出去，而不是让接收方回来读成员的：编码在后台跑，
         *          期间用户可以开第二轮，读到的是后一轮的尺寸。
         */
        void finished(const QString &outputPath, const QSize &frameSize);
        /// 本轮被取消：临时帧与中间 wav 都已删掉，没有成品也不会有 finished
        void aborted();

    private:
        /// 定时器一跳：抓一帧并推进会话
        void onTick();

        /// 按模式抓到这一跳的画面
        [[nodiscard]] Base::Expected<Base::CapturedImage> grabFrame();

        /**
         * @brief 按参数把光标圈叠在这一帧上
         * @param frame 刚抓好的帧，就地改
         * @param frameGlobalOriginLogical 这一帧左上角在屏幕全局逻辑坐标里的位置
         * @details 没开就直接返回。画不出来时只报第一条日志：同一个原因会每帧重复一次，
         *          15 帧率录十分钟能把别的信息全淹掉。
         */
        void overlayCursorHighlight(Base::CapturedImage &frame, const QPoint &frameGlobalOriginLogical);

        /**
         * @brief 起取声进程（一路或两路）
         * @return 成功返回空载荷；起不动时返回原因，本轮录屏就地拒绝开始
         * @details 宁可在开始时拒绝，也不要在结束时交出一个安静的成品。等 started 是有界的
         *          （几秒），设备被别的程序独占时 ffmpeg 会立刻退掉。
         */
        [[nodiscard]] Base::Expected<void> startAudioCapture();

        /**
         * @brief 收尾取声进程
         * @return 取到声时返回 wav 路径，否则返回空路径
         * @details 用 stdin 里一个 q 让 ffmpeg 正常收尾（它自己写 wav 的长度头），
         *          强杀会留下一个头信息不对的 wav。
         */
        [[nodiscard]] std::filesystem::path finishAudioCapture();

        /// 一次编码的结果：成品路径，或原因
        using EncodeOutcome = Base::Expected<std::filesystem::path>;

        /**
         * @brief 把这一帧序列交给工作线程编码
         * @param config 本轮参数的快照：期间重新开一轮会覆盖成员那份，编码要用的是这一次的格式与码率
         * @param frameDirectory 这一轮的帧目录，同上按快照带过去
         * @param frames 帧数，估编码超时与算达成帧率用
         * @param recordedDurationMs 这一轮录了多久，毫秒（暂停不计）：与帧数一起决定成品按什么帧率铺开
         * @param audioWav 中间 wav，空表示本轮没取声
         * @param frameSize 这一轮落下的那一帧的物理尺寸，成品进历史时按它记
         * @details 结果用 QFuture::then 送回界面线程（continuation 的 context 是本对象），
         *          因此并发跑两轮编码也各有归处，不需要排队。
         */
        void startEncoding(const Core::RecorderConfig &config, const std::filesystem::path &frameDirectory, int frames, qint64 recordedDurationMs,
                           const std::filesystem::path &audioWav, const QSize &frameSize);

        /**
         * @brief 编码回来之后处置成品、中间 wav 与帧目录
         * @param outcome 编码结果
         * @param frameDirectory 这一次编码用的帧目录
         * @param audioWav 这一次编码并进去的 wav
         * @param frameSize 这一次编码吃的那一帧的尺寸，随 finished 一起交出去
         */
        void deliverEncoding(const EncodeOutcome &outcome, const std::filesystem::path &frameDirectory, const std::filesystem::path &audioWav, const QSize &frameSize);

    protected:
        /**
         * @brief 取光标此刻的全局逻辑位置
         * @return 生产实现交回 QCursor::pos()
         * @details 留成可覆写的只为一件事：用例要把光标钉在一个已知点上，否则「圈该落在帧里的第几格」
         *          没法断言——真实的鼠标随时会动，而这一层的判据是像素位置。
         */
        [[nodiscard]] virtual QPoint cursorGlobalPosition() const;

    private:
        Core::IScreenCaptureSource             &m_source;                                           ///< 抓帧来源，不拥有
        Core::IRecordingEncoder                &m_encoder;                                          ///< 编码器，不拥有
        Platform::IPlatformIntegration         &m_integration;                                      ///< 平台集成，问窗口位置用，不拥有
        QTimer                                  m_timer;                                            ///< 抓帧定时器
        QElapsedTimer                           m_clock;                                            ///< 单调时钟，会话的时间轴都从它取
        std::unique_ptr<Core::RecordingSession> m_session;                                          ///< 当前会话
        std::unique_ptr<Core::IRecordingSink>   m_sink;                                             ///< 当前帧去处（PNG 序列）
        std::unique_ptr<QProcess>               m_audio;                                            ///< 取声进程，空表示本轮不取声
        Core::RecorderConfig                    m_config;                                           ///< 本轮参数
        std::filesystem::path                   m_frameDirectory;                                   ///< 本轮临时帧目录
        std::filesystem::path                   m_audioWav;                                         ///< 本轮取声落在哪份 wav 上
        QSize                                   m_frameSize;                                        ///< 本轮第一张落下的帧的物理尺寸，成品进历史按它记，start 时清空
        AudioCaptureOutcome                     m_audioOutcome = AudioCaptureOutcome::NotRequested; ///< 取声结果，stop 之后读
        std::vector<QFuture<EncodeOutcome>>     m_runningEncodes;                                   ///< 还在编码的那几份，析构前要等完
        /// 还在回接的那几份：watcher 挂在本对象下随它一起析构，析构前要先拆线再等编码（与 RecognitionRunner 同一条理由）
        std::vector<QFutureWatcher<EncodeOutcome> *> m_runningDeliveries;
        bool                                         m_cursorHighlightWarned = false; ///< 光标高亮已经报过一次原因，本轮不再重复
    };

} // namespace GyanisScribe::Plugins
