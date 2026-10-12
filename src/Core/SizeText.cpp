#include "Core/SizeText.h"

#include <QRegularExpression>
#include <QStringList>

namespace GyanisScribe::Core
{

    std::optional<QSize> parseSizeText(const QString &text)
    {
        // 宽度与高度之间允许 x/×/逗号/空白：界面上的读数是「宽 × 高」，用户照着敲、
        // 换一种敲都得认
        static const QRegularExpression separator{QStringLiteral(R"([x×,\s]+)")};
        const QStringList               parts = text.trimmed().split(separator, Qt::SkipEmptyParts);
        if (parts.size() != 2)
        {
            return std::nullopt;
        }

        bool      okWidth  = false;
        bool      okHeight = false;
        const int width    = parts[0].toInt(&okWidth);
        const int height   = parts[1].toInt(&okHeight);
        if (!okWidth || !okHeight || width <= 0 || height <= 0)
        {
            return std::nullopt;
        }
        return QSize{width, height};
    }

} // namespace GyanisScribe::Core
