#include "Core/Image/OutputDecoration.h"

#include "Base/Text.h"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace
{
    /// 装饰件自己的取整：投影按逐像素算，边框按整圈算，画布尺寸必须与两者一致地加出来
    [[nodiscard]] int paddingFor(const bool border, const bool shadow, const GyanisScribe::Core::OutputDecorationStyle &style)
    {
        return (border ? style.borderPixels : 0) + (shadow ? style.shadowPixels : 0);
    }

    /// 这一列（或这一行）在闭区间矩形之外多出几格：矩形内为 0
    /// @details 矩形的边界按闭区间算（right/bottom 那一列像素仍在矩形上），与 QRect 的取点口径一致：
    ///          少算那一格会让紧贴边框的一圈投影凭空深一格，画出来是一条看得见的硬边
    [[nodiscard]] int axisExcess(const int index, const int low, const int high)
    {
        return std::max({low - index, 0, index - high});
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
        canvas.fill(fillBackground ? Qt::white : Qt::transparent);
        const auto imageRect = QRect{padding, image.size()};

        // 投影单独画在一层透明图上，最后以「源覆盖」贴到画布：垫白那一路白底要从投影下面透出来，
        // 直接往画布写像素会把垫底整格顶掉（黑像素带 alpha 写上去，白底就不见了）
        QImage shadowLayer;
        if (shadow)
        {
            shadowLayer = QImage{canvasSize, QImage::Format_ARGB32_Premultiplied};
            shadowLayer.fill(Qt::transparent);
            // 投影源头取「成品 + 边框」所占的那一块：渐隐从边框外沿起算，贴边那一圈最深。
            // 成品内部一格都不碰——图里本身有透明像素时，投影垫在底下会透出来
            const auto borderSpan = border ? imageRect.adjusted(-style.borderPixels, -style.borderPixels, style.borderPixels, style.borderPixels) : imageRect;

            // 投影半径之外 alpha 已归零，画布上真正要写的只有成品四周那一圈。按上、下两条整幅宽，
            // 左右两条只走中间的行切分：四个角各归上/下带，每个像素只写一次，成品内部一行都不遍历。
            const auto shadowSpan = std::max(style.shadowPixels, 0);
            if (shadowSpan > 0)
            {
                // alpha 只取决于横竖两轴各自离边框外沿几格：先把这两份格数按列/按行算好，
                // 再把所有可能的组合查表算一次，逐像素时只剩一次查表与一次写入
                const auto       tableSpan       = static_cast<std::size_t>(shadowSpan + 1);
                std::vector<QRgb> shadowTable(tableSpan * tableSpan, 0);
                for (int excessY = 0; excessY < shadowSpan; ++excessY)
                {
                    for (int excessX = 0; excessX < shadowSpan; ++excessX)
                    {
                        const auto distance = std::sqrt(static_cast<double>(excessX * excessX + excessY * excessY));
                        if (distance >= shadowSpan)
                        {
                            continue;
                        }
                        // 距离平方衰减：紧贴那一圈最深，到边界正好为零，不会留下一条看得见的硬边
                        const auto falloff = 1.0 - distance / static_cast<double>(shadowSpan);
                        const auto alpha   = static_cast<int>(std::lround(kOutputShadowMaximumAlpha * falloff * falloff));
                        if (alpha > 0)
                        {
                            shadowTable[static_cast<std::size_t>(excessY) * tableSpan + static_cast<std::size_t>(excessX)] = qRgba(0, 0, 0, alpha);
                        }
                    }
                }

                std::vector<int> columnExcess(static_cast<std::size_t>(canvasSize.width()));
                for (int x = 0; x < canvasSize.width(); ++x)
                {
                    columnExcess[static_cast<std::size_t>(x)] = axisExcess(x, borderSpan.left(), borderSpan.right());
                }
                std::vector<int> rowExcess(static_cast<std::size_t>(canvasSize.height()));
                for (int y = 0; y < canvasSize.height(); ++y)
                {
                    rowExcess[static_cast<std::size_t>(y)] = axisExcess(y, borderSpan.top(), borderSpan.bottom());
                }

                auto *const shadowPixels = reinterpret_cast<QRgb *>(shadowLayer.bits());
                const auto  shadowStride = shadowLayer.bytesPerLine() / static_cast<int>(sizeof(QRgb));
                const auto  writeShadowRow = [&](const int y, const int firstColumn, const int lastColumn)
                {
                    auto *const       scanline = shadowPixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(shadowStride);
                    const auto *const alphaRow = shadowTable.data() + static_cast<std::size_t>(rowExcess[static_cast<std::size_t>(y)]) * tableSpan;
                    for (int x = firstColumn; x <= lastColumn; ++x)
                    {
                        scanline[x] = alphaRow[static_cast<std::size_t>(columnExcess[static_cast<std::size_t>(x)])];
                    }
                };

                const auto lastRow    = canvasSize.height() - 1;
                const auto lastColumn = canvasSize.width() - 1;
                // 上、下两条整幅宽：四角的投影只在这里写
                for (int y = 0; y < imageRect.top(); ++y)
                {
                    writeShadowRow(y, 0, lastColumn);
                }
                for (int y = imageRect.bottom() + 1; y <= lastRow; ++y)
                {
                    writeShadowRow(y, 0, lastColumn);
                }
                // 左右两条只补成品所占的那几行：角上的格子已归上、下带
                for (int y = imageRect.top(); y <= imageRect.bottom(); ++y)
                {
                    writeShadowRow(y, 0, imageRect.left() - 1);
                    writeShadowRow(y, imageRect.right() + 1, lastColumn);
                }
            }
        }

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
