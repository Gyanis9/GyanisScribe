/**
 * @file PopupSurface.h
 * @brief 浮层窗口的透明表面：让圆角外真的是桌面，而不是黑块
 * @author Gyanis
 * @date 2026-11-14
 * @version 0.1.0
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Geometry/ContinuousCorner.h"

class QWidget;

namespace GyanisScribe::Gui
{
    /**
     * @brief 让浮层窗口带上透明底，并在显示与改尺寸时套一圈连续曲率掩膜
     * @param window 目标顶层窗口；为空时什么也不做
     * @param role 掩膜用的圆角档位
     * @details 提示窗与下拉弹层由 Qt 在显示时现建，透明属性晚一步整窗就落到不透明表面上；
     *          同时 Windows 对这类弹出窗口的透明合成并不可靠，圆角外仍会显出黑块。
     *          掩膜由系统裁掉四角，任何合成路径下都不会再露出黑角。
     */
    void preparePopupSurface(QWidget *window, Core::CornerRole role);
} // namespace GyanisScribe::Gui
