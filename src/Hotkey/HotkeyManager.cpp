#include "Hotkey/HotkeyManager.h"

#include "Base/Logger.h"
#include "Hotkey/HotkeyCombination.h"
#include "Platform/IHotkeyBackend.h"
#include "Platform/IPlatformIntegration.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"

#include <format>

namespace GyanisScribe::Hotkey
{
    namespace
    {
        // 默认组合与需求文档一致：Ctrl+Alt+A 截图、Ctrl+Alt+R 录屏、Ctrl+Alt+O 识别
        constexpr std::string_view kDefaultCapture = "Ctrl+Alt+A";
        constexpr std::string_view kDefaultRecord  = "Ctrl+Alt+R";
        constexpr std::string_view kDefaultOcr     = "Ctrl+Alt+O";
        constexpr std::string_view kDefaultPick    = "Ctrl+Alt+P";

        /// 长截图的默认组合：L 取 long，与 A/R/O/P 同一套「动作首字母」记法
        constexpr std::string_view kDefaultCaptureLong = "Ctrl+Alt+L";

        /// 收起/摆回全部贴图的默认组合：H 取 hide，与同类工具的 F3 同一件事
        constexpr std::string_view kDefaultTogglePins = "Ctrl+Alt+H";

        /// 切到下一组贴图的默认组合：G 取 group
        constexpr std::string_view kDefaultNextPinGroup = "Ctrl+Alt+G";

        /// 快速保存的默认组合：S 取 save，与 A/R/O/P 同一套「动作首字母」记法
        constexpr std::string_view kDefaultQuickSave = "Ctrl+Alt+S";
    } // namespace

    HotkeyManager::HotkeyManager(Core::ISettingsStore &settings, Platform::IPlatformIntegration &integration, QObject *parent) :
        IHotkeyManager(parent), m_settings(settings), m_integration(integration)
    {
        // 订阅设置变更：改完快捷键立刻重登，不需要重启也不需要用户在设置页点「应用」
        QObject::connect(&m_settings, &Core::ISettingsStore::valueChanged, this,
                         [this](const QString &key)
                         {
                             for (const auto action: managedActions())
                             {
                                 if (QString::fromUtf8(settingsKeyFor(action).data(), static_cast<qsizetype>(settingsKeyFor(action).size())) == key)
                                 {
                                     if (const auto report = reload(); report && !report->failures.empty())
                                     {
                                         // 重载里带了失败项也要出声，否则用户以为改成功了
                                         Base::Logger::warning(std::format("快捷键改动后仍有 {} 项没能登记", report->failures.size()));
                                     }
                                     return;
                                 }
                             }
                         });
    }

    HotkeyManager::~HotkeyManager()
    {
        if (m_backend)
        {
            m_backend->unregisterAll();
        }
    }

    std::span<const HotkeyAction> HotkeyManager::managedActions()
    {
        // 表长度由这份初始化列表自己算：每加一个动作就改三处的日子结束了
        static constexpr std::array actions = {HotkeyAction::Capture,     HotkeyAction::Record,     HotkeyAction::Ocr,          HotkeyAction::Pick,
                                               HotkeyAction::CaptureLong, HotkeyAction::TogglePins, HotkeyAction::NextPinGroup, HotkeyAction::QuickSave};
        return actions;
    }

    std::string_view HotkeyManager::settingsKeyFor(HotkeyAction action)
    {
        switch (action)
        {
            case HotkeyAction::Capture:
                return Core::kHotkeyCapture;
            case HotkeyAction::Record:
                return Core::kHotkeyRecord;
            case HotkeyAction::Ocr:
                return Core::kHotkeyOcr;
            case HotkeyAction::Pick:
                return Core::kHotkeyPick;
            case HotkeyAction::CaptureLong:
                return Core::kHotkeyCaptureLong;
            case HotkeyAction::TogglePins:
                return Core::kHotkeyTogglePins;
            case HotkeyAction::NextPinGroup:
                return Core::kHotkeyNextPinGroup;
            case HotkeyAction::QuickSave:
                return Core::kHotkeyQuickSave;
        }
        // 上面穷举过；走到这里说明新增了动作而这里没跟上，交回兜底键名并在日志里点名
        Base::Logger::error(std::format("动作枚举 {} 没有对应的设置键，请把 settingsKeyFor 补全", static_cast<int>(action)));
        return Core::kHotkeyCapture;
    }

    std::string_view HotkeyManager::defaultCombinationTextFor(HotkeyAction action)
    {
        switch (action)
        {
            case HotkeyAction::Capture:
                return kDefaultCapture;
            case HotkeyAction::Record:
                return kDefaultRecord;
            case HotkeyAction::Ocr:
                return kDefaultOcr;
            case HotkeyAction::Pick:
                return kDefaultPick;
            case HotkeyAction::CaptureLong:
                return kDefaultCaptureLong;
            case HotkeyAction::TogglePins:
                return kDefaultTogglePins;
            case HotkeyAction::NextPinGroup:
                return kDefaultNextPinGroup;
            case HotkeyAction::QuickSave:
                return kDefaultQuickSave;
        }
        Base::Logger::error(std::format("动作枚举 {} 没有对应的默认组合，请把 defaultCombinationTextFor 补全", static_cast<int>(action)));
        return kDefaultCapture;
    }

