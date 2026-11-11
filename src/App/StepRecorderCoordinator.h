/**
 * @file StepRecorderCoordinator.h
 * @brief 步骤记录协调器：事件源起停、防抖抓屏、窗口信息、暂停、上限与首次说明
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Steps/StepSession.h"
#include "Gui/Controls/Toast.h"
#include "Platform/IPlatformIntegration.h"
#include "Platform/IStepEventSource.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QScreen;
class QWidget;

namespace GyanisScribe::Core
{
    class IScreenCaptureSource;
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Gui
{
    class StepRecorderHud;
    class ThemeManager;
} // namespace GyanisScribe::Gui

namespace GyanisScribe::App
{
    /// 点击到抓屏之间等待的毫秒数：让钩子回调先退出，目标界面的菜单或页面切换也在这段时间里落定
    inline constexpr int kCaptureDelayMs = 250;

    /// 同一元素重复点击的合并时间窗；判定本身在 Core::StepSession::shouldMerge，这里只是对外的同一档读数
    inline constexpr int kClickMergeWindowMs = Core::kClickMergeWindowMs;

    /**
     * @brief 步骤记录协调器的外部依赖
     * @details 全部只引用不拥有；两个可调函数留空即用生产实现，替身与用例按需替换。
     */
    struct StepRecorderDependencies
    {
        Core::ISettingsStore           &settings;      ///< 首次说明与后续设置
        Platform::IPlatformIntegration &integration;   ///< 能力表、前台窗口与界面元素描述
        Core::IScreenCaptureSource     &captureSource; ///< 整屏抓帧来源
        Gui::ThemeManager              &theme;         ///< 控制条取色

        /// 事件源工厂；空表示 Platform::createStepEventSource
        std::function<std::unique_ptr<Platform::IStepEventSource>()> eventSourceFactory;

        /// 首次说明；空表示 Gui::StepConsentDialog，返回 true 才开始记录
        std::function<bool(QWidget *)> consentProvider;

        /// 会话临时目录的上一级；每次会话在它下面按时刻建一个子目录
        std::filesystem::path tempRoot;
    };

    /**
     * @brief 一次操作步骤记录的编排者
     * @details 平台事件源只在录制会话内存在：开始时装钩子，暂停与停止时真卸钩；事件先攒进待处理槽，
     *          延迟之后才抓屏，让点击后的菜单与页面切换落定。界面元素在事件到达时立刻问，
     *          抓屏之后再问会读到已经变样的界面。
     * @note 全部状态只在 GUI 线程访问；停止后会话与临时截图留给审核，等下一次开始或析构回收。
     */
    class StepRecorderCoordinator : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造协调器并清理过期会话目录
         * @param dependencies 外部依赖、两个可替换入口与临时目录根
         * @param parent Qt 父对象
         * @details 构造即按 tempRoot 回收超过 24 小时的旧会话目录，免得一次没审完的记录一直占着临时空间。
         */
        explicit StepRecorderCoordinator(StepRecorderDependencies dependencies, QObject *parent = nullptr);

        /**
         * @brief 析构：摘钩子、收起控制条并删掉本次会话的临时目录
         */
        ~StepRecorderCoordinator() override;

        /**
         * @brief 开始或停止录制
         * @details 没在录时先过能力与首次说明，再建新会话、装钩子、显示控制条；在录时等同停止。
         *          停止后不打开审核窗，步骤留在会话里等后续处理。
         */
        void toggleRecording();

        /// 用例入口：翻转暂停态，与按控制条上的暂停/继续同一个入口
        void togglePauseForTesting();

        /// 是否正在录制
        [[nodiscard]] bool isRecording() const noexcept;

        /// 是否处于暂停
        [[nodiscard]] bool isPaused() const noexcept;

        /// 当前会话已记下的步数
        [[nodiscard]] int stepCount() const noexcept;

        /**
         * @brief 用例入口：停掉延迟计时并立刻提交待处理事件
         * @details 没有待处理事件时什么也不做；与真实 250 毫秒到点是同一条提交路径。
         */
        void flushPendingForTesting();

        /// 当前会话，供用例与后续审核读取；尚未开始记录时步数为 0
        [[nodiscard]] Core::StepSession *sessionForTesting() noexcept;

        /// 控制条；尚未开始过录制时为 nullptr
        [[nodiscard]] Gui::StepRecorderHud *hudForTesting() noexcept;

    signals:
        /// 要说给用户的一句话（由控制器决定用气泡还是落日志）
        void messageForUser(const QString &message, Gui::ToastKind kind);

        /// 录制状态与步数变了：开始、停止、暂停、继续与每追加一步都发一次
        void stateChanged(bool recording, bool paused, int steps);

        /// 一次正常停止，带上本次会话的步数；运行期故障走 messageForUser，不经过这一条
        void stopped(int steps);

    private:
        /// 攒在待处理槽里的一次输入事件与它到达时取到的窗口信息
        struct PendingEvent
        {
            Platform::StepInputEvent               event;       ///< 事件本体
            std::optional<Platform::UiElementInfo> element;     ///< 点击到达时取到的界面元素；窗口切换为空
            QString                                windowTitle; ///< 到达时的顶层窗口标题
            QString                                processName; ///< 到达时的顶层窗口进程名
        };

        /// 过能力与首次说明，建新会话并装上事件源
        void startRecording();

        /// 丢弃待处理事件、卸钩子、收起控制条并广播停止
        void stopRecording();

        /// 翻转暂停：暂停真卸钩子，继续重新装
        void togglePause();

        /// 事件源投递的一条输入事件：攒进待处理槽并重启延迟计时
        void handleEvent(const Platform::StepInputEvent &event);

        /// 事件源运行期故障：结束本次记录、清掉临时目录并告诉用户
        void handleSourceFailed(const Base::Error &error);

        /// 延迟到点：按待处理事件抓屏、拼步骤、合并并追加
        void commitPending();

        /// 丢掉待处理事件并停表
        void clearPending();

        /// 首次说明是否已同意；同意时把状态写回配置
        [[nodiscard]] bool ensureConsent();

        /// 首次创建控制条并接上它的两个按钮
        void ensureHud();

        /// 把控制条摆到主屏可用区顶部中央并显示
        void showHud();

        /// 本程序自己窗口的句柄，交给界面元素描述时排除
        [[nodiscard]] std::vector<quintptr> ownWindowHandles() const;

        /**
         * @brief 按待处理事件选一块屏
         * @param pending 待处理事件
         * @param window 窗口切换时从 screenWindows 找到的窗口，找不到为空
         * @return 命中的屏；一块都算不出时回落主屏，可能为空
         */
        [[nodiscard]] QScreen *resolveScreen(const PendingEvent &pending, std::optional<Platform::ScreenWindow> &window) const;

        /**
         * @brief 补齐窗口标题与进程
         * @param pending 待处理事件，读它到达时缓存的值
         * @param window 窗口切换时找到的窗口快照，可为空
         * @param step 正在拼装的步骤，就地填两个字段
         */
        void fillWindowIdentity(const PendingEvent &pending, const std::optional<Platform::ScreenWindow> &window, Core::RecordedStep &step) const;

        /// 抓一张图；控制条先收起再抓，抓完恢复
        [[nodiscard]] Base::Expected<Base::CapturedImage> captureScreen(const QString &screenName);

        StepRecorderDependencies                    m_dependencies;      ///< 外部依赖，都不拥有
        std::unique_ptr<Platform::IStepEventSource> m_source;            ///< 事件源，只在录制会话内运行
        Core::StepSession                           m_session;           ///< 本次会话的步骤与临时目录
        std::unique_ptr<Gui::StepRecorderHud>       m_hud;               ///< 控制条，首次开始时建
        QTimer                                      m_pendingTimer;      ///< 延迟抓屏的单次计时器
        std::optional<PendingEvent>                 m_pending;           ///< 尚未提交的事件，新事件直接替换
        bool                                        m_recording = false; ///< 是否正在录制
        bool                                        m_paused    = false; ///< 是否处于暂停
    };

} // namespace GyanisScribe::App
