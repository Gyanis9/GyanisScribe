#include "History/HistoryTypes.h"

#include "Base/Text.h"

#include <array>
#include <cstddef>
#include <format>

namespace
{
    using GyanisScribe::History::HistoryKind;

    /// 存储标记与中文显示名按下标互指，新增类别只能追加在末尾
    constexpr std::array<std::string_view, 2> kKindTokens = {"Screenshot", "Recording"};
} // namespace

namespace GyanisScribe::History
{
    std::string_view historyKindToken(const HistoryKind kind)
    {
        switch (kind)
        {
            case HistoryKind::Screenshot:
                return kKindTokens[0];
            case HistoryKind::Recording:
                return kKindTokens[1];
        }
        return kKindTokens[0];
    }

    QString historyKindLabel(const HistoryKind kind)
    {
        // 显示名与存储标记分开：改中文说法不该让已经写在盘上的历史文件读不出来
        return kind == HistoryKind::Recording ? GyanisScribe::Base::uiText("录屏") : GyanisScribe::Base::uiText("截图");
    }

    Base::Expected<HistoryKind> parseHistoryKind(const std::string_view token)
    {
        for (std::size_t index = 0; index < kKindTokens.size(); ++index)
        {
            if (kKindTokens[index] == token)
            {
                return static_cast<HistoryKind>(index);
            }
        }

        // 逐项列出可用标记：只报「不认识」用户不知道该把这一行改成什么
        return Base::Expected<HistoryKind>{std::unexpected{Base::Error{
                .code    = Base::ErrorCode::InvalidArgument,
                .message = GyanisScribe::Base::uiFormat(
                        GyanisScribe::Base::uiTextUtf8("历史文件里的类别 \"{}\" 不认识。可用取值只有 Screenshot 与 Recording（区分大小写），改不回任一取值就把这一行删掉"), token),
        }}};
    }

} // namespace GyanisScribe::History
