#include "Core/Memory/MemoryToken.h"

#include <QStringList>

#include <algorithm>

namespace GyanisScribe::Core
{
    namespace
    {
        /// 纯 ASCII 字母或数字才享受前缀匹配，其余字符一律走引号短语
        bool isAsciiAlphanumeric(const QChar character)
        {
            const ushort code = character.unicode();
            return (code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z');
        }

        bool isPureAsciiAlphanumeric(const QString &text)
        {
            return !text.isEmpty() && std::ranges::all_of(text, [](const QChar character) { return isAsciiAlphanumeric(character); });
        }

        /// CJK 需要逐字成词；其余字母数字由 unicode61 自己按连续段成词
        bool isCjk(const QChar character)
        {
            switch (character.script())
            {
                case QChar::Script_Han:
                case QChar::Script_Hiragana:
                case QChar::Script_Katakana:
                case QChar::Script_Hangul:
                    return true;
                default:
                    return false;
            }
        }

        /// 判断预处理后的片段是否还含有可被 FTS 切出的 token
        bool hasTokenCharacter(const QString &text)
        {
            return std::ranges::any_of(text, [](const QChar character) { return character.isLetterOrNumber(); });
        }
    } // namespace

    QString prepareFtsText(const QString &text)
    {
        QString result;
        result.reserve(text.size());

        QChar previous;
        bool  hasPrevious  = false;
        bool  pendingSpace = false;

        for (const QChar character: text)
        {
            // 制表符与换行在 Unicode 里同属控制类，但它们是词边界，必须先于控制字符处理
            if (character.isSpace())
            {
                pendingSpace = true;
                continue;
            }
            // 其余控制字符不可见也不参与检索，直接丢弃
            if (character.category() == QChar::Other_Control)
            {
                continue;
            }

            if (!result.isEmpty())
            {
                const bool previousIsCjk = hasPrevious && isCjk(previous);
                const bool currentIsCjk  = isCjk(character);
                // CJK 逐字成词，并且与任何非 CJK 字符分开；普通空白只压成一个空格
                if (pendingSpace || previousIsCjk || currentIsCjk)
                {
                    result.append(QLatin1Char(' '));
                }
            }

            result.append(character);
            previous     = character;
            hasPrevious  = true;
            pendingSpace = false;
        }
        return result;
    }

    QString buildFtsQuery(const QString &userText)
    {
        // 按 Unicode 空白切段：全角空格 U+3000 等与 ASCII 空白同为中文输入法的词边界，
        // 只认 ASCII \s 会把两个独立词粘成一段连续短语
        QStringList segments;
        QString     currentSegment;
        for (const QChar character: userText)
        {
            if (character.isSpace())
            {
                if (!currentSegment.isEmpty())
                {
                    segments.append(currentSegment);
                    currentSegment.clear();
                }
                continue;
            }
            currentSegment.append(character);
        }
        if (!currentSegment.isEmpty())
        {
            segments.append(currentSegment);
        }

        QStringList parts;
        parts.reserve(segments.size());
        for (const QString &segment: segments)
        {
            // 两个字符以上的纯 ASCII 词加前缀星号，输入到一半也能命中；单字符不加，避免扫出整库
            if (isPureAsciiAlphanumeric(segment) && segment.size() >= 2)
            {
                parts.append(QStringLiteral("\"%1\"*").arg(segment));
                continue;
            }

            const QString prepared = prepareFtsText(segment);
            // 纯标点段在 unicode61 下切不出 token，留着会变成空短语让 FTS5 报语法错
            if (prepared.isEmpty() || !hasTokenCharacter(prepared))
            {
                continue;
            }

            QString quoted = prepared;
            quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            quoted.prepend(QLatin1Char('"'));
            quoted.append(QLatin1Char('"'));
            parts.append(quoted);
        }
        return parts.join(QLatin1Char(' '));
    }
} // namespace GyanisScribe::Core
