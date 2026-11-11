/**
 * @file UiElementLabels.h
 * @brief UI Automation 控件类型到界面标签的映射
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QString>

namespace GyanisScribe::Platform
{

    /**
     * @brief 把 UI Automation 的控件类型 id 换成界面用中文
     * @param uiaControlTypeId UIA_*ControlTypeId 那一组常量之一
     * @return 对应的中文标签（如「按钮」「编辑框」）；不认识或读不到时返回「控件」
     * @details 入参用 int 而不是 UIA 的枚举：本头在非 Windows 上也编，不能把 UIAutomation 的头带进来。
     *          标签走 Base::uiText，翻译目录按源码里的中文归口。
     */
    [[nodiscard]] QString uiControlTypeLabel(int uiaControlTypeId);

} // namespace GyanisScribe::Platform
