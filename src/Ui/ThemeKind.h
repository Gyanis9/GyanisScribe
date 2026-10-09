/**
 * @file ThemeKind.h
 * @brief 主题档位的取值、文本形式与「跟随系统」的解算规则
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QtGlobal>

#include <string_view>

namespace GyanisScribe::Ui
{

    /**
     * @brief 用户可选的主题档位
     * @details 三档是需求第五节的原样：浅色、深色、跟随系统。枚举成员只追加不改序，
     *          用例按序号钉住了 JSON 与配置里的取值表。
     */
    enum class ThemeKind
    {
        Light,  ///< 浅色主题
        Dark,   ///< 深色主题
        System, ///< 跟随系统的深浅色偏好
    };

    /**
     * @brief 系统回报的深浅色偏好
     * @details 与 ThemeKind 分开是有意的：系统可能根本报不出偏好（无 DeskDock、老 Windows、
     *          容器里的 offscreen 平台），这一档不是「用户选了浅色」。
     */
    enum class SystemScheme
    {
        Light,   ///< 系统当前为浅色
        Dark,    ///< 系统当前为深色
        Unknown, ///< 系统没有可用的偏好回报
    };

    /**
     * @brief 把配置里的文本解析成主题档位
     * @param text 配置存的文本，取值只能是 Light、Dark、System
     * @return 解析出的档位；文本不合法时返回 InvalidArgument 并写清可用的三个取值
     */
    [[nodiscard]] Core::Expected<ThemeKind> parseThemeKind(std::string_view text);

    /**
     * @brief 取主题档位对应的配置文本
     * @param themeKind 主题档位
     * @return 与 parseThemeKind 互指的文本，用于写回配置与设置页下拉
     */
    [[nodiscard]] std::string_view themeKindText(ThemeKind themeKind);

    /**
     * @brief 解算「用户选的档位 + 系统回报」最终该用哪套色
     * @param requestedKind 用户在设置里选的档位
     * @param scheme 系统回报的偏好
     * @return 一定落在 Light 或 Dark：System 档按系统回报解算，回报不可用时兜底成浅色
     * @note 兜底选浅色是刻意的：报不出偏好时深色文字压在白底上是读不出的那种错，浅色反过来不会
     */
    [[nodiscard]] ThemeKind resolveThemeKind(ThemeKind requestedKind, SystemScheme scheme);

    /**
     * @brief 把 Qt 回报的系统偏好翻成本模块的枚举
     * @param scheme Qt 的深浅色偏好
     * @return 对应的 SystemScheme，Qt 的未知档翻成 Unknown
     */
    [[nodiscard]] SystemScheme toSystemScheme(Qt::ColorScheme scheme);

} // namespace GyanisScribe::Ui
