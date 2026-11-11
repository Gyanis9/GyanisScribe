/**
 * @file RewindCoordinator.h
 * @brief 回溯缓冲的开关、抓帧、编码、状态机与选帧交付
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Rewind/FrameChangeDetector.h"
#include "Core/Rewind/FrameRingBuffer.h"
#include "Core/Rewind/RewindConfig.h"
#include "Core/Rewind/RewindTypes.h"
#include "Gui/Controls/Toast.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>
#include <optional>

class QScreen;

namespace GyanisScribe::Core
{
    class IScreenCaptureSource;
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    class RewindPickerWindow;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::App
{
    /**
     * @brief 回溯截图协调器
     * @details 只抓光标所在屏；画面变化才编码；选择器/框选/标注期间暂停；锁屏与排除程序自动暂停。
     * @note 所有状态与缓冲只在 GUI 线程访问；编码与解码在 QtConcurrent 线程池，结果回 GUI 线程追加。
     *       托盘只认 bufferStatusChanged 这一条通道，本类不直接改托盘文案。
     */
    class RewindCoordinator : public QObject
    {
        Q_OBJECT

    public:
        /// 构造依赖
        struct Dependencies
        {
            Core::IScreenCaptureSource     &source;      ///< 抓帧来源
            Platform::IPlatformIntegration &integration; ///< 前台窗口、窗口枚举与会话信号
            Core::ISettingsStore           &settings;    ///< 回溯设置
        };

        /**
         * @brief 构造协调器
         * @param dependencies 三样外部依赖，都只引用不拥有
         * @param parent Qt 父对象
         */
        explicit RewindCoordinator(Dependencies dependencies, QObject *parent = nullptr);

        /**
         * @brief 析构：停表、清空并等编码与解码收口
         * @details 工作线程里的结果不能在对象销毁后回到已析构的接收者身上，所以先断线再等。
         */
        ~RewindCoordinator() override;

        /**
         * @brief 按设置启动缓冲
         * @details 未开启、配置读不出、或平台给不出锁屏通知时保持关闭；后两者会说清原因。
         */
        void start();

        /// 停止缓冲：清空、关闭选择器、丢弃在途结果
        void stop();

        /**
         * @brief 打开回溯选择器
         * @details 未开启、正在框选、缓冲为空时给出可操作提示；编码在跑时等它完成再打开。
         */
        void showPicker();

        /// 框选/标注/交付结束：只有处于 CaptureFlow 时恢复缓冲
        void handleCaptureFlowFinished();

        /**
         * @brief 清空缓冲但不改开关
         * @details 开启时清空后继续抓新帧；锁屏、排除程序与错误暂停保持原状；关闭时是空操作。
         *          选择器开着时清空会收起它并恢复抓帧：空胶片条没有可选项。
         */
        void clearBuffer();

        /// 设置变更后重读：关掉即停，开启则应用新容量，原本关闭的顺手启动
        void reloadFromSettings();

        /**
         * @brief 选择器此刻是否摆在屏上
         * @return 选择器建过且可见时为 true
         * @details 步骤记录启动前要用它挡人：选择器是盖住桌面的遮罩，叠在录制里会把遮罩自己也记成一步。
         */
        [[nodiscard]] bool isPickerVisible() const noexcept;

        /// 用例直接驱动一次抓帧，不等真实定时器
        void captureTickForTesting();

        /// 当前缓冲读数，供用例判追加与淘汰
        [[nodiscard]] Core::RewindBufferStats bufferStatsForTesting() const;

        /// 缓冲里最旧一帧的样本号；空缓冲返回空
        [[nodiscard]] std::optional<quint64> firstFrameIdForTesting() const;

        /**
         * @brief 某一帧样本的抓帧时刻
         * @param id 样本号
         * @return 样本还在缓冲里时返回它的时刻；已被淘汰时返回无效值
         * @details 用例拿它核对选帧交回的帧带的是样本那一格时间，而不是解码那一刻的现时。
         */
        [[nodiscard]] QDateTime sampleCapturedAtForTesting(quint64 id) const;

        /**
         * @brief 当前状态按对外状态枚举折算，供用例判暂停与恢复
         * @details 对外枚举只有四档暂停原因，选择器与框选这两档界面流程报 Buffering；
         *          托盘要看到的暂停由 bufferStatusChanged 单独表达。
         */
        [[nodiscard]] Core::RewindStatus statusForTesting() const noexcept;

        /// 用例按样本号走一遍选帧解码，与选择器确认同一条路
        void chooseFrameForTesting(quint64 id);

        /**
         * @brief 改热键等编码的上限
         * @param milliseconds 等待毫秒数
         * @details 用例把它改成 0，就能稳定走到「编码还没完，先用现有帧打开」那条超时分支。
         */
        void setPickerWaitForTesting(int milliseconds);

        /// 当前生效的配置，供用例判时长与帧率是否跟着设置变了
        [[nodiscard]] Core::RewindConfig configForTesting() const;

        /// 连续编码失败次数，供用例判心跳与成功编码是否把它清零
        [[nodiscard]] int consecutiveEncodeFailuresForTesting() const noexcept;

        /// 是否有一帧已提交编码、结果还没回接
        [[nodiscard]] bool encodePendingForTesting() const noexcept;

        /**
         * @brief 往缓冲里塞一段不是编码器产出的字节
         * @return 样本号；缓冲拒绝追加时返回 0
         * @details 正常路径只收编码器自己的产出，选帧解码失败的用例没有别的入口造出坏数据。
         */
        quint64 injectCorruptFrameForTesting();

    signals:
        /// 选中的历史帧已解码：控制器据此进入回溯框选
        void frameChosen(const Base::CapturedImage &frame);

        /// 要说给用户的一句话（由控制器决定用气泡还是落日志）
        void messageForUser(const QString &message, Gui::ToastKind kind);

        /// 托盘那一行读数：是否开启、是否暂停、暂停原因；开启且未暂停时原因为空
        void bufferStatusChanged(bool enabled, bool paused, const QString &reason);

    private:
        /// 内部状态；比对外状态多出选择器与框选两档
        enum class State
        {
            Disabled,          ///< 功能关闭
            Buffering,         ///< 定时抓帧
            PickerOpen,        ///< 选择器打开，缓冲暂停
            CaptureFlow,       ///< 框选/标注/交付进行中，缓冲暂停
            PausedByExclusion, ///< 排除的程序在前台
            PausedByLock,      ///< 已锁屏
            PausedByError,     ///< 连续抓帧或编码失败
        };

        /// 应用一份读好的配置：缓冲容量与节拍一起换
        void applyConfig(const Core::RewindConfig &config);
        /// 切状态：起停定时器、发托盘读数，进入错误暂停时补一句用户提示
        void setState(State state, const QString &reason = {});
        /// 按当前帧率起表
        void startCaptureTimer();
        /// 一次抓帧节拍：解析光标屏、查排除名单、抓帧、判变化、编码或追加心跳
        void onTick();
        /// 把变化帧交给工作线程编码；同一时刻只允许一个
        void startEncode(const Base::CapturedImage &frame, const Core::FrameFingerprint &fingerprint);
        /// 编码成功：拼样本、追加缓冲，必要时打开等待中的选择器
        void appendEncoded(const Core::RewindEncodedFrame &encoded);
        /// 打开选择器并把当前样本摆进去
        void openPicker();
        /// 选中一帧：提交后台解码，解码期间先收起选择器
        void chooseFrame(quint64 id);
        /// 选择器被取消：回到打开前的状态
        void handlePickerDismissed();
        /// 收起选择器后该回哪个状态：暂停原因仍在就留在暂停，界面流程一律回抓帧
        [[nodiscard]] State stateAfterPickerClosed() const noexcept;
        /// 编码完成：成功追加，连续失败暂停，结果过期则丢弃
        void handleEncoded();
        /// 解码完成：成功进框选并发 frameChosen，失败把选择器摆回来
        void handleDecoded();
        /// 锁屏：清空、收起选择器并暂停
        void handleSessionLocked();
        /// 解锁：配置仍开启就恢复抓帧
        void handleSessionUnlocked();
        /// 屏幕增减或分辨率变化：清空并重新解析光标屏
        void handleScreenTopologyChanged();
        /// 盯住一块屏的几何变化
        void watchScreenGeometry(QScreen *screen);
        /// 丢弃在途帧、指纹与选中号，并把失败计数归零
        void resetFrameState();
        /// 排除名单里的程序是否在被抓屏上有可见窗口
        [[nodiscard]] bool exclusionVisible(const QRect &screenRect) const;

        Dependencies                                             m_dependencies;                           ///< 抓帧来源、平台与设置，都不拥有
        Core::RewindConfig                                       m_config;                                 ///< 最近一次应用成功的配置
        Core::FrameRingBuffer                                    m_buffer;                                 ///< 编码后的帧，只在 GUI 线程访问
        QTimer                                                   m_timer;                                  ///< 抓帧节拍；排除暂停期间也继续跑，用来发现程序消失
        QElapsedTimer                                            m_clock;                                  ///< 样本时间的单调来源
        std::unique_ptr<Gui::RewindPickerWindow>                 m_picker;                                 ///< 选择器，首次打开时建，之后复用
        QFutureWatcher<Base::Expected<Core::RewindEncodedFrame>> m_encoderWatcher;                         ///< 编码结果的回接点
        QFutureWatcher<Base::Expected<QImage>>                   m_decoderWatcher;                         ///< 解码结果的回接点
        std::optional<Core::RewindEncodedFrame>                  m_lastEncoded;                            ///< 最近一次成功编码的字节与缩略图，心跳样本复用
        std::optional<Core::FrameFingerprint>                    m_lastFingerprint;                        ///< 与最近一次编码对应的画面指纹
        std::optional<Base::CapturedImage>                       m_pendingFrame;                           ///< 已提交编码、等结果回接的帧
        std::optional<Core::FrameFingerprint>                    m_pendingFingerprint;                     ///< 在途帧的指纹
        std::optional<quint64>                                   m_pendingChosenId;                        ///< 已提交解码、等结果回接的样本号
        qint64                                                   m_pendingMonotonicMs = 0;                 ///< 在途帧的抓帧时刻
        quint64                                                  m_nextSampleId       = 1;                 ///< 下一条样本号，只增不减
        QString                                                  m_screenName;                             ///< 当前抓的屏名，可能为空（离屏平台）
        QRect                                                    m_screenRect;                             ///< 当前屏的逻辑全局矩形
        QRect                                                    m_screenAvailableRect;                    ///< 当前屏的可用区，选择器按它摆放；抓帧仍按整屏
        State                                                    m_state             = State::Disabled;    ///< 当前状态
        State                                                    m_stateBeforePicker = State::Buffering;   ///< 打开选择器前的状态，取消时回这里
        QString                                                  m_stateReason;                            ///< 当前暂停原因，重发状态时沿用
        QString                                                  m_announcedErrorReason;                   ///< 已经弹过的错误原因，从选择器回到同一暂停时不重复弹
        QString                                                  m_reasonBeforePicker;                     ///< 打开选择器前的暂停原因
        int                                                      m_consecutiveFailures            = 0;     ///< 连续抓帧失败次数
        int                                                      m_consecutiveSlowGrabs           = 0;     ///< 连续慢抓次数
        int                                                      m_consecutiveEncodeFailures      = 0;     ///< 连续编码失败次数
        int                                                      m_capturedFrames                 = 0;     ///< 抓帧尝试总数，诊断用
        int                                                      m_pickerWaitMs                   = 300;   ///< 热键等编码完成的上限，用例可改短
        bool                                                     m_openPickerWhenEncoded          = false; ///< 热键等编码完成后再开选择器
        bool                                                     m_screenResolved                 = false; ///< 是否已解析过光标屏；离屏平台屏名为空也要能判首次
        mutable bool                                             m_windowEnumerationFailureLogged = false; ///< 窗口枚举失败只记一次日志
    };
} // namespace GyanisScribe::App
