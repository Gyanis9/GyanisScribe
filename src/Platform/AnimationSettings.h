/**
 * @file AnimationSettings.h
 * @brief 系统「显示动画」开关的查询
 * @author Gyanis
 * @date 2026-10-24
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

namespace GyanisScribe::Platform
{
    /**
     * @brief 系统是否允许播放动画
     * @return Windows 上按 SPI_GETCLIENTAREAANIMATION 回答；查询失败或非 Windows 时按允许处理
     * @details 只回答系统那一档；应用内的测试注入与全局合成在 Gui 的动画开关那一处做。
     */
    [[nodiscard]] bool animationsEnabled();
} // namespace GyanisScribe::Platform
