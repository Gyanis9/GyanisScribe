/**
 * @file QScreenCaptureSource.h
 * @brief 用 QScreen 抓帧的那一份来源实现
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Capture/IScreenCaptureSource.h"

namespace GyanisScribe::Capture
{

    /**
     * @brief QScreen 抓帧实现
     * @details 跨平台起步用 QScreen::grabWindow：Windows 上走 GDI 那条路，容器里也编得过。
     *          窗口抓取在部分平台上拿到的是空白图——那时返回 PlatformUnsupported 加中文原因，
     *          而不是把空白图当成成功交下去。
     * @warning 必须在 QGuiApplication 存在之后调用，Qt 的屏幕列表在那之前是空的。
     */
    class QScreenCaptureSource : public IScreenCaptureSource
    {
    public:
        /**
         * @brief 构造抓帧来源
         */
        QScreenCaptureSource() = default;

        /**
         * @brief 析构抓帧来源
         */
        ~QScreenCaptureSource() override = default;

        /**
         * @brief 抓整块屏幕，见 IScreenCaptureSource::grabScreen()
         * @param screenName 目标屏幕名，空串取光标所在那块
         * @return 整帧，或在屏幕缺失/抓回空图时的 ScreenUnavailable
         */
        [[nodiscard]] Core::Expected<Core::CapturedImage> grabScreen(const QString &screenName) const override;

        /**
         * @brief 抓一个窗口，见 IScreenCaptureSource::grabWindow()
         * @param windowHandle 平台窗口句柄
         * @return 窗口那一帧；句柄失效返回 InvalidArgument，本平台抓不出窗口返回 PlatformUnsupported
         */
        [[nodiscard]] Core::Expected<Core::CapturedImage> grabWindow(quintptr windowHandle) const override;
    };

} // namespace GyanisScribe::Capture
