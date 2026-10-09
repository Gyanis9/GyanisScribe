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
         * @brief 配置存储，供设置页与后续模块取用
         * @return 本控制器持有的那一份，所有权不在调用方
         */
        [[nodiscard]] Settings::SettingsStore &settings() const noexcept;

    private:
        /**
         * @brief 抓到一帧之后的收尾：交剪贴板并记日志
         * @param image 抓屏产物
         */
        void handleCaptured(const Core::CapturedImage &image);

        std::unique_ptr<Settings::SettingsStore>        m_settings;      ///< 配置存储
        std::unique_ptr<Platform::IPlatformIntegration> m_integration;   ///< 平台集成
        std::unique_ptr<Capture::IScreenCaptureSource>  m_captureSource; ///< 抓帧来源
        std::unique_ptr<Capture::CaptureService>        m_capture;       ///< 截图编排
        std::unique_ptr<Hotkey::HotkeyManager>          m_hotkeys;       ///< 热键登记与触发
    };

} // namespace GyanisScribe::App
