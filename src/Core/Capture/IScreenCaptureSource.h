/**
 * @file IScreenCaptureSource.h
 * @brief 抓帧来源的契约：把「像素从哪来」与「编排怎么走下去」分开
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Capture/CursorPixels.h"
#include "Base/Types.h"

#include <QtGlobal>

#include <QString>

#include <optional>

namespace GyanisScribe::Core
{

    /**
     * @brief 抓帧来源
     * @details 现在这一份是 QScreen::grabWindow；换成 Windows Graphics Capture 或 DXGI 桌面重复时，
     *          新增一份实现即可，编排层与选区换算都不动——那条替换路径就靠这个接口留着。
     * @note 返回的帧里 physical 像素与 logicalSize、devicePixelRatio 三者必须自洽，
     *       下游的裁图与标注全按这一套互指的读数算。
     */
    class IScreenCaptureSource
    {
    public:
        IScreenCaptureSource()                                        = default;
        virtual ~IScreenCaptureSource()                               = default;
        IScreenCaptureSource(const IScreenCaptureSource &)            = delete;
        IScreenCaptureSource &operator=(const IScreenCaptureSource &) = delete;

        /**
         * @brief 抓一块屏幕的全部像素
         * @param screenName 目标屏幕名；空串表示「光标所在那块」
         * @return 成功返回整帧
         * @return 屏幕不存在或抓回空图时返回 ScreenUnavailable，文案点名是哪块屏
         */
        [[nodiscard]] virtual Base::Expected<Base::CapturedImage> grabScreen(const QString &screenName) const = 0;

        /**
         * @brief 抓一个窗口
         * @param windowHandle 平台窗口句柄（Qt 的 WId）
         * @return 成功返回该窗口的帧，逻辑尺寸按所在屏的缩放因子算
         * @return 句柄已失效、或本平台抓不出窗口时返回原因
         */
        [[nodiscard]] virtual Base::Expected<Base::CapturedImage> grabWindow(quintptr windowHandle) const = 0;

        /**
         * @brief 取鼠标指针当前的形状与落点
         * @param screenName 与同一次 grabScreen() 传的同一个屏名，空串表示「光标所在那块」
         * @return 拿得出时返回形状；本平台拿不到、指针此刻没显示、或落点算不出时返回空
         * @note 缺省实现返回空：一份来源没接这一格，编排层就跳过合成并在日志里说明，
         *       而不是画一枚凭空的指针冒充「含指针」。
         */
        [[nodiscard]] virtual std::optional<CursorSprite> grabCursor(const QString &screenName) const
        {
            Q_UNUSED(screenName);
            return std::nullopt;
        }
    };

} // namespace GyanisScribe::Core
