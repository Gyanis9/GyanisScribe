/**
 * @file CaptureService.h
 * @brief 截图编排的实现：抓帧、裁选区、记住上次区域、铺遮罩框选
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Capture/ICaptureService.h"

#include <QPointer>

#include <memory>
#include <vector>

namespace GyanisScribe::Capture
{
    class CaptureOverlay;
    class IScreenCaptureSource;
} // namespace GyanisScribe::Capture

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::Capture
{

    /**
     * @brief 截图服务实现
     * @details 抓帧走注入的 IScreenCaptureSource，选区换算走 ScreenUtils，本类只做编排与记账：
     *          三者都能替身替换，容器里没有真屏幕也跑得动用例。
     * @warning 只在 GUI 线程上使用；跨线程投递到那一层时，CapturedImage 与 Error 要先注册元类型，
     *          否则 QueuedConnection 会在运行期丢消息（Record 与 Ocr 那一批会碰到这条）。
     */
    class CaptureService : public ICaptureService
    {
        Q_OBJECT

    public:
        /**
         * @brief 绑定抓帧来源、配置与平台集成
         * @param source 抓帧来源，生命周期必须长于本服务
         * @param settings 配置存储，用于读写「上次区域」那两项，生命周期必须长于本服务
         * @param integration 平台集成，只用来问「屏上有哪些窗口」，生命周期必须长于本服务
         * @param parent QObject 父对象，默认无
         */
        CaptureService(const IScreenCaptureSource &source, Settings::ISettingsStore &settings, const Platform::IPlatformIntegration &integration, QObject *parent = nullptr);

        /**
         * @brief 析构服务，顺带收掉还开着的遮罩
         */
        ~CaptureService() override;

        CaptureService(const CaptureService &)            = delete;
        CaptureService &operator=(const CaptureService &) = delete;

        /**
         * @brief 抓一块屏幕，见 ICaptureService::captureFullScreen()
         * @param screenName 目标屏幕名，空串取光标所在那块
         */
        void captureFullScreen(const QString &screenName = {}) override;

        /**
         * @brief 抓指定区域，见 ICaptureService::captureRegion()
         * @param logicalRegion 该屏内的逻辑矩形
         * @param screenName 目标屏幕名，空串取光标所在那块
         * @details 重写基类接口：先抓整帧再裁选区，成功后才把选区写进配置——
         *          失败也记的话，一次坏抓图会把用户上次的好选区顶掉。
         */
        void captureRegion(const QRect &logicalRegion, const QString &screenName = {}) override;

        /**
         * @brief 抓一个窗口，见 ICaptureService::captureWindow()
         * @param windowHandle 平台窗口句柄
         */
        void captureWindow(quintptr windowHandle) override;

        /**
         * @brief 复用上次区域，见 ICaptureService::captureLastRegion()
         */
        void captureLastRegion() override;

        /**
         * @brief 铺遮罩进入框选，见 ICaptureService::startInteractive()
         */
        void startInteractive() override;

        /**
         * @brief 收掉遮罩并发 captureCancelled，见 ICaptureService::cancelInteractive()
         */
        void cancelInteractive() override;

    private:
        /**
         * @brief 把一次抓帧结果转成信号
         * @param result 抓帧或裁剪的结果
         */
        void deliver(const Core::Expected<Core::CapturedImage> &result);

        /**
         * @brief 收掉全部遮罩，不发任何信号
         */
        void closeOverlays();

        /**
         * @brief 问平台要这一轮的窗口候选（全局逻辑矩形，按 z 序）
         * @return 平台没这能力、或给不出时返回空表——遮罩就退回「只做手动框选」
         */
        [[nodiscard]] std::vector<QRect> windowCandidates() const;

        const IScreenCaptureSource           &m_source;      ///< 抓帧来源
        Settings::ISettingsStore             &m_settings;    ///< 「上次区域」的读写处
        const Platform::IPlatformIntegration &m_integration; ///< 窗口候选的来源，不拥有
        std::vector<QPointer<CaptureOverlay>> m_overlays;    ///< 在屏的遮罩，每块屏一张；窗口自己析构后要认得空指针
    };

} // namespace GyanisScribe::Capture
