#include "Base/SizeText.h"

#include <QRegularExpression>

#include <cmath>
#include <QStringList>

namespace GyanisScribe::Base
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

    std::optional<double> parseScaleRatio(const QString &text)
    {
        auto trimmed = text.trimmed();
        if (trimmed.isEmpty())
        {
            return std::nullopt;
        }

        // 百分数是「相对原大小」的另一种写法：50% 就是 0.5 倍
        bool percent = false;
        if (trimmed.endsWith(QLatin1Char('%'), Qt::CaseInsensitive))
        {
            percent = true;
            trimmed.chop(1);
        } else if (trimmed.endsWith(QLatin1Char('x'), Qt::CaseInsensitive))
        {
            trimmed.chop(1);
        }

        bool       ok    = false;
        const auto value = trimmed.trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(value) || value <= 0.0)
        {
            return std::nullopt;
        }

        const auto ratio = percent ? value / 100.0 : value;
        if (ratio <= 0.0 || ratio > kMaximumScaleRatio)
        {
            return std::nullopt;
        }
        return ratio;
    }

} // namespace GyanisScribe::Base
