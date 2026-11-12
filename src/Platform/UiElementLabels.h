/**
 * @file UiElementLabels.h
 * @brief UI Automation 控件类型到界面标签的映射
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Platform/IPlatformIntegration.h"

#include <QString>

#include <optional>
#include <vector>

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

    /**
     * @brief 从一条元素链里挑出「光标下最深一级」那一份描述
     * @param chain 由深到浅的元素链，见 IPlatformIntegration::uiElementChainAtPoint()
     * @return 链首那一级的描述；链为空、或链首的名字 trimmed() 后为空时返回空的 optional
     * @details 「没有元素」这一出口只有这一处判定：链为空与链首无名同样收场，
     *          调用方不必各自再写一遍、各自漏掉其中一种。链首没有名字时整级都交不出去——
     *          交回一条只有控件类型的描述，看着像是真认出了什么。
     */
    [[nodiscard]] std::optional<UiElementInfo> headElementOf(const std::vector<UiElementInfo> &chain);

} // namespace GyanisScribe::Platform
