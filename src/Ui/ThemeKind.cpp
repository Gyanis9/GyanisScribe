#include "Ui/ThemeKind.h"

#include "Core/Text.h"

#include <array>
#include <format>

namespace
{
    /**
     * @brief 按位置对齐的取值表
     * @details 文本与档位必须同序：themeKindText 按下标取文本、parseThemeKind 按下标回档位，
     *          两处共用这一张表才不会各说各话。
     */
    constexpr std::array<std::string_view, 3> kKindTexts = {"Light", "Dark", "System"};
} // namespace

namespace GyanisScribe::Ui
{
    Core::Expected<ThemeKind> parseThemeKind(const std::string_view text)
    {
        for (std::size_t index = 0; index < kKindTexts.size(); ++index)
        {
            if (kKindTexts[index] == text)
            {
                return static_cast<ThemeKind>(index);
            }
        }

        // 逐项列出可用取值：只报「取值不对」用户不知道该填什么
        return Core::Expected<ThemeKind>{std::unexpected{Core::Error{
                .code    = Core::ErrorCode::InvalidArgument,
                .message = GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("主题取值 \"{}\" 不在可用档位里：请在设置的通用页从 Light、Dark、System 三项里选一项，"
                                                                      "手写配置文件时只能是这三个词（区分大小写）"),
                                       text),
        }}};
    }

    std::string_view themeKindText(const ThemeKind themeKind)
    {
        // 越界档位属调用方 bug：返回 System 的文本会把错误藏起来，所以按最后一档之后直接断言式回落
        switch (themeKind)
        {
            case ThemeKind::Light:
                return kKindTexts[0];
            case ThemeKind::Dark:
                return kKindTexts[1];
            case ThemeKind::System:
                return kKindTexts[2];
        }
        return kKindTexts[2];
    }

    ThemeKind resolveThemeKind(const ThemeKind requestedKind, const SystemScheme scheme)
    {
        // 只有 System 档需要解算，用户明确选了浅色或深色就不该被系统回报覆盖
        if (requestedKind != ThemeKind::System)
        {
            return requestedKind;
        }

        // 系统报不出偏好时兜底浅色：深色文字压在白底上是读不出，反过来不会
        return scheme == SystemScheme::Dark ? ThemeKind::Dark : ThemeKind::Light;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    SystemScheme toSystemScheme(const Qt::ColorScheme scheme)
    {
        switch (scheme)
        {
            case Qt::ColorScheme::Light:
                return SystemScheme::Light;
            case Qt::ColorScheme::Dark:
                return SystemScheme::Dark;
            case Qt::ColorScheme::Unknown:
                break;
        }
        return SystemScheme::Unknown;
    }

#endif

} // namespace GyanisScribe::Ui
