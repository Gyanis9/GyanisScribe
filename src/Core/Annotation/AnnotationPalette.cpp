#include "Core/Annotation/AnnotationPalette.h"

#include "Core/Pick/RecentColors.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"

#include "Base/Text.h"

#include <algorithm>
#include <string_view>

namespace GyanisScribe::Core
{
    namespace
    {
        /**
         * @brief 色板解码的共用实现
         * @param tokens 待解码的色块词
         * @param recoveryHint 恢复默认的做法，由配置读入或文件导入那一侧给出
         * @return 解码出的色板；为空、超上限或含坏词时返回原因
         */
        Base::Expected<std::vector<QColor>> paletteFromTokensWithHint(const QStringList &tokens, const std::string_view recoveryHint)
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
            const auto parsed = Core::parseRecentColors(tokens, Base::uiText("自定义色板").toStdString(), recoveryHint);
            if (!parsed)
            {
                return std::unexpected(parsed.error());
            }
            return *parsed;
        }
    } // namespace

    std::vector<QColor> defaultAnnotationPalette()
    {
        // 值只住在本头那一处；这里按区间两端构造 vector，不用 ranges::to（编译器实现进度不一，写法要保守）
        const auto &table = kAnnotationPaletteColors;
        return {table.begin(), table.end()};
    }

    Base::Expected<std::vector<QColor>> annotationPalette(const Core::ISettingsStore &settings)
    {
        const auto stored = settings.stringListValue(Core::kAnnotationPalette, {});
        if (stored.isEmpty())
        {
            // 「没填」与「填了八个」之间还有「填成空白」：清掉那一格就是回到内置那八格，不是留一张空表
            return defaultAnnotationPalette();
        }
        return paletteFromTokens(stored);
    }

    QStringList paletteToTokens(const std::vector<QColor> &palette)
    {
        return Core::serializeRecentColors(palette);
    }

    Base::Expected<std::vector<QColor>> paletteFromTokens(const QStringList &tokens)
    {
        const auto recovery = Base::uiText("删掉配置里的 %1 这一项可以恢复默认").arg(QString::fromUtf8(Core::kAnnotationPalette)).toStdString();
        return paletteFromTokensWithHint(tokens, recovery);
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
        const auto token = Core::recentColorToken(color);
        const auto seen  = std::ranges::any_of(palette, [&](const QColor &held) { return Core::recentColorToken(held) == token; });
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
            lines << Core::recentColorToken(color);
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
        const auto recovery = Base::uiText("检查导入文件里的色块写法，或删掉配置里的 %1 这一项可以恢复默认").arg(QString::fromUtf8(Core::kAnnotationPalette)).toStdString();
        return paletteFromTokensWithHint(tokens, recovery);
    }

} // namespace GyanisScribe::Core
