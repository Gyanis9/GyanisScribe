#include "Core/Hotkey/HotkeyManager.h"

#include "Base/Logger.h"
#include "Base/Text.h"
#include "Core/Hotkey/HotkeyCombination.h"
#include "Platform/IHotkeyBackend.h"
#include "Platform/IPlatformIntegration.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"

#include <QScopeGuard>

#include <format>
#include <optional>

namespace GyanisScribe::Core
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

        /// 回溯截图的默认组合：与前面八颗默认键都不重复，用户不必先改掉别的动作才能用
        constexpr std::string_view kDefaultRewind = "Ctrl+Alt+Z";

        /// 步骤记录的默认组合：R 已归录屏，取 T 与前面九颗默认键都不重复
        constexpr std::string_view kDefaultSteps = "Ctrl+Alt+T";

        /// 恢复成功时把「旧组合仍在使用」接在失败原因后面，用户才知道现在按哪颗键还有用
        void appendRestoredNote(Base::Error &error, const QString &restoredText)
        {
            error.message += "；" + Base::uiFormat(Base::uiTextUtf8("旧的 {} 仍在使用，新的组合没有生效"), restoredText.toStdString());
        }

        /// 旧组合也恢复不了时，失败原因要说明这颗键现在按不了，而不是只留一行日志
        void appendUnrestoredNote(Base::Error &error, const QString &previousText)
        {
            error.message += "；" + Base::uiFormat(Base::uiTextUtf8("旧的 {} 也无法恢复，该按键暂时不可用"), previousText.toStdString());
        }
    } // namespace

    HotkeyManager::HotkeyManager(Core::ISettingsStore &settings, Platform::IPlatformIntegration &integration, QObject *parent) :
        IHotkeyManager(parent), m_settings(settings), m_integration(integration)
    {
        // 订阅设置变更：改完快捷键立刻重登，不需要重启也不需要用户在设置页点「应用」
        QObject::connect(&m_settings, &Core::ISettingsStore::valueChanged, this,
                         [this](const QString &key)
                         {
                             if (m_writingRestoredBindings)
                             {
                                 // 写回恢复的旧组合也会发同一条通知：那一批已经登记在册，再登一遍是白翻
                                 return;
                             }
                             // 「允许单键」这一格同样决定旧组合还能不能用：它一变也要重登
                             const auto allowSimpleKey = QString::fromUtf8(kAllowSimpleGlobalHotkeys.data(), static_cast<qsizetype>(kAllowSimpleGlobalHotkeys.size()));
                             bool       isWatched      = key == allowSimpleKey;
                             for (const auto action: managedActions())
                             {
                                 const auto actionKey = settingsKeyFor(action);
                                 if (QString::fromUtf8(actionKey.data(), static_cast<qsizetype>(actionKey.size())) == key)
                                 {
                                     isWatched = true;
                                     break;
                                 }
                             }
                             if (!isWatched)
                             {
                                 return;
                             }
                             if (const auto report = reload(); report)
                             {
                                 if (report->failures.empty() && report->customFailures.empty())
                                 {
                                     return;
                                 }
                                 // 设置页这一路没人接返回值：失败项要从信号交回界面，否则用户以为改成功了
                                 Base::Logger::warning(std::format("快捷键改动后仍有 {} 项没能登记", report->failures.size() + report->customFailures.size()));
                                 emit reloadFailed(*report);
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
        static constexpr std::array kActions = {HotkeyAction::Capture,    HotkeyAction::Record,       HotkeyAction::Ocr,       HotkeyAction::Pick,   HotkeyAction::CaptureLong,
                                                HotkeyAction::TogglePins, HotkeyAction::NextPinGroup, HotkeyAction::QuickSave, HotkeyAction::Rewind, HotkeyAction::Steps};
        return kActions;
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
            case HotkeyAction::Rewind:
                return Core::kHotkeyRewind;
            case HotkeyAction::Steps:
                return Core::kHotkeySteps;
        }
        // 上面穷举过；走到这里说明新增了动作而这里没跟上，交回兜底键名并在日志里点名
        Base::Logger::error(std::format("动作枚举 {} 没有对应的设置键，请把 settingsKeyFor 补全", static_cast<int>(action)));
        return Core::kHotkeyCapture;
    }

    QString HotkeyManager::defaultCombinationTextForSettingsKey(const std::string_view settingsKey)
    {
        for (const auto action: managedActions())
        {
            if (settingsKeyFor(action) == settingsKey)
            {
                return QString::fromUtf8(defaultCombinationTextFor(action));
            }
        }
        return {};
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
            case HotkeyAction::Rewind:
                return kDefaultRewind;
            case HotkeyAction::Steps:
                return kDefaultSteps;
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
        // 上一轮真正在册的记录要留一份：这一轮先清场，新值登记不上时靠它把旧键放回去
        const auto                   previousBindings = m_activeBindings;
        std::map<int, ActiveBinding> nextBindings;
        // 恢复旧键时要先说清这个编号此刻该归哪个功能：固定动作传空，自定义传这一格的标识
        const auto restoreAndNote = [&](Base::Error &error, const int identifier, const std::optional<QString> &token, const QString &settingsKey)
        {
            const auto restore = restorePreviousBinding(identifier, token, previousBindings, nextBindings);
            switch (restore.outcome)
            {
                case RestoreOutcome::Restored:
                    appendRestoredNote(error, restore.previous.toText());
                    // 只记真正恢复成功的项：写回配置那一层拿这份名单把旧组合落回磁盘
                    report.restored.push_back(RestoredHotkeyBinding{settingsKey, restore.previous.toText()});
                    break;
                case RestoreOutcome::Failed:
                    appendUnrestoredNote(error, restore.previous.toText());
                    break;
                case RestoreOutcome::NoPrevious:
                    break;
            }
        };
        // 先清场再重登：留着上一轮的登记，用户改掉的旧组合会一直占着，新组合也可能撞号
        m_backend->unregisterAll();

        for (const auto action: managedActions())
        {
            const int  identifier = identifierFor(action);
            const auto storedText = m_settings.stringValue(settingsKeyFor(action), QString::fromUtf8(defaultCombinationTextFor(action)));

            const auto combination = HotkeyCombination::fromText(storedText.toStdString(), m_settings.boolValue(GyanisScribe::Core::kAllowSimpleGlobalHotkeys, false));
            if (!combination)
            {
                // 设置里的值坏掉时按项记失败，不用默认组合顶上：顶上去用户按到的就不是他设的那个键
                Base::Error error = combination.error();
                restoreAndNote(error, identifier, std::nullopt, QString::fromUtf8(settingsKeyFor(action)));
                report.failures.push_back(HotkeyFailure{action, storedText, std::move(error)});
                continue;
            }

            if (const auto registered = m_backend->registerHotkey(identifier, combination->modifiers(), combination->key()); !registered)
            {
                Base::Error error = registered.error();
                restoreAndNote(error, identifier, std::nullopt, QString::fromUtf8(settingsKeyFor(action)));
                report.failures.push_back(HotkeyFailure{action, storedText, std::move(error)});
                continue;
            }
            nextBindings.insert_or_assign(identifier, ActiveBinding{std::nullopt, *combination});
            ++report.registered;
        }

        // 自定义那一批：编号从 kCustomIdentifierBase 起按表序排，两项同一条组合撞上了就各自记一条失败（后端会拒第二个）
        for (std::size_t slot = 0; slot < m_customBindings.size(); ++slot)
        {
            const auto  identifier  = kCustomIdentifierBase + static_cast<int>(slot);
            const auto &binding     = m_customBindings[slot];
            const auto  combination = HotkeyCombination::fromText(binding.combination.toStdString(), m_settings.boolValue(GyanisScribe::Core::kAllowSimpleGlobalHotkeys, false));
            if (!combination)
            {
                Base::Error error = combination.error();
                restoreAndNote(error, identifier, binding.token, QString::fromUtf8(GyanisScribe::Core::kHotkeyCustomPrefix) + binding.token);
                report.customFailures.push_back(CustomHotkeyFailure{binding.token, binding.combination, std::move(error)});
                continue;
            }
            if (const auto registered = m_backend->registerHotkey(identifier, combination->modifiers(), combination->key()); !registered)
            {
                Base::Error error = registered.error();
                restoreAndNote(error, identifier, binding.token, QString::fromUtf8(GyanisScribe::Core::kHotkeyCustomPrefix) + binding.token);
                report.customFailures.push_back(CustomHotkeyFailure{binding.token, binding.combination, std::move(error)});
                continue;
            }
            nextBindings.insert_or_assign(identifier, ActiveBinding{binding.token, *combination});
            ++report.registered;
        }

        m_activeBindings = std::move(nextBindings);
        return report;
    }

    HotkeyManager::RestoreResult HotkeyManager::restorePreviousBinding(const int identifier, const std::optional<QString> &token, const std::map<int, ActiveBinding> &previous,
                                                                       std::map<int, ActiveBinding> &next)
    {
        const auto old = previous.find(identifier);
        if (old == previous.end())
        {
            return {};
        }
        if (old->second.token != token)
        {
            // 自定义编号按下标算，增删前一项会让后一项换号：编号下的功能已经换人，旧组合不是它的
            return {};
        }
        // 恢复也要过当前这一档：关掉「允许单键」后旧裸键不再合法，不能借恢复通道绕开开关
        const auto current = HotkeyCombination::fromText(old->second.combination.toText().toStdString(), m_settings.boolValue(kAllowSimpleGlobalHotkeys, false));
        if (!current)
        {
            return {};
        }
        RestoreResult result;
        result.outcome  = RestoreOutcome::Failed;
        result.previous = old->second.combination;
        if (const auto restored = m_backend->registerHotkey(identifier, current->modifiers(), current->key()); !restored)
        {
            Base::Logger::warning(std::format("编号 {} 的旧组合没能恢复：{}", identifier, restored.error().message));
            return result;
        }
        next.insert_or_assign(identifier, ActiveBinding{token, *current});
        result.outcome = RestoreOutcome::Restored;
        return result;
    }

    void HotkeyManager::setCustomBindings(std::vector<CustomHotkeyBinding> bindings)
    {
        m_customBindings = std::move(bindings);
    }

    void HotkeyManager::persistRestoredBindings(const HotkeyReloadReport &report)
    {
        if (report.restored.empty())
        {
            return;
        }
        // 每条写回都会发 valueChanged：不压住这一位，写几条就重登几轮
        m_writingRestoredBindings = true;
        // 中途抛出也要把标志复位：永久留在 true 会吞掉之后所有设置变更的重登
        const auto resetWriting = qScopeGuard([this] { m_writingRestoredBindings = false; });
        for (const auto &entry: report.restored)
        {
            if (const auto written = m_settings.setValue(entry.settingsKey.toStdString(), entry.combination); !written)
            {
                Base::Logger::warning(std::format("恢复的旧组合没能写回配置：{}（{}）", entry.settingsKey.toStdString(), written.error().message));
            }
        }
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

} // namespace GyanisScribe::Core
