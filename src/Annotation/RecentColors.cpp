#include "Annotation/RecentColors.h"

#include <algorithm>
#include <format>

namespace GyanisScribe::Annotation
{
    std::vector<QColor> pushRecentColor(const std::span<const QColor> current, const QColor &picked, const std::size_t capacity)
    {
        // 一次失败的取色（无效色）不该改动色板：原样退回旧清单
        if (!picked.isValid())
        {
            return {current.begin(), current.end()};
        }

        std::vector<QColor> result;
        result.reserve(capacity + 1);
        result.push_back(picked);

        for (const auto &color: current)
        {
            // 同色只留最新那一处：重复项排进来会让同一个颜色占掉两个名额，上限也就名存实亡
            const auto duplicate = std::any_of(result.begin(), result.end(), [&color](const QColor &other) { return other == color; });
            if (!duplicate)
            {
                result.push_back(color);
            }
        }

        // 先排重再截断：容量为 0 时得到空清单，而不是留一条比上限多的记录
        if (result.size() > capacity)
        {
            result.resize(capacity);
        }
        return result;
    }

    QString recentColorToken(const QColor &color)
    {
        // 固定 6 位十六进制：QColor::name() 在带 alpha 的颜色上会给出 8 位，读回那一侧只认 6 位，
        // 两边都用这个显式格式才不会一边写得下、一边读不回
        return QStringLiteral("#%1%2%3").arg(color.red(), 2, 16, QLatin1Char('0')).arg(color.green(), 2, 16, QLatin1Char('0')).arg(color.blue(), 2, 16, QLatin1Char('0')).toUpper();
    }

    QStringList serializeRecentColors(const std::span<const QColor> colors)
    {
        QStringList tokens;
        tokens.reserve(static_cast<qsizetype>(colors.size()));
        for (const auto &color: colors)
        {
            tokens.append(recentColorToken(color));
        }
        return tokens;
    }

    Core::Expected<std::vector<QColor>> parseRecentColors(const QStringList &tokens)
    {
        std::vector<QColor> colors;
        colors.reserve(static_cast<std::size_t>(tokens.size()));

        for (const auto &token: tokens)
        {
            if (token.size() != 7 || token.at(0) != QLatin1Char('#'))
            {
                return Core::Expected<std::vector<QColor>>{std::unexpected{Core::Error{
                        .code    = Core::ErrorCode::InvalidArgument,
                        .message = std::format("最近使用颜色里有一条不是 #RRGGBB 写法：\"{}\"。删掉配置里的 Annotation/RecentColors 这一项可以恢复默认", token.toStdString()),
                }}};
            }

            const auto digits   = token.mid(1);
            bool       accepted = false;
            const auto value    = digits.toUInt(&accepted, 16);
            if (!accepted)
            {
                return Core::Expected<std::vector<QColor>>{std::unexpected{Core::Error{
                        .code    = Core::ErrorCode::InvalidArgument,
                        .message = std::format("最近使用颜色里有一条不是十六进制：\"{}\"。删掉配置里的 Annotation/RecentColors 这一项可以恢复默认", token.toStdString()),
                }}};
            }

            // 用 rgb() 而不是 setColorRgba：后者会把 alpha 也算进去，而这里存的本来就是 6 位不透明色
            colors.push_back(QColor::fromRgb(static_cast<int>(value)));
        }

        return colors;
    }

} // namespace GyanisScribe::Annotation
