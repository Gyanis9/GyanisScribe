#include "Core/Pick/ColorFormat.h"

#include "Base/Text.h"

#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace
{
    using GyanisScribe::Core::ColorFormat;
    using GyanisScribe::Core::kColorFormatCount;

    /// 存储标记与界面名按下标互指，顺序与 ColorFormat 一致
    constexpr std::array<std::string_view, kColorFormatCount> kFormatTokens = {"Hex", "Rgb", "Hsl", "Hsv", "Hsi", "Decimal"};
    constexpr std::array<std::string_view, kColorFormatCount> kFormatLabels = {"HEX", "RGB", "HSL", "HSV", "HSI", "十进制"};

    /// Qt 的浮点色相/饱和/明度分量都是 0..1，写成人看的写法要乘回角度与百分数
    constexpr double kDegreeFullTurn   = 360.0;
    constexpr double kPercentFullScale = 100.0;

    /// 无彩色时 Qt 给 -1，按各行惯例落到 0；负分量只可能来自这个情形
    [[nodiscard]] double nonNegative(const qreal component)
    {
        return component < 0.0 ? 0.0 : component;
    }

    [[nodiscard]] int percentOf(const double normalized)
    {
        return static_cast<int>(std::lround(normalized * kPercentFullScale));
    }

    [[nodiscard]] int degreesOf(const QColor &color)
    {
        return static_cast<int>(std::lround(nonNegative(color.hueF()) * kDegreeFullTurn));
    }
} // namespace

namespace GyanisScribe::Core
{
    std::string_view colorFormatToken(const ColorFormat format)
    {
        const auto index = static_cast<std::size_t>(format);
        return index < kFormatTokens.size() ? kFormatTokens[index] : kFormatTokens[0];
    }

    QString colorFormatLabel(const ColorFormat format)
    {
        // 标签与标记分开：配置里存 Hex，界面上显示 HEX，改大小写不该动已经写下的配置
        const auto index = static_cast<std::size_t>(format);
        return GyanisScribe::Base::uiText(index < kFormatLabels.size() ? kFormatLabels[index].data() : kFormatLabels[0].data());
    }

    std::optional<ColorFormat> colorFormatAt(const int index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= kColorFormatCount)
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
            labels.append(GyanisScribe::Base::uiText(label));
        }
        return labels;
    }

    Base::Expected<ColorFormat> parseColorFormat(const std::string_view token)
    {
        for (std::size_t index = 0; index < kFormatTokens.size(); ++index)
        {
            if (kFormatTokens[index] == token)
            {
                return static_cast<ColorFormat>(index);
            }
        }

        return Base::Expected<ColorFormat>{std::unexpected{Base::Error{
                .code    = Base::ErrorCode::InvalidArgument,
                .message = GyanisScribe::Base::uiFormat(
                        GyanisScribe::Base::uiTextUtf8("颜色写法 \"{}\" 不认识。可用取值只有 Hex、Rgb、Hsl、Hsv、Hsi 与 Decimal（区分大小写），请在设置的取色页里重选一次"), token),
        }}};
    }

    ColorFormat colorFormatOrDefault(const std::string_view token, std::string &complaint)
    {
        complaint.clear();
        const auto parsed = parseColorFormat(token);
        if (parsed)
        {
            return *parsed;
        }
        complaint = parsed.error().message;
        return ColorFormat::Hex;
    }

    QString formatColor(const QColor &color, const ColorFormat format, const HexCasing casing)
    {
        // 字母档只管 HEX 那一串：QColor::name() 给的是小写，大写档在它之后再折一次
        const auto hexText = [&color, casing]
        {
            const auto lower = color.name(QColor::HexRgb);
            return casing == HexCasing::Uppercase ? lower.toUpper() : lower;
        };

        switch (format)
        {
            case ColorFormat::Rgb:
                return QString::fromUtf8(std::format("rgb({}, {}, {})", color.red(), color.green(), color.blue()).c_str());
            case ColorFormat::Hsl:
                // hueF 是 HSL 色相，hslSaturationF 才是 HSL 饱和（saturationF 给的是 HSV 那一套，
                // 同一个色抄进 CSS 会差出一截）
                return QString::fromUtf8(
                        std::format("hsl({}, {}%, {}%)", degreesOf(color), percentOf(nonNegative(color.hslSaturationF())), percentOf(nonNegative(color.lightnessF()))).c_str());
            case ColorFormat::Hsv:
                // 设计工具的取色盘多用 HSV 这一套：saturationF / valueF 就是它那两个分量
                return QString::fromUtf8(
                        std::format("hsv({}, {}%, {}%)", degreesOf(color), percentOf(nonNegative(color.saturationF())), percentOf(nonNegative(color.valueF()))).c_str());
            case ColorFormat::Hsi:
            {
                // HSI 的分量定义：I 是三分量均值，S = 1 - min/I（I 为 0 即纯黑，按惯例饱和记 0）
                const auto redF       = static_cast<double>(color.redF());
                const auto greenF     = static_cast<double>(color.greenF());
                const auto blueF      = static_cast<double>(color.blueF());
                const auto intensity  = (redF + greenF + blueF) / 3.0;
                const auto saturation = intensity <= 0.0 ? 0.0 : 1.0 - std::min({redF, greenF, blueF}) / intensity;
                return QString::fromUtf8(std::format("hsi({}, {}%, {}%)", degreesOf(color), percentOf(saturation), percentOf(intensity)).c_str());
            }
            case ColorFormat::Decimal:
                // 十进制三元组：逗号加空格分隔，粘进代码当数组元素、粘进设计工具的 RGB 输入栏都是这个形状
                return QString::fromUtf8(std::format("{}, {}, {}", color.red(), color.green(), color.blue()).c_str());
            case ColorFormat::Hex:
                return hexText();
        }
        // 写法表加了一档却忘了接分支：交出去的是 HEX 那一形，而不是空串——空串会被粘进对面当空值
        return hexText();
    }

    HexCasing hexCasingOf(const bool uppercase)
    {
        return uppercase ? HexCasing::Uppercase : HexCasing::Lowercase;
    }

} // namespace GyanisScribe::Core
