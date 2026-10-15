#include "Annotation/AnnotationPalette.h"

#include "Pick/RecentColors.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"
#include "Ui/DesignTokens.h"

#include "Base/Text.h"

#include <algorithm>

namespace GyanisScribe::Annotation
{
    std::vector<QColor> defaultAnnotationPalette()
    {
        // 值只住在 Ui/DesignTokens.h 那一处；这里按区间两端构造 vector，不用 ranges::to（编译器实现进度不一，写法要保守）
        const auto &table = Ui::kAnnotationPalette;
        return std::vector<QColor>(table.begin(), table.end());
    }

    Base::Expected<std::vector<QColor>> annotationPalette(const Settings::ISettingsStore &settings)
    {
        const auto stored = settings.stringListValue(Settings::kAnnotationPalette, {});
        if (stored.isEmpty())
        {
            // 「没填」与「填了八个」之间还有「填成空白」：清掉那一格就是回到内置那八格，不是留一张空表
            return defaultAnnotationPalette();
        }
        return paletteFromTokens(stored);
    }

    QStringList paletteToTokens(const std::vector<QColor> &palette)
    {
        return Pick::serializeRecentColors(palette);
    }

    Base::Expected<std::vector<QColor>> paletteFromTokens(const QStringList &tokens)
    {
        if (tokens.isEmpty())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument,
                                               GyanisScribe::Base::uiTextUtf8("色板是空的：至少要留一格。想回到内置那八格，请把设置里那一格清掉而不是填成空白")});
        }
        if (tokens.size() > static_cast<qsizetype>(kPaletteCapacity))
        {
            // 不截断：悄悄丢掉后面几格，用户看到的是「我存的八格少了两格」，而原因哪儿都没有
            return std::unexpected(
                    Base::Error{Base::ErrorCode::InvalidArgument, GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("色板有 {} 格，超出上限 {} 格：请删掉几格再存"),
                                                                                               tokens.size(), static_cast<int>(kPaletteCapacity))});
        }
        const auto parsed = Pick::parseRecentColors(tokens);
        if (!parsed)
        {
            return std::unexpected(parsed.error());
        }
        return *parsed;
    }

    std::vector<QColor> movePaletteColor(std::vector<QColor> palette, const std::size_t from, const std::size_t to)
    {
        if (from >= palette.size() || to >= palette.size() || from == to)
        {
            return palette;
        }
        // 「挪到那一格那儿」：先把要挪的摘出来，再按落点插入。直接交换在拖过半排时会把意图反着算
        const auto moving = palette[from];
        palette.erase(palette.begin() + static_cast<std::ptrdiff_t>(from));
        palette.insert(palette.begin() + static_cast<std::ptrdiff_t>(to), moving);
        return palette;
    }

    std::vector<QColor> addPaletteColor(std::vector<QColor> palette, const QColor &color)
    {
        if (!color.isValid() || palette.size() >= kPaletteCapacity)
        {
            return palette;
        }
        const auto token = Pick::recentColorToken(color);
        const auto seen  = std::ranges::any_of(palette, [&](const QColor &held) { return Pick::recentColorToken(held) == token; });
        if (seen)
        {
            return palette;
        }
        palette.push_back(color);
        return palette;
    }

    std::vector<QColor> removePaletteColor(std::vector<QColor> palette, const std::size_t index)
    {
        if (index >= palette.size())
        {
            return palette;
        }
        palette.erase(palette.begin() + static_cast<std::ptrdiff_t>(index));
        return palette;
    }

    QString paletteFileText(const std::vector<QColor> &palette)
    {
        QStringList lines;
        lines.reserve(static_cast<qsizetype>(palette.size()));
        for (const auto &color: palette)
        {
            lines << Pick::recentColorToken(color);
        }
        // 末尾留一个换行：粘进记事本再导回来，最后一格不会因为「没有行尾」被当成半截
        return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }

    Base::Expected<std::vector<QColor>> parsePaletteFileText(const QString &text)
    {
        QStringList tokens;
        for (const auto &line: text.split(QLatin1Char('\n')))
        {
            // 逗号也认：手写的清单里「#FF0000, #00FF00」是常见的一笔
            for (const auto &piece: line.split(QLatin1Char(',')))
            {
                const auto trimmed = piece.trimmed();
                if (!trimmed.isEmpty())
                {
                    tokens << trimmed;
                }
            }
        }
        return paletteFromTokens(tokens);
    }

} // namespace GyanisScribe::Annotation
