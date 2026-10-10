#include "App/CaptureIntent.h"

#include "Core/Choices.h"
#include "Core/Text.h"

namespace GyanisScribe::App
{
    namespace
    {
        /// 这一格在配置里的键名，报错文案要把它原话写出来便于手工修
        constexpr std::string_view kSettingName = "截完之后";
        constexpr std::string_view kPageName    = "截图页";
        constexpr std::string_view kSettingKey  = "Capture/AfterAction";
    } // namespace

    std::string_view captureIntentText(const CaptureIntent intent) noexcept
    {
        for (const auto &choice: kCaptureIntentChoices)
        {
            if (choice.intent == intent)
            {
                return choice.token;
            }
        }
        // 词表里没有这一档：新加枚举值而忘了补词表就落在这里，交回空由调用方决定报法
        return {};
    }

    Core::Expected<CaptureIntent> parseCaptureIntent(const QString &stored)
    {
        const auto wanted = stored.toStdString();
        for (const auto &choice: kCaptureIntentChoices)
        {
            if (wanted == choice.token)
            {
                return choice.intent;
            }
        }
        // 不替用户猜一档：悄悄回落到「进标注窗」会让一个不想弹窗的用户每按一次热键都多出一层窗
        const auto localizedSetting = GyanisScribe::Core::uiTextUtf8(kSettingName.data());
        const auto localizedPage    = GyanisScribe::Core::uiTextUtf8(kPageName.data());
        return std::unexpected(Core::unknownTokenError(kCaptureIntentChoices, stored, localizedSetting, localizedPage, kSettingKey));
    }

    CaptureIntent defaultCaptureIntent() noexcept
    {
        return kCaptureIntentChoices.front().intent;
    }

    Core::Expected<int> parseCaptureDelay(const int stored)
    {
        if (stored >= 0 && stored <= kMaximumCaptureDelaySeconds)
        {
            return stored;
        }
        // 越界不夹到上限：把 600 秒当成 60 秒，用户等到的是「莫名其妙弹出一层窗」，
        // 而这一眼就能看出是配置写歪了，报出来才修得动
        return Core::Expected<int>{std::unexpected{Core::Error{
                .code    = Core::ErrorCode::InvalidArgument,
                .message = Core::uiFormat(
                        Core::uiTextUtf8("延时截图的秒数 \"{}\" 不在可用范围（0 到 {} 秒，0 表示不延时）里：请在设置的截图页重选，或删掉 Capture/DelaySeconds 这一项回到默认档"),
                        stored, kMaximumCaptureDelaySeconds),
        }}};
    }

} // namespace GyanisScribe::App
