/**
 * @file AppController.h
 * @brief 把各模块接起来的那一层：信号槽集中在这里，模块之间不互相 include 界面
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"
#include "Hotkey/IHotkeyManager.h"

#include <QObject>

#include <memory>

namespace GyanisScribe::Capture
{
    class CaptureService;
    class IScreenCaptureSource;
} // namespace GyanisScribe::Capture

namespace GyanisScribe::Annotation
{
    class AnnotationEngine;
    class AnnotationWindow;
} // namespace GyanisScribe::Annotation

namespace GyanisScribe::Ocr
{
    class IOcrEngine;
} // namespace GyanisScribe::Ocr

namespace GyanisScribe::Pin
{
    class PinManager;
} // namespace GyanisScribe::Pin

namespace GyanisScribe::Record
{
    class FfmpegRecordingEncoder;
    struct RecorderConfig;
    class RecordingService;
} // namespace GyanisScribe::Record

namespace GyanisScribe::Hotkey
{
    class HotkeyManager;
} // namespace GyanisScribe::Hotkey

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::Settings
{
    class SettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Ui
{
    class IconProvider;
    class ThemeManager;
} // namespace GyanisScribe::Ui

namespace GyanisScribe::App
{

    /**
     * @brief 核心控制器
     * @details 依赖在这里创建、连接也在这里挂：Capture 不认识 Hotkey，Hotkey 不认识 Settings，
     *          三者只经由本控制器串起来。后续加贴图、录屏、识别时同样只补连接，不改各模块。
     */
    class AppController : public QObject
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造控制器并创建各模块实例
         * @param parent QObject 父对象，默认无
         * @note 要在 QApplication 存在之后构造：配置、屏幕列表与热键后端都要靠应用对象
         */
        explicit AppController(QObject *parent = nullptr);

        /**
         * @brief 析构控制器，按创建的反向顺序交还各模块
         */
        ~AppController() override;

        AppController(const AppController &)            = delete;
        AppController &operator=(const AppController &) = delete;

        /**
         * @brief 登记全局热键
         * @return 成功返回本轮登记结果；热键一项都没登记上也返回成功结果，失败项在报告里
         * @note 托盘、设置页与热键重载都调这一个，避免出现第二套登记路径
         */
        [[nodiscard]] Core::Expected<Hotkey::HotkeyReloadReport> registerHotkeys();

        /**
         * @brief 进入截图框选
         */
        void startCapture();

        /**
         * @brief 录屏热键：没在录就开始整屏录，正在录就停下并编码
         * @details 独立的录屏界面还没做，先做成一个开关：再按一次就是「停止并保存」，
         *          用户不必在屏幕另一头找停止按钮。
         */
        void toggleRecording();

        /**
         * @brief 配置存储，供设置页与后续模块取用
         * @return 本控制器持有的那一份，所有权不在调用方
         */
        [[nodiscard]] Settings::SettingsStore &settings() const noexcept;

        /**
         * @brief 主题管理器
         * @return 控制器创建的那一个，界面从这里取当前生效的色板与图标描边色
         */
        [[nodiscard]] GyanisScribe::Ui::ThemeManager &theme() const noexcept;

        /**
         * @brief 图标供给器
         * @return 全应用共用的那一个，描边色已跟着主题接好
         */
        [[nodiscard]] GyanisScribe::Ui::IconProvider &icons() const noexcept;

    private:
        /**
         * @brief 抓到一帧之后的收尾：开启一轮标注
         * @param image 抓屏产物
         * @details 截完不再直接进剪贴板：标注窗的「复制」就是原来的那一步，只是多了先标一笔的机会
         */
        void handleCaptured(const Core::CapturedImage &image);

        /// 标注窗请求复制
        void handleCopyRequested(const QImage &image);

        /// 拼这一轮录屏的参数：屏幕名与输出目录都从这里取
        [[nodiscard]] Core::Expected<Record::RecorderConfig> makeRecordingConfig() const;

        /// 录屏结束：把成品路径落到日志
        void handleRecordingFinished(const QString &outputPath);

        /// 录屏中止：把原因落到日志
        void handleRecordingFailed(const QString &reason);
        /// 标注窗请求存文件
        void handleSaveRequested(const QImage &image);
        /// 标注窗请求贴图
        void handlePinRequested(const QImage &image);
        /// 标注窗请求识别文字
        void handleOcrRequested(const QImage &image);

        std::unique_ptr<Settings::SettingsStore>        m_settings;         ///< 配置存储
        std::unique_ptr<Platform::IPlatformIntegration> m_integration;      ///< 平台集成
        std::unique_ptr<Capture::IScreenCaptureSource>  m_captureSource;    ///< 抓帧来源
        std::unique_ptr<Capture::CaptureService>        m_capture;          ///< 截图编排
        std::unique_ptr<Hotkey::HotkeyManager>          m_hotkeys;          ///< 热键登记与触发
        std::unique_ptr<GyanisScribe::Ui::ThemeManager> m_theme;            ///< 主题档位与样式表
        std::unique_ptr<GyanisScribe::Ui::IconProvider> m_icons;            ///< 图标供给器，描边色跟主题
        std::unique_ptr<Annotation::AnnotationEngine>   m_annotation;       ///< 标注引擎，跨轮次保留（载入新帧会清空）
        std::unique_ptr<Annotation::AnnotationWindow>   m_annotationWindow; ///< 标注窗口，复用同一把工具栏
        std::unique_ptr<Pin::PinManager>                m_pins;             ///< 贴图管理器
        std::unique_ptr<Ocr::IOcrEngine>                m_ocr;              ///< 识别后端，按设置与平台能力选一次
        std::unique_ptr<Record::FfmpegRecordingEncoder> m_recorder;         ///< 录屏的编码步
        std::unique_ptr<Record::RecordingService>       m_recording;        ///< 录屏服务，一轮一个会话
        Core::CapturedImage                             m_lastCapture;      ///< 本轮抓到的帧，贴图与识别要用它定位
        QRect                                           m_lastSelection;    ///< 本轮选区（全局逻辑坐标），标注窗与贴图的位置来源
    };

} // namespace GyanisScribe::App
