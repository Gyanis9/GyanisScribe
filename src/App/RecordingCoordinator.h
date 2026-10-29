/**
 * @file RecordingCoordinator.h
 * @brief 录屏这一族从控制器里搬出来的落脚处
 * @author Gyanis
 * @date 2026-10-16
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Gui/Controls/Toast.h"

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>

#include <memory>

class QTimer;

namespace GyanisScribe::Core
{
    class IScreenCaptureSource;
    class ISettingsStore;
    struct RecorderConfig;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    class IconProvider;
    class RecordingHud;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::Plugins
{
    class FfmpegRecordingEncoder;
    class RecordingService;
} // namespace GyanisScribe::Plugins

namespace GyanisScribe::App
{

    /**
     * @brief 录屏一轮的整套动作：配置取自设置 → 起服务 → 控制条与倒计时 → 收尾交成品
     * @details 只做录屏自己的事：托盘那一行读数走 recordingStatusChanged，成品归档走 finished，
     *          要说给用户的话走 messageForUser——三样都交回控制器，它才知道该接到谁身上。
     */
    class RecordingCoordinator : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造
         * @param captureSource 抓帧来源，交给录屏服务
         * @param integration 平台能力（前台窗口那一档录窗口要用）
         * @param settings 录制参数与输出目录从这里读
         * @param icons 控制条的图标出口
         * @param parent Qt 父对象
         */
        RecordingCoordinator(GyanisScribe::Core::IScreenCaptureSource &captureSource, GyanisScribe::Platform::IPlatformIntegration &integration,
                             GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Gui::IconProvider &icons, QObject *parent = nullptr);

        /**
         * @brief 构造：录屏服务由调用方交进来
         * @param captureSource 抓帧来源，交给录屏服务
         * @param integration 平台能力（前台窗口那一档录窗口要用）
         * @param settings 录制参数与输出目录从这里读
         * @param icons 控制条的图标出口
         * @param service 录屏服务本体，所有权转给协调器
         * @param parent Qt 父对象
         * @note 这一支不建编码器：service 拿着的编码器与抓帧来源都要活过本对象。
         */
        RecordingCoordinator(GyanisScribe::Core::IScreenCaptureSource &captureSource, GyanisScribe::Platform::IPlatformIntegration &integration,
                             GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Gui::IconProvider &icons, std::unique_ptr<GyanisScribe::Plugins::RecordingService> service,
                             QObject *parent = nullptr);

        /**
         * @brief 析构
         * @note 声明在头、定义在 .cpp：两个成员的唯一指针各指着一个只前向声明的类型
         *       （编码器与服务），析构要在类型完整的 TU 里实例化——否则别的 TU 里
         *       内联析构会让 clang/GCC 报 sizeof 应用在不完整类型上（MSVC 默认放过，口径不一）。
         */
        ~RecordingCoordinator() override;

        /**
         * @brief 一次按下：空闲时开始这一轮（含倒计时与控制条），在录或暂停时收尾并保存
         * @details 两个方向合成一个入口，是因为热键、托盘菜单与控制条上的停止键按的都是同一个键，
         *          三处各写一份「现在该开始还是该停」就会三处各有一套判据。
         */
        void toggle();

        /// 取消这一轮：临时帧与中间音频都删掉，不出成品（成功那一路由服务的 aborted 信号回到 handleAborted，收尾只有一处）
        void cancel();

        /**
         * @brief 控制条现在是不是摆在屏幕上
         * @return true 表示控制条可见；收起或关闭之后返回 false
         */
        [[nodiscard]] bool isHudVisible() const noexcept;

    signals:
        /// 要说给用户的一句话（由控制器决定用气泡还是落日志）
        void messageForUser(const QString &message, GyanisScribe::Gui::ToastKind kind);
        /// 托盘那一行读数：秒数与是否暂停；-1 表示没在录（交还给贴图张数那一档）
        void recordingStatusChanged(int seconds, bool paused);
        /// 成品落盘：控制器据此记进历史（尺寸取这一轮自己那份，见 handleFinished 的注释）
        void finished(const QString &outputPath, const QSize &frameSize);

    private:
        /// 把服务的信号接到本类的收尾口上：两个构造共用这一份连接
        void connectService();

        /// 按设置与当前那一块屏拼一份录制配置；读不出或做不出来的那几档一律交回原因，不替用户挑一档
        [[nodiscard]] GyanisScribe::Base::Expected<GyanisScribe::Core::RecorderConfig> makeConfig() const;
        void                                                                           showHud();
        void                                                                           hideHud();
        void                                                                           updateHud();

    protected:
        /**
         * @brief 这一轮录哪块屏
         * @return 先按光标位置取屏，光标不在任何一块屏上时退回主屏；一块屏都没有时返回空串
         * @details 生产实现跟着光标走，多屏机器上人在副屏干活时录的才是眼前那块。
         *          留成可覆写只为用例：离屏平台给不出屏名，而录屏参数必须带屏名。
         */
        [[nodiscard]] virtual QString recordingScreenName() const;

    private:
        /**
         * @brief 编码完成：收起控制条、按需报一句没带上声音、把成品交回控制器归档
         * @param outputPath 成品路径（显示文本）
         * @param frameSize 这一轮的录屏区尺寸
         */
        void handleFinished(const QString &outputPath, const QSize &frameSize);

        /// 录屏中止：收起控制条并把原因说给用户
        void handleFailed(const QString &reason);

        /// 取消完成：收起控制条并说清「这一段没有成品」
        void handleAborted();

        GyanisScribe::Core::IScreenCaptureSource                      &m_captureSource;                ///< 抓帧来源，不拥有
        GyanisScribe::Platform::IPlatformIntegration                  &m_integration;                  ///< 平台能力，不拥有
        GyanisScribe::Core::ISettingsStore                            &m_settings;                     ///< 录制参数来源，不拥有
        GyanisScribe::Gui::IconProvider                               &m_icons;                        ///< 控制条图标出口，不拥有
        std::shared_ptr<GyanisScribe::Plugins::FfmpegRecordingEncoder> m_encoder;                      ///< 编码那一步（ffmpeg 子进程），与服务共享所有权
        std::unique_ptr<GyanisScribe::Plugins::RecordingService>       m_service;                      ///< 录屏服务，一轮一个会话
        QPointer<GyanisScribe::Gui::RecordingHud>                      m_hud;                          ///< 录屏控制条，一轮一个、收起即销毁
        QTimer                                                        *m_ticker             = nullptr; ///< 每秒推一次读数；随控制条一起起停
        int                                                            m_countdownRemaining = 0;       ///< 倒计时剩余整秒，推给控制条的大字
    };

} // namespace GyanisScribe::App
