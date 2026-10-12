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
     * @details 判的是「本平台给不给得出这个能力」，不是「本仓现在写了多少」：界面据此把入口标灰或干脆不放，
     *          而不是让用户点了以后才发现没反应。每一项都要有真实消费方——没有读者的位一律不加，
     *          「以后可能用」的理由不成立（能力到用时再补，一次提交就能加上）。
     */
    struct PlatformCapabilities
    {
        bool globalHotkeys     = false; ///< 全局热键：Windows 有 RegisterHotKey，其余平台交 Stub，热键面板按它决定要不要给登记入口
        bool clickThrough      = false; ///< 鼠标穿透：置位后贴图窗口不吃鼠标也不抢焦点
        bool windowDiscovery   = false; ///< 取当前前台窗口：托盘按它决定「截取当前窗口」这一项放不放
        bool windowEnumeration = false; ///< 枚举屏上可见窗口：截图遮罩按它决定要不要给「悬停认出整扇窗口」
        bool uiElements        = false; ///< 光标下的界面元素：Windows 有 UI Automation，截图遮罩按它决定悬停要不要细分到元素一级
        bool autostart         = false; ///< 开机自启动：Windows 写当前用户的 Run 键，其余平台按它把「通用」页那一格灰掉并给出原因
    };

} // namespace GyanisScribe::Platform
