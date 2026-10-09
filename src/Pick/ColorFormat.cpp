#include "Pick/ColorFormat.h"

#include <QStringList>

#include <array>
#include <cmath>
#include <format>

namespace
{
    using GyanisScribe::Pick::ColorFormat;

    /// 存储标记与界面名按下标互指，顺序与 ColorFormat 一致
    constexpr std::array<std::string_view, 3> kFormatTokens = {"Hex", "Rgb", "Hsl"};
    constexpr std::array<std::string_view, 3> kFormatLabels = {"HEX", "RGB", "HSL"};

    /// Qt 的浮点 HSL 三个分量都是 0..1，写成人看的写法要乘回角度与百分数
    constexpr double kDegreeFullTurn   = 360.0;
    constexpr double kPercentFullScale = 100.0;
} // namespace

namespace GyanisScribe::Pick
{
    std::string_view colorFormatToken(const ColorFormat format)
    {
        switch (format)
        {
            case ColorFormat::Hex:
                return kFormatTokens[0];
            case ColorFormat::Rgb:
                return kFormatTokens[1];
            case ColorFormat::Hsl:
                return kFormatTokens[2];
        }
        return kFormatTokens[0];
    }

    QString colorFormatLabel(const ColorFormat format)
    {
        // 标签与标记分开：配置里存 Hex，界面上显示 HEX，改大小写不该动已经写下的配置
        switch (format)
        {
            case ColorFormat::Hex:
                return QString::fromUtf8(kFormatLabels[0].data(), static_cast<qsizetype>(kFormatLabels[0].size()));
            case ColorFormat::Rgb:
                return QString::fromUtf8(kFormatLabels[1].data(), static_cast<qsizetype>(kFormatLabels[1].size()));
            case ColorFormat::Hsl:
                return QString::fromUtf8(kFormatLabels[2].data(), static_cast<qsizetype>(kFormatLabels[2].size()));
        }
        return QString::fromUtf8(kFormatLabels[0].data(), static_cast<qsizetype>(kFormatLabels[0].size()));
    }

    std::optional<ColorFormat> colorFormatAt(const int index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= kFormatTokens.size())
        {
            return {};
        }
        return static_cast<ColorFormat>(index);
    }

    int indexOfColorFormat(const ColorFormat format)
    {
        return static_cast<int>(static_cast<std::size_t>(format));
    }

    QStringList colorFormatLabels()
    {
        QStringList labels;
        for (const auto &label: kFormatLabels)
        {
            labels.append(QString::fromUtf8(label.data(), static_cast<qsizetype>(label.size())));
        }
        return labels;
    }

    Core::Expected<ColorFormat> parseColorFormat(const std::string_view token)
    {
        for (std::size_t index = 0; index < kFormatTokens.size(); ++index)
        {
            if (kFormatTokens[index] == token)
            {
                return static_cast<ColorFormat>(index);
            }
        }

        return Core::Expected<ColorFormat>{std::unexpected{Core::Error{
                .code    = Core::ErrorCode::InvalidArgument,
                .message = std::format("颜色写法 \"{}\" 不认识。可用取值只有 Hex、Rgb 与 Hsl（区分大小写），请在设置的取色页里重选一次", token),
        }}};
    }

    QString formatColor(const QColor &color, const ColorFormat format)
    {
        switch (format)
        {
            case ColorFormat::Rgb:
                return QString::fromUtf8(std::format("rgb({}, {}, {})", color.red(), color.green(), color.blue()).c_str());
            case ColorFormat::Hsl:
            {
                // hueF 是 HSL 色相，hslSaturationF 才是 HSL 饱和（saturationF 给的是 HSV 那一套，
                // 同一个色抄进 CSS 会差出一截）；无彩色时 Qt 给 -1，按 CSS 惯例落到 0
                const auto nonNegative = [](const qreal component) { return component < 0.0 ? 0.0 : component; };
                return QString::fromUtf8(std::format("hsl({}, {}%, {}%)", static_cast<int>(std::lround(nonNegative(color.hueF()) * kDegreeFullTurn)),
                                                     static_cast<int>(std::lround(nonNegative(color.hslSaturationF()) * kPercentFullScale)),
                                                     static_cast<int>(std::lround(nonNegative(color.lightnessF()) * kPercentFullScale)))
                                                 .c_str());
            }
            case ColorFormat::Hex:
                return color.name(QColor::HexRgb).toUpper();
        }
        return color.name(QColor::HexRgb).toUpper();
    }

} // namespace GyanisScribe::Pick
