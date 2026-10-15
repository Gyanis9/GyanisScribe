/**
 * @file QScreenCaptureSource.h
 * @brief 用 QScreen 抓帧的那一份来源实现
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Capture/IScreenCaptureSource.h"

namespace GyanisScribe::Plugins
{

    /**
     * @brief QScreen 抓帧实现
     * @details 跨平台起步用 QScreen::grabWindow：Windows 上走 GDI 那条路，容器里也编得过。
     *          窗口抓取在部分平台上拿到的是空白图——那时返回 PlatformUnsupported 加中文原因，
     *          而不是把空白图当成成功交下去。
     * @warning 必须在 QGuiApplication 存在之后调用，Qt 的屏幕列表在那之前是空的。
     */
    class QScreenCaptureSource : public Core::IScreenCaptureSource
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
         * @brief 抓整块屏幕，见 Core::IScreenCaptureSource::grabScreen()
         * @param screenName 目标屏幕名，空串取光标所在那块
         * @return 整帧，或在屏幕缺失/抓回空图时的 ScreenUnavailable
         */
        [[nodiscard]] Base::Expected<Base::CapturedImage> grabScreen(const QString &screenName) const override;

        /**
         * @brief 抓一个窗口，见 Core::IScreenCaptureSource::grabWindow()
         * @param windowHandle 平台窗口句柄
         * @return 窗口那一帧；句柄失效返回 InvalidArgument，本平台抓不出窗口返回 PlatformUnsupported
         */
        [[nodiscard]] Base::Expected<Base::CapturedImage> grabWindow(quintptr windowHandle) const override;

        /**
         * @brief 取鼠标指针当前的形状与落点，见 Core::IScreenCaptureSource::grabCursor()
         * @param screenName 与同一次 grabScreen() 同一个屏名，空串取光标所在那块
         * @details 重写基类接口：形状在 Windows 上用 GetCursorInfo 拿系统正在画的那枚指针并解出
         *          像素，落点用 Qt 的光标读数折回该屏的逻辑坐标；其余平台保持基类的空值。
         *          Qt 6.11 没有公开 API 能取到系统指针的形状，只能走 Win32 这一层。
         * @return 指针在屏上可见、形状也解得出来时返回；隐藏、解不出或本平台不支持时返回空
         */
        [[nodiscard]] std::optional<Core::CursorSprite> grabCursor(const QString &screenName) const override;
    };

} // namespace GyanisScribe::Plugins
