#include "Core/Image/OutputDecoration.h"

#include "Base/Text.h"
#include "Core/Image/SoftShadow.h"

#include <QPainter>

#include <format>

namespace
{
    /// 装饰件自己的取整：投影按逐像素算，边框按整圈算，画布尺寸必须与两者一致地加出来
    [[nodiscard]] int paddingFor(const bool border, const bool shadow, const GyanisScribe::Core::OutputDecorationStyle &style)
    {
        return (border ? style.borderPixels : 0) + (shadow ? style.shadowPixels : 0);
    }

    /// 越界拒绝的同一句话：三个校验口只差键名与范围，文案口径要一致
    [[nodiscard]] GyanisScribe::Base::Error outOfRange(std::string_view key, int stored, int minimum, int maximum)
    {
        return GyanisScribe::Base::Error{
                GyanisScribe::Base::ErrorCode::InvalidArgument,
                GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("{} \"{}\" 不在 {} 到 {} 之间：请在设置的截图页重选一个值，或删掉这一项回到默认档"), std::string{key},
                                             stored, minimum, maximum),
        };
    }
} // namespace

namespace GyanisScribe::Core
{

    Base::Expected<int> parseOutputBorderPixels(const int stored)
    {
        if (stored >= kMinimumOutputBorderPixels && stored <= kMaximumOutputBorderPixels)
        {
            return stored;
        }
        return std::unexpected(outOfRange("Save/OutputBorderWidth", stored, kMinimumOutputBorderPixels, kMaximumOutputBorderPixels));
    }

    Base::Expected<int> parseOutputShadowPixels(const int stored)
    {
        if (stored >= kMinimumOutputShadowPixels && stored <= kMaximumOutputShadowPixels)
        {
            return stored;
        }
        return std::unexpected(outOfRange("Save/OutputShadowWidth", stored, kMinimumOutputShadowPixels, kMaximumOutputShadowPixels));
    }

    Base::Expected<QColor> parseOutputBorderColor(const QString &stored)
    {
        if (stored.trimmed().isEmpty())
        {
            // 没存过就是默认档：这一条不是「读不懂」，不该报成坏值
            return kDefaultOutputBorderColor;
        }
        // Qt 6 的 setNamedColor 没有返回值，判「认不认得」要用 isValidColorName：它认 CSS 风格的颜色名，
        // 不只是 #RRGGBB——界面提示写的是 #RRGGBB，多收下的那些写法（如 red）也在同一档里能用
        const auto text = stored.trimmed();
        if (!QColor::isValidColorName(text))
        {
            return std::unexpected(Base::Error{
                    Base::ErrorCode::InvalidArgument,
                    GyanisScribe::Base::uiFormat(
                            GyanisScribe::Base::uiTextUtf8("边框颜色 \"{}\" 不是一段认得出的颜色写法（如 #RRGGBB）：请在设置的截图页重填，或留空回到默认的中灰"),
                            stored.toStdString()),
            });
        }
        return QColor{text};
    }

    QSize decoratedOutputSize(const QSize &imageSize, const bool border, const bool shadow, const OutputDecorationStyle &style)
    {
        const auto padding = paddingFor(border, shadow, style);
        return QSize{imageSize.width() + padding * 2, imageSize.height() + padding * 2};
    }

    QImage decorateForOutput(const QImage &image, const bool border, const bool shadow, const bool fillBackground, const OutputDecorationStyle &style)
    {
        if (image.isNull() || (!border && !shadow && !fillBackground))
        {
            // 没有任何一档要做的事：原样交回，调用方不必在外面再包一层判空
            return image;
        }

        // 画布是把成品四周各撑开一份内边距；那份内边距由同一个式子推出来，
        // 图的落点与尺寸核算共用它，不会出现「投影多一圈、图偏一格」这类只在特定宽高下现形的错位
        const auto canvasSize = decoratedOutputSize(image.size(), border, shadow, style);
        const auto padding    = QPoint{(canvasSize.width() - image.width()) / 2, (canvasSize.height() - image.height()) / 2};
        QImage     canvas{canvasSize, QImage::Format_ARGB32_Premultiplied};
        if (canvas.isNull())
        {
            // 尺寸算出天量时 Qt 分不出画布，交回的是一张空图：原样交回，别拿它去建 QPainter
            return canvas;
        }
        canvas.fill(fillBackground ? Qt::white : Qt::transparent);
        const auto imageRect = QRect{padding, image.size()};

        // 投影单独画在一层透明图上，最后以「源覆盖」贴到画布：垫白那一路白底要从投影下面透出来，
        // 直接往画布写像素会把垫底整格顶掉（黑像素带 alpha 写上去，白底就不见了）
        const auto borderSpan  = border ? imageRect.adjusted(-style.borderPixels, -style.borderPixels, style.borderPixels, style.borderPixels) : imageRect;
        const auto shadowLayer = shadow ? makeSoftShadowLayer(canvasSize, borderSpan, 0, style.shadowPixels, kOutputShadowMaximumAlpha) : QImage{};

        QPainter painter{&canvas};
        if (!shadowLayer.isNull())
        {
            painter.drawImage(QPoint{0, 0}, shadowLayer);
        }
        if (border)
        {
            // 边框在成品四周各加宽 style.borderPixels 圈（矩形两端都是闭区间，右下要多走一格）
            painter.fillRect(imageRect.adjusted(-style.borderPixels, -style.borderPixels, style.borderPixels, style.borderPixels), style.borderColor);
        }
        painter.drawImage(imageRect.topLeft(), image);
        painter.end();
        return canvas;
    }

} // namespace GyanisScribe::Core
