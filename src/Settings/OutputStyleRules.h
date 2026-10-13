/**
 * @file OutputStyleRules.h
 * @brief 成品装饰参数（边框宽、边框色、投影宽）的读取与回落
 * @author Gyanis
 * @date 2026-10-13
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/OutputDecoration.h"

namespace GyanisScribe::Settings
{
    class ISettingsStore;

    /**
     * @brief 从设置里读出一档成品装饰的参数
     * @param settings 配置来源
     * @return 三格都在合法区间内的参数；哪一格读不出就按那一格的默认档继续，原因写进日志
     * @details 三格共用一个出口：画布尺寸与绘制必须拿到同一份参数，一处读到新值、另一处读到旧值会画出偏格子的边框。
     *          坏值按默认档继续而不拒掉这一次输出——一个填错的宽度不该把已经做好的成品丢在外面。
     */
    [[nodiscard]] Core::OutputDecorationStyle readOutputDecorationStyle(const ISettingsStore &settings);

} // namespace GyanisScribe::Settings
