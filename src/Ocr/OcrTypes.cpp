#include "Ocr/OcrTypes.h"

namespace GyanisScribe::Ocr
{

    QString OcrLine::text() const
    {
        QStringList parts;
        parts.reserve(words.size());
        for (const auto &word: words)
        {
            parts << word.text;
        }
        // 词之间补空格：后端给的是逐词框，拼回一行时不补就会把两个词粘成一段谁也查不到的串
        return parts.join(QLatin1Char(' '));
    }

    QString OcrResult::text() const
    {
        QStringList parts;
        parts.reserve(lines.size());
        for (const auto &line: lines)
        {
            parts << line.text();
        }
        return parts.join(QLatin1Char('\n'));
    }

    bool OcrResult::isEmpty() const noexcept
    {
        return lines.isEmpty();
    }

} // namespace GyanisScribe::Ocr
