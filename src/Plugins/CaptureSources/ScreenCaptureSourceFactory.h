/**
 * @file ScreenCaptureSourceFactory.h
 * @brief 抓帧来源的选型：能拿到哪一套原语就用哪一套
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Capture/IScreenCaptureSource.h"

#include <memory>

namespace GyanisScribe::Plugins
{

    /**
     * @brief 建一份抓帧来源
     * @return Windows 上能建起 DXGI 桌面重复时交那一份，否则交 Qt 的 grabWindow 那一份
     * @details 选中的是哪一套会记一条日志：用户报「录屏掉帧」时，第一件要知道的就是这台机器
     *          当时用的哪套原语——两套原语的抓帧成本差着一个量级（读数见 README 性能实测）。
     */
    [[nodiscard]] std::unique_ptr<Core::IScreenCaptureSource> createScreenCaptureSource();

} // namespace GyanisScribe::Plugins
