/**
 * @file NamingRules.h
 * @brief 成品文件名模板的读取与校验
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QString>

namespace GyanisScribe::Settings
{
    class ISettingsStore;

    /**
     * @brief 从设置里读出成品文件名模板
     * @param settings 配置来源
     * @return 已校验的模板；留空或不合法时交回默认档，不合法这一路的原因写进日志
     * @details 四个写盘口（截图、贴图保存、识别导出、录屏成品）都从这里取：读取与回落只此一处，
     *          与保存格式、质量那几格同一条约定——读不出就记一条并按默认档走，不各写一份回退。
     */
    [[nodiscard]] QString nameTemplateOrDefault(const ISettingsStore &settings);

} // namespace GyanisScribe::Settings
