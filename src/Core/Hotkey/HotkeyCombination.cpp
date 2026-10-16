#include "Core/Hotkey/HotkeyCombination.h"

#include "Base/Text.h"

#include <QChar>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace GyanisScribe::Core
{
    namespace
    {
        std::string trimmedToLower(std::string_view text)
        {
            const auto begin = text.find_first_not_of(" \t");
            if (begin == std::string_view::npos)
            {
                return {};
            }
            const auto  end = text.find_last_not_of(" \t");
            std::string lowered(text.substr(begin, end - begin + 1));
            std::ranges::transform(lowered, lowered.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return lowered;
        }

        // 修饰键别名一次列全：设置里手写的 INI 会出现 Ctrl、CTRL、Control 几种写法，
        // 认不全就会把用户写对的组合判成坏值
        constexpr std::array<std::string_view, 2> kControlAliases = {"ctrl", "control"};
        constexpr std::array<std::string_view, 2> kAltAliases     = {"alt", "option"};
        constexpr std::array<std::string_view, 1> kShiftAliases   = {"shift"};
        constexpr std::array<std::string_view, 3> kMetaAliases    = {"win", "meta", "super"};

        template<std::size_t Size>
        bool matchesAny(const std::array<std::string_view, Size> &aliases, std::string_view token)
        {
            return std::ranges::find(aliases, token) != aliases.end();
        }

        // 允许当按键用的只有这三段：字母、数字、F1-F24，再加空格。
        // 符号键在不同键盘布局上落点不同，登记成全局热键后用户按到的不是屏幕上写的那个键
        bool isUsableKey(Qt::Key key)
        {
            const auto code = static_cast<int>(key);
            return (code >= static_cast<int>(Qt::Key_A) && code <= static_cast<int>(Qt::Key_Z)) || (code >= static_cast<int>(Qt::Key_0) && code <= static_cast<int>(Qt::Key_9)) ||
                   (code >= static_cast<int>(Qt::Key_F1) && code <= static_cast<int>(Qt::Key_F24)) || code == static_cast<int>(Qt::Key_Space);
        }

        // 按键本体那两条在两档里同解：不是修饰键本身，且在允许集合内。
        // 判的是形状，报错文案由各自那一档给（两档要说清自己是谁，用户才知道往哪边改）
        enum class KeyShapeProblem
        {
            None,
            ModifierItself,
            NotInAllowedSet,
        };

        KeyShapeProblem keyShapeProblem(Qt::Key key)
        {
            const auto code = static_cast<int>(key);
            // 修饰键本身不能当按键：那会注册出「按住 Ctrl 就触发」的组合，别的程序没法正常打字
            if (code == static_cast<int>(Qt::Key_Control) || code == static_cast<int>(Qt::Key_Alt) || code == static_cast<int>(Qt::Key_Shift) ||
                code == static_cast<int>(Qt::Key_Meta))
            {
                return KeyShapeProblem::ModifierItself;
            }
            return isUsableKey(key) ? KeyShapeProblem::None : KeyShapeProblem::NotInAllowedSet;
        }

        /// 「按键是修饰键本身」这一条两档说的是同一句话，所以错误对象也只造一份
        Base::Error modifierItselfError()
        {
            return {Base::ErrorCode::InvalidArgument,
                    GyanisScribe::Base::uiTextUtf8("组合键的按键部分不能是修饰键本身：请在 Ctrl、Alt、Shift、Win 之外再选一个字母、数字、空格或 F1-F24")};
        }

        QString keyTokenText(Qt::Key key)
        {
            const auto code = static_cast<int>(key);
            if ((code >= static_cast<int>(Qt::Key_A) && code <= static_cast<int>(Qt::Key_Z)) || (code >= static_cast<int>(Qt::Key_0) && code <= static_cast<int>(Qt::Key_9)))
            {
                return {QChar(static_cast<ushort>(code))};
            }
            if (code == static_cast<int>(Qt::Key_Space))
            {
                return QStringLiteral("Space");
            }
            if (code >= static_cast<int>(Qt::Key_F1) && code <= static_cast<int>(Qt::Key_F24))
            {
                return QStringLiteral("F") + QString::number(code - static_cast<int>(Qt::Key_F1) + 1);
            }
            return {};
        }

        std::vector<std::string> splitOnPlus(std::string_view text)
        {
            std::vector<std::string> segments;
            std::size_t              position = 0;
            while (true)
            {
                const auto next = text.find('+', position);
                segments.emplace_back(text.substr(position, next == std::string_view::npos ? std::string_view::npos : next - position));
                if (next == std::string_view::npos)
                {
                    break;
                }
                position = next + 1;
            }
            return segments;
        }

        std::optional<Qt::Key> parseKeyToken(std::string_view token)
        {
            const auto lowered = trimmedToLower(token);
            if (lowered.size() == 1)
            {
                const auto character = static_cast<unsigned char>(lowered.front());
                if (character >= 'a' && character <= 'z')
                {
                    return static_cast<Qt::Key>(static_cast<int>(Qt::Key_A) + (character - 'a'));
                }
                if (character >= '0' && character <= '9')
                {
                    return static_cast<Qt::Key>(static_cast<int>(Qt::Key_0) + (character - '0'));
                }
                return std::nullopt;
            }
            if (lowered == "space")
            {
                return Qt::Key_Space;
            }
            if (lowered.size() > 1 && lowered.front() == 'f')
            {
                const auto digits = std::string_view(lowered).substr(1);
                int        number = 0;
                const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), number);
                // 整串必须是数字且落在 1-24，否则按「不认识这个按键」交回，不做任何就近取整
                if (result.ec == std::errc{} && result.ptr == digits.data() + digits.size() && number >= 1 && number <= 24)
                {
                    return static_cast<Qt::Key>(static_cast<int>(Qt::Key_F1) + (number - 1));
                }
            }
            return std::nullopt;
        }
    } // namespace

    HotkeyCombination::HotkeyCombination(Qt::KeyboardModifiers modifiers, Qt::Key key) : m_modifiers(modifiers), m_key(key)
    {
    }

    Base::Expected<HotkeyCombination> HotkeyCombination::parse(std::string_view text)
    {
        const auto segments = splitOnPlus(text);

        Qt::KeyboardModifiers modifiers = Qt::NoModifier;
        std::string_view      keyToken;

        for (std::size_t index = 0; index < segments.size(); ++index)
        {
            const auto lowered = trimmedToLower(segments[index]);
            if (lowered.empty())
            {
                return std::unexpected(
                        Base::Error{Base::ErrorCode::InvalidArgument,
                                    GyanisScribe::Base::uiFormat(
                                            GyanisScribe::Base::uiTextUtf8("组合键「{}」里有一段是空的（相邻的 + 之间没有内容）。请写成 Ctrl+Alt+A 这种每段都有内容的形式"),
                                            std::string(text))});
            }

            if (index + 1 == segments.size())
            {
                // 最后一段是按键本体，前面各段一律按修饰键解释
                keyToken = segments[index];
                break;
            }

            if (matchesAny(kControlAliases, lowered))
            {
                modifiers |= Qt::ControlModifier;
            } else if (matchesAny(kAltAliases, lowered))
            {
                modifiers |= Qt::AltModifier;
            } else if (matchesAny(kShiftAliases, lowered))
            {
                modifiers |= Qt::ShiftModifier;
            } else if (matchesAny(kMetaAliases, lowered))
            {
                modifiers |= Qt::MetaModifier;
            } else
            {
                return std::unexpected(
                        Base::Error{Base::ErrorCode::InvalidArgument,
                                    GyanisScribe::Base::uiFormat(
                                            GyanisScribe::Base::uiTextUtf8("组合键「{}」里的「{}」不是修饰键。修饰键只认 Ctrl、Alt、Shift、Win（Control/Option/Meta/Super 同义）"),
                                            std::string(text), segments[index])});
            }
            // 同一修饰键重复写按并位处理：与 toText() 的输出保持同形，比较才不会因重复段落判成不相等
        }

        if (keyToken.empty())
        {
            return std::unexpected(Base::Error{
                    Base::ErrorCode::InvalidArgument,
                    GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("组合键「{}」最后没有按键。请写成 Ctrl+Alt+A 这种「修饰键+按键」的形式"), std::string(text))});
        }

        const auto parsedKey = parseKeyToken(keyToken);
        if (!parsedKey)
        {
            return std::unexpected(
                    Base::Error{Base::ErrorCode::InvalidArgument,
                                GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("组合键「{}」里的按键「{}」不支持。请改用字母 A-Z、数字 0-9、Space 或 F1-F24"),
                                                             std::string(text), std::string(keyToken))});
        }

        // 到这里只管「读得出来」，不管「能不能拿去登记」：裸键在这里是合法的，
        // 全局档要的那一个修饰键由 fromText() 补上
        return HotkeyCombination(modifiers, *parsedKey);
    }

    Base::Expected<HotkeyCombination> HotkeyCombination::fromText(const std::string_view text, const bool allowBareKeys)
    {
        auto parsed = parse(text);
        if (!parsed)
        {
            return parsed;
        }

        if (const auto usable = parsed->checkUsableGlobally(allowBareKeys); !usable)
        {
            return std::unexpected(usable.error());
        }
        return parsed;
    }

    Base::Expected<void> HotkeyCombination::checkUsableGlobally(const bool allowBareKeys) const
    {
        switch (keyShapeProblem(m_key))
        {
            case KeyShapeProblem::ModifierItself:
                return std::unexpected(modifierItselfError());
            case KeyShapeProblem::NotInAllowedSet:
                return std::unexpected(
                        Base::Error{Base::ErrorCode::InvalidArgument,
                                    GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("按键 0x{:X} 不能用作全局热键。请改用字母、数字、空格或 F1-F24 之一"),
                                                                 static_cast<int>(m_key))});
            case KeyShapeProblem::None:
                break;
        }

        if (m_modifiers == Qt::NoModifier && !allowBareKeys)
        {
            return std::unexpected(
                    Base::Error{Base::ErrorCode::InvalidArgument,
                                GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("全局热键至少要带一个修饰键：单键 {} 会把其它程序的普通按键吞掉。请加上 Ctrl 或 Alt"),
                                                             keyTokenText(m_key).toStdString())});
        }
        return {};
    }

    Base::Expected<void> HotkeyCombination::checkUsableInWindow() const
    {
        // 界面内的按键只少「至少一个修饰键」这一条：窗口里的裸键只在程序自己有焦点时吃到，
        // 不会像全局单键那样把别的程序的打字吞掉。文案各自说自己是哪一档，
        // 混着一句「不能用作全局热键」用户就不知道该往哪边改
        switch (keyShapeProblem(m_key))
        {
            case KeyShapeProblem::ModifierItself:
                return std::unexpected(modifierItselfError());
            case KeyShapeProblem::NotInAllowedSet:
                return std::unexpected(
                        Base::Error{Base::ErrorCode::InvalidArgument,
                                    GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("按键 0x{:X} 不能用作界面按键。请改用字母、数字、空格或 F1-F24 之一"),
                                                                 static_cast<int>(m_key))});
            case KeyShapeProblem::None:
                return {};
        }
        return {};
    }

    QString HotkeyCombination::toText() const
    {
        const auto keyText = keyTokenText(m_key);
        // 「未设」只看有没有按键本体：界面按键是裸键，修饰键为空时也要吐得出「R」这一格文本，
        // 否则写进配置的是空串，下一次读它就成了「填的是空文本」
        if (keyText.isEmpty())
        {
            return {};
        }

        // 顺序固定 Ctrl、Alt、Shift、Win：存储、菜单提示与录入框三处共用这一个形态
        QStringList parts;
        if (m_modifiers.testFlag(Qt::ControlModifier))
        {
            parts << QStringLiteral("Ctrl");
        }
        if (m_modifiers.testFlag(Qt::AltModifier))
        {
            parts << QStringLiteral("Alt");
        }
        if (m_modifiers.testFlag(Qt::ShiftModifier))
        {
            parts << QStringLiteral("Shift");
        }
        if (m_modifiers.testFlag(Qt::MetaModifier))
        {
            parts << QStringLiteral("Win");
        }
        parts << keyText;
        return parts.join(QChar('+'));
    }

    Qt::KeyboardModifiers HotkeyCombination::modifiers() const noexcept
    {
        return m_modifiers;
    }

    Qt::Key HotkeyCombination::key() const noexcept
    {
        return m_key;
    }

} // namespace GyanisScribe::Core
