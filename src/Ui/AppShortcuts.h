/**
 * @file AppShortcuts.h
 * @brief 界面按键的清册：哪些裸键可以换、默认是哪一颗、换过之后存在哪一格
 * @author Gyanis
 * @date 2026-10-14
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Hotkey/HotkeyCombination.h"

#include <QString>

#include <span>
#include <string_view>

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Ui
{

    /**
     * @brief 一档可以换键的界面动作
     * @details 三格都是数据：标识进配置键名，默认按键与中文名给设置页那一行用。
     *          动作本身不在这里——按键分派住在各自的模块，清册只管「这一档现在认哪颗键」。
     */
    struct AppShortcutEntry
    {
        std::string_view token;      ///< 配置键名里的标识，形如 Annotation/Rectangle
        std::string_view defaultKey; ///< 没人改过时的按键文本，取 HotkeyCombination 那一形（如 R、F5）
        std::string_view label;      ///< 设置页这一行的名字
    };

    /**
     * @brief 全部可换键的界面动作，顺序就是设置页上那一段的顺序
     * @return 清册本体
     * @note 想加一档就在这里多一行，再把那个模块的分派改成按 token 读键；
     *       设置页与判据都按这份表数条数，不必再改第二处。
     */
    [[nodiscard]] std::span<const AppShortcutEntry> appShortcutEntries();

    /**
     * @brief 按标识找清册里的那一档
     * @param token 配置键名里的标识
     * @return 指向清册本体的那一行（生命周期同进程）；表里没有时返回空指针
     * @note 给「只有标识、手上没有整行」的调用方用：工具栏与按键分派按 kToolTable 的标识列找键。
     */
    [[nodiscard]] const AppShortcutEntry *appShortcutEntry(std::string_view token);

    /**
     * @brief 标识对应的配置键
     * @param token 清册里的标识
     * @return 形如 Ui/Shortcut/Annotation/Rectangle 的键名
     */
    [[nodiscard]] QString appShortcutKey(std::string_view token);

    /**
     * @brief 这一档现在认的那个键
     * @param settings 配置存储
     * @param entry 清册里的哪一档
     * @return 用户填过且读得出的那一键；没填过、或填的那串读不出时返回默认按键
     * @warning 填歪的那一串不拦调用方：读不出的原因进日志，界面那一格仍显示原文，
     *            用户看得见自己敲了什么。分派按默认键走，不按半截文本猜一个近似的键。
     */
    [[nodiscard]] Hotkey::HotkeyCombination appShortcutValue(const Settings::ISettingsStore &settings, const AppShortcutEntry &entry);

    /**
     * @brief 界面上该提示的那颗键
     * @param settings 配置存储
     * @param entry 清册里的哪一档
     * @return 现成的按键文本（如「R」），与 appShortcutValue() 同解
     */
    [[nodiscard]] QString appShortcutText(const Settings::ISettingsStore &settings, const AppShortcutEntry &entry);

} // namespace GyanisScribe::Ui
