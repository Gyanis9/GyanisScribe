/**
 * @file PlatformCapabilities.h
 * @brief 平台能力表：哪些功能在本平台有真实后端
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

namespace GyanisScribe::Platform
{

    /**
     * @brief 平台能力位
     * @details 判的是「本平台给不给得出这个能力」，不是「本仓现在写了多少」：界面据此把入口标灰并给中文原因，
     *          而不是让用户点了以后才发现没反应。每一项都要有真实消费方，缺能力时走降级提示而不是静默成功。
     */
    struct PlatformCapabilities
    {
        bool globalHotkeys   = false; ///< 全局热键：Windows 有 RegisterHotKey，其余平台交 Stub
        bool clickThrough    = false; ///< 鼠标穿透：置位后贴图窗口不吃鼠标也不抢焦点
        bool screenRecording = false; ///< 录屏采集：需要平台级端点重复或 loopback
        bool audioLoopback   = false; ///< 系统声音录制：WASAPI loopback 一档
        bool systemOcr       = false; ///< 系统自带 OCR：Windows.Media.Ocr 一档
    };

} // namespace GyanisScribe::Platform
