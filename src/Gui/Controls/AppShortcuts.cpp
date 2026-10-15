#include "Gui/Controls/AppShortcuts.h"

#include "Base/Logger.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"

#include <algorithm>
#include <array>
#include <format>
#include <span>
#include <string>

namespace GyanisScribe::Gui
{
    namespace
    {
        // 可换键的界面动作。默认那颗与中文名与各自模块的分派同解：标注窗那十三档对齐 Gui::kToolTable
        // （由 TestAnnotationToolbar 的独立清单钉住），遮罩与贴图那十二颗对齐两边的按键判据。
        // 同一窗口里两档不许共用一颗键；不同窗口撞字母是常态（遮罩的 V 复制尺寸、贴图的 V 上下翻转，各管各的）
        constexpr std::array<AppShortcutEntry, 25> kAppShortcutEntries = {
                // 标注窗：切到哪一档工具，默认取英文工具名首字母，橡皮擦的 E 被椭圆占了就用中文首字母 X
                AppShortcutEntry{"Annotation/Rectangle", "R", "矩形"},
                AppShortcutEntry{"Annotation/Ellipse", "E", "椭圆"},
                AppShortcutEntry{"Annotation/Arrow", "A", "箭头"},
                AppShortcutEntry{"Annotation/Pen", "P", "画笔"},
                AppShortcutEntry{"Annotation/Highlight", "H", "高亮"},
                AppShortcutEntry{"Annotation/Mosaic", "M", "马赛克"},
                AppShortcutEntry{"Annotation/Text", "T", "文字"},
                AppShortcutEntry{"Annotation/Number", "N", "序号"},
                AppShortcutEntry{"Annotation/Line", "L", "直线"},
                AppShortcutEntry{"Annotation/Blur", "B", "高斯模糊"},
                AppShortcutEntry{"Annotation/Eraser", "X", "橡皮擦"},
                AppShortcutEntry{"Annotation/Polyline", "G", "折线"},
                AppShortcutEntry{"Annotation/Magnifier", "F", "放大镜"},
                // 截图遮罩：这几颗裸键管的是这一块选区怎么框、怎么交出去
                AppShortcutEntry{"Mask/AspectRatio", "A", "锁定长宽比"},
                AppShortcutEntry{"Mask/SizeEditor", "S", "开尺寸输入框"},
                AppShortcutEntry{"Mask/RegionCycle", "R", "挑最近框过的选区"},
                AppShortcutEntry{"Mask/CopySize", "V", "复制选区尺寸"},
                AppShortcutEntry{"Mask/ColorFormat", "C", "颜色写法换一种"},
                AppShortcutEntry{"Mask/PointerToggle", "P", "这一张含不含鼠标指针"},
                // 贴图窗：转正那一颗带一个按住 Shift 的反向档
                AppShortcutEntry{"Pin/Rotate", "R", "转正一挡"},
                AppShortcutEntry{"Pin/FlipHorizontal", "H", "左右翻转"},
                AppShortcutEntry{"Pin/FlipVertical", "V", "上下翻转"},
                AppShortcutEntry{"Pin/Gray", "G", "灰度显示"},
                AppShortcutEntry{"Pin/Invert", "I", "反色"},
                AppShortcutEntry{"Pin/Thumbnail", "T", "缩到小图 / 还原"},
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
        return QString::fromStdString(std::string{GyanisScribe::Core::kAppShortcutPrefix} + std::string{token});
    }

    Core::HotkeyCombination appShortcutValue(const Core::ISettingsStore &settings, const AppShortcutEntry &entry)
    {
        const auto fallback = Core::HotkeyCombination::parse(entry.defaultKey);
        if (!fallback)
        {
            // 清册自己写错了默认键：这里没有更下一档可回落，报出来并交回「未设」，
            // 让那一档按键按下去没反应，也比替用户猜一颗键安全
            Base::Logger::error(std::format("界面按键清册的默认键「{}」读不出：{}", entry.defaultKey, fallback.error().message));
            return {};
        }

        // 默认值直接给默认按键文本：没填过与填了空串都落回这一格，不会多读一次键
        const auto stored = settings.stringValue(appShortcutKey(entry.token).toStdString(), QString::fromUtf8(entry.defaultKey.data(), static_cast<int>(entry.defaultKey.size())));
        const auto resolved = Core::HotkeyCombination::parse(stored.toStdString());
        if (resolved && resolved->checkUsableInWindow())
        {
            return *resolved;
        }

        Base::Logger::warning(std::format("界面按键「{}」那一格填的是「{}」，读不出或不能当按键：这一档按默认键 {} 走", entry.token, stored.toStdString(), entry.defaultKey));
        return *fallback;
    }

    QString appShortcutText(const Core::ISettingsStore &settings, const AppShortcutEntry &entry)
    {
        const auto value = appShortcutValue(settings, entry);
        const auto text  = value.toText();
        // 兜底那颗也拿不到文本时（清册写错）交回默认那一串原文：界面上至少还有个能看的东西
        return text.isEmpty() ? QString::fromUtf8(entry.defaultKey.data(), static_cast<int>(entry.defaultKey.size())) : text;
    }

    std::string_view appShortcutFamilyName(const std::string_view token)
    {
        // 标识的第一段就是族名：设置页那一栏按它分组，加一档新按键不必再改第二处清单
        const auto family = token.substr(0, token.find('/'));
        if (family == "Annotation")
        {
            return "标注窗";
        }
        if (family == "Mask")
        {
            return "截图遮罩";
        }
        if (family == "Pin")
        {
            return "贴图窗";
        }
        return {};
    }

    bool isAppShortcutHit(const Core::ISettingsStore &settings, const AppShortcutEntry &entry, const int key, const Qt::KeyboardModifiers modifiers,
                          const bool allowShiftVariant)
    {
        const Core::HotkeyCombination pressed{modifiers, static_cast<Qt::Key>(key)};
        const auto                      bound = appShortcutValue(settings, entry);
        if (pressed == bound)
        {
            return true;
        }

        // 反向档：与登记的那颗相同、只是多按了一个 Shift。用户把正向档换成带 Ctrl 的组合时，
        // 反向档就是那颗组合再加 Shift，跟着一起走
        if (!allowShiftVariant || !modifiers.testFlag(Qt::ShiftModifier))
        {
            return false;
        }
        // 摘位而不是相减：Shift 是一个位，别的位还挂在上面
        return Core::HotkeyCombination(modifiers & ~Qt::ShiftModifier, static_cast<Qt::Key>(key)) == bound;
    }

} // namespace GyanisScribe::Gui
