#include "Ui/AppShortcuts.h"

#include "Core/Logger.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"

#include <algorithm>
#include <array>
#include <format>
#include <span>
#include <string>

namespace GyanisScribe::Ui
{
    namespace
    {
        // 标注窗十三档工具的按键。默认那颗与中文名跟 Annotation::kToolTable 是同一份口径，
        // 两处各写一遍就会改出两个说法，所以由 TestAppShortcuts 逐档钉住一致
        constexpr std::array<AppShortcutEntry, 13> kAppShortcutEntries = {
                AppShortcutEntry{"Annotation/Rectangle", "R", "矩形"},   AppShortcutEntry{"Annotation/Ellipse", "E", "椭圆"},
                AppShortcutEntry{"Annotation/Arrow", "A", "箭头"},       AppShortcutEntry{"Annotation/Pen", "P", "画笔"},
                AppShortcutEntry{"Annotation/Highlight", "H", "高亮"},   AppShortcutEntry{"Annotation/Mosaic", "M", "马赛克"},
                AppShortcutEntry{"Annotation/Text", "T", "文字"},        AppShortcutEntry{"Annotation/Number", "N", "序号"},
                AppShortcutEntry{"Annotation/Line", "L", "直线"},        AppShortcutEntry{"Annotation/Blur", "B", "高斯模糊"},
                AppShortcutEntry{"Annotation/Eraser", "X", "橡皮擦"},    AppShortcutEntry{"Annotation/Polyline", "G", "折线"},
                AppShortcutEntry{"Annotation/Magnifier", "F", "放大镜"},
        };
    } // namespace

    std::span<const AppShortcutEntry> appShortcutEntries()
    {
        return kAppShortcutEntries;
    }

    const AppShortcutEntry *appShortcutEntry(const std::string_view token)
    {
        const auto found = std::ranges::find(kAppShortcutEntries, token, &AppShortcutEntry::token);
        return found == kAppShortcutEntries.end() ? nullptr : &*found;
    }

    QString appShortcutKey(const std::string_view token)
    {
        // 前缀 + 标识：一格一签，「分组轮换顺序」那一档用的也是「前缀 + 项」这一形
        return QString::fromStdString(std::string{GyanisScribe::Settings::kAppShortcutPrefix} + std::string{token});
    }

    Hotkey::HotkeyCombination appShortcutValue(const Settings::ISettingsStore &settings, const AppShortcutEntry &entry)
    {
        const auto fallback = Hotkey::HotkeyCombination::parse(entry.defaultKey);
        if (!fallback)
        {
            // 清册自己写错了默认键：这里没有更下一档可回落，报出来并交回「未设」，
            // 让那一档按键按下去没反应，也比替用户猜一颗键安全
            Core::Logger::error(std::format("界面按键清册的默认键「{}」读不出：{}", entry.defaultKey, fallback.error().message));
            return {};
        }

        // 默认值直接给默认按键文本：没填过与填了空串都落回这一格，不会多读一次键
        const auto stored = settings.stringValue(appShortcutKey(entry.token).toStdString(), QString::fromUtf8(entry.defaultKey.data(), static_cast<int>(entry.defaultKey.size())));
        const auto resolved = Hotkey::HotkeyCombination::parse(stored.toStdString());
        if (resolved && resolved->checkUsableInWindow())
        {
            return *resolved;
        }

        Core::Logger::warning(std::format("界面按键「{}」那一格填的是「{}」，读不出或不能当按键：这一档按默认键 {} 走", entry.token, stored.toStdString(), entry.defaultKey));
        return *fallback;
    }

    QString appShortcutText(const Settings::ISettingsStore &settings, const AppShortcutEntry &entry)
    {
        const auto value = appShortcutValue(settings, entry);
        const auto text  = value.toText();
        // 兜底那颗也拿不到文本时（清册写错）交回默认那一串原文：界面上至少还有个能看的东西
        return text.isEmpty() ? QString::fromUtf8(entry.defaultKey.data(), static_cast<int>(entry.defaultKey.size())) : text;
    }

} // namespace GyanisScribe::Ui