    int HotkeyManager::identifierFor(HotkeyAction action)
    {
        // 编号 = 枚举序号 + 1：0 留给「没有编号」的哨兵位，枚举新成员只能追加在末尾正是为了这条不位移
        return static_cast<int>(action) + 1;
    }

    std::optional<HotkeyAction> HotkeyManager::actionForIdentifier(int identifier)
    {
        for (const auto action: managedActions())
        {
            if (identifierFor(action) == identifier)
            {
                return action;
            }
        }
        return std::nullopt;
    }

    Base::Expected<HotkeyReloadReport> HotkeyManager::reload()
    {
        if (!m_backend)
        {
            m_backend = m_integration.createHotkeyBackend();
            // 后端只建一次：激活信号接的是这一个对象，重建会把已登记的热键与信号连接一起换掉
            QObject::connect(m_backend.get(), &Platform::IHotkeyBackend::activated, this,
                             [this](int identifier)
                             {
                                 if (const auto slot = customIndexForIdentifier(identifier); slot)
                                 {
                                     // 编号区间已经分开了，先认自定义那一段：这一路的标识是调用方给的，热键层不解释它
                                     emit customActionTriggered(m_customBindings[*slot].token);
                                     return;
                                 }
                                 const auto action = actionForIdentifier(identifier);
                                 if (!action)
                                 {
                                     // 不在册的编号别替它消费，也别替它发动作信号
                                     Base::Logger::warning(std::format("收到没登记过的热键编号 {}，已忽略", identifier));
                                     return;
                                 }
                                 emit actionTriggered(*action);
                             });
        }

        HotkeyReloadReport report;
        // 先清场再重登：留着上一轮的登记，用户改掉的旧组合会一直占着，新组合也可能撞号
        m_backend->unregisterAll();

        for (const auto action: managedActions())
        {
            const auto storedText = m_settings.stringValue(settingsKeyFor(action), QString::fromUtf8(defaultCombinationTextFor(action)));

            const auto combination = HotkeyCombination::fromText(storedText.toStdString(), m_settings.boolValue(GyanisScribe::Core::kAllowSimpleGlobalHotkeys, false));
            if (!combination)
            {
                // 设置里的值坏掉时按项记失败，不用默认组合顶上：顶上去用户按到的就不是他设的那个键
                report.failures.push_back(HotkeyFailure{action, storedText, combination.error()});
                continue;
            }

            if (const auto registered = m_backend->registerHotkey(identifierFor(action), combination->modifiers(), combination->key()); !registered)
            {
                report.failures.push_back(HotkeyFailure{action, storedText, registered.error()});
                continue;
            }
            ++report.registered;
        }

        // 自定义那一批：编号从 kCustomIdentifierBase 起按表序排，两项同一条组合撞上了就各自记一条失败（后端会拒第二个）
        for (std::size_t slot = 0; slot < m_customBindings.size(); ++slot)
        {
            const auto &binding    = m_customBindings[slot];
            const auto combination = HotkeyCombination::fromText(binding.combination.toStdString(), m_settings.boolValue(GyanisScribe::Core::kAllowSimpleGlobalHotkeys, false));
            if (!combination)
            {
                report.customFailures.push_back(CustomHotkeyFailure{binding.token, binding.combination, combination.error()});
                continue;
            }
            if (const auto registered = m_backend->registerHotkey(kCustomIdentifierBase + static_cast<int>(slot), combination->modifiers(), combination->key()); !registered)
            {
                report.customFailures.push_back(CustomHotkeyFailure{binding.token, binding.combination, registered.error()});
                continue;
            }
            ++report.registered;
        }

        return report;
    }

    void HotkeyManager::setCustomBindings(std::vector<CustomHotkeyBinding> bindings)
    {
        m_customBindings = std::move(bindings);
    }

    std::optional<std::size_t> HotkeyManager::customIndexForIdentifier(const int identifier) const
    {
        if (identifier < kCustomIdentifierBase)
        {
            return {};
        }
        const auto slot = static_cast<std::size_t>(identifier - kCustomIdentifierBase);
        if (slot >= m_customBindings.size())
        {
            return {};
        }
        return slot;
    }

    QString HotkeyManager::bindingText(HotkeyAction action) const
    {
        const auto storedText  = m_settings.stringValue(settingsKeyFor(action), QString::fromUtf8(defaultCombinationTextFor(action)));
        const auto combination = HotkeyCombination::fromText(storedText.toStdString(), m_settings.boolValue(GyanisScribe::Core::kAllowSimpleGlobalHotkeys, false));
        if (!combination)
        {
            // 界面提示不能出现一个按不出来的组合：坏值就退回默认组合的文本，并把原因留在设置里
            Base::Logger::warning(std::format("设置项 {} 的组合「{}」无效（{}），提示改按默认组合显示", std::string(settingsKeyFor(action)), storedText.toStdString(),
                                              combination.error().message));
            return QString::fromUtf8(defaultCombinationTextFor(action));
        }
        return combination->toText();
    }

} // namespace GyanisScribe::Hotkey
