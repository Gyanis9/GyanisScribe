/**
 * @file CornerStyle.h
 * @brief 主题相关的圆角描边宽度
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Gui/Theme/ThemeKind.h"

namespace GyanisScribe::Gui
{

    /**
     * @brief 当前主题的可见圆角描边宽度
     * @param kind 已解算的主题档
     * @return 高对比度为 2，其余为 1
     * @note QSS 只保留同宽的透明边框占位，可见描边由自绘代码按这个值画
     */
    [[nodiscard]] int themeBorderWidth(ThemeKind kind) noexcept;

} // namespace GyanisScribe::Gui
