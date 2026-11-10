#include "Core/Share/ShareRenderer.h"

#include "Base/Text.h"
#include "Core/Image/SoftShadow.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPoint>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace
{
    [[nodiscard]] GyanisScribe::Base::Error invalidArgument(std::string message)
    {
        return GyanisScribe::Base::Error{GyanisScribe::Base::ErrorCode::InvalidArgument, std::move(message)};
    }

    [[nodiscard]] GyanisScribe::Base::Error internalError(std::string message)
    {
        return GyanisScribe::Base::Error{GyanisScribe::Base::ErrorCode::InternalError, std::move(message)};
    }

    [[nodiscard]] GyanisScribe::Base::Error emptyImageError()
    {
        return invalidArgument(GyanisScribe::Base::uiTextUtf8("这张图是空的，没有可美化的内容：请先截一张图再分享"));
    }

    [[nodiscard]] GyanisScribe::Base::Error canvasEdgeTooLargeError()
    {
        return invalidArgument(GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("美化画布超过了 {} 像素的边长上限：请缩小截图或减少内边距与投影"),
                                                            GyanisScribe::Core::kMaximumShareCanvasEdge));
    }

    [[nodiscard]] std::string canvasTooLargeMessage()
    {
        return GyanisScribe::Base::uiTextUtf8("美化画布太大，内存放不下：请缩小截图或减少内边距与投影");
    }

    [[nodiscard]] GyanisScribe::Base::Error canvasPixelLimitError()
    {
        return invalidArgument(canvasTooLargeMessage());
    }

    [[nodiscard]] GyanisScribe::Base::Error canvasAllocationError()
    {
        return internalError(canvasTooLargeMessage());
    }

    [[nodiscard]] GyanisScribe::Base::Error scaleFailedError()
    {
        return internalError(GyanisScribe::Base::uiTextUtf8("美化图片没能缩放：内存不够，请缩小截图或减少内边距与投影"));
    }

    /// 背景总是不透明：样式里的颜色只取 RGB，忽略可能残留的 alpha
    [[nodiscard]] QColor opaqueColor(const QColor &color)
    {
        return QColor{color.red(), color.green(), color.blue()};
    }

    /// 预览缩放后的像素尺寸：至少留 1 格，避免小图上整段几何缩成 0
    [[nodiscard]] int scaledPixelSpan(const int pixels, const double scale)
    {
        return std::max(1, static_cast<int>(std::lround(static_cast<double>(pixels) * scale)));
    }
} // namespace

namespace GyanisScribe::Core
{

    QSize shareCanvasSize(const QSize &imageSize, const ShareStyle &style) noexcept
    {
        const auto chrome = style.editorChrome ? kShareEditorChromeHeight : 0;
        const auto outer  = style.paddingPixels + (style.shadow ? style.shadowPixels : 0);
        return QSize{imageSize.width() + 2 * outer, imageSize.height() + chrome + 2 * outer};
    }

    Base::Expected<QImage> renderShareImage(const QImage &image, const ShareStyle &style, const int maximumDimension)
    {
        if (image.isNull())
        {
            return std::unexpected(emptyImageError());
        }
        if (const auto validated = validateShareStyle(style); !validated.has_value())
        {
            return std::unexpected(validated.error());
        }
        if (static_cast<qint64>(image.width()) * static_cast<qint64>(image.height()) > kMaximumShareCanvasPixels)
        {
            return std::unexpected(canvasPixelLimitError());
        }
        const auto full = shareCanvasSize(image.size(), style);
        if (full.width() > kMaximumShareCanvasEdge || full.height() > kMaximumShareCanvasEdge)
        {
            return std::unexpected(canvasEdgeTooLargeError());
        }

        const auto outputWidth = maximumDimension > 0 ? std::min(maximumDimension, full.width()) : full.width();
        const auto scale       = static_cast<double>(outputWidth) / static_cast<double>(full.width());
        const auto fullOuter   = style.paddingPixels + (style.shadow ? style.shadowPixels : 0);
        // 留白本来就是 0 时画布贴着卡片，不能凭空加一格；只有存在的留白在预览缩小时才兜底到 1 格
        const auto outer       = fullOuter > 0 ? scaledPixelSpan(fullOuter, scale) : 0;
        const auto imageWidth  = std::max(1, outputWidth - 2 * outer);
        const auto imageHeight = std::max(1, static_cast<int>(std::lround(static_cast<double>(image.height()) * scale)));
        const auto chrome      = style.editorChrome ? scaledPixelSpan(kShareEditorChromeHeight, scale) : 0;
        const auto canvasSize  = QSize{outputWidth, imageHeight + chrome + 2 * outer};

        const auto scaledImage = image.scaled(imageWidth, imageHeight, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (scaledImage.isNull())
        {
            return std::unexpected(scaleFailedError());
        }

        QImage canvas{canvasSize, QImage::Format_ARGB32_Premultiplied};
        if (canvas.isNull())
        {
            return std::unexpected(canvasAllocationError());
        }

        const auto card   = QRect{outer, outer, imageWidth, imageHeight + chrome};
        const auto radius = std::min(static_cast<int>(std::lround(static_cast<double>(style.cornerRadiusPixels) * scale)), std::min(card.width(), card.height()) / 2);

        QPainter painter{&canvas};
        if (style.gradient)
        {
            QLinearGradient gradient{0.0, 0.0, 0.0, static_cast<qreal>(canvasSize.height())};
            gradient.setColorAt(0.0, opaqueColor(style.backgroundTop));
            gradient.setColorAt(1.0, opaqueColor(style.backgroundBottom));
            painter.fillRect(canvas.rect(), gradient);
        } else
        {
            painter.fillRect(canvas.rect(), opaqueColor(style.backgroundTop));
        }

        if (style.shadow)
        {
            const auto shadowLayer = makeSoftShadowLayer(canvasSize, card, radius, scaledPixelSpan(style.shadowPixels, scale), kSoftShadowMaximumAlpha);
            if (!shadowLayer.isNull())
            {
                painter.drawImage(QPoint{0, 0}, shadowLayer);
            }
        }

        QPainterPath cardPath;
        cardPath.addRoundedRect(card, radius, radius);
        painter.save();
        painter.setClipPath(cardPath);

        if (chrome > 0)
        {
            painter.fillRect(QRect{card.left(), card.top(), card.width(), chrome}, style.editorChromeColor);

            const auto                  dotRadius  = scaledPixelSpan(5, scale);
            const auto                  dotSpacing = dotRadius * 2 + scaledPixelSpan(8, scale);
            const auto                  firstDotX  = card.left() + scaledPixelSpan(16, scale) + dotRadius;
            const auto                  dotY       = card.top() + chrome / 2;
            const std::array<QColor, 3> dotColors{QColor{0xFF, 0x5F, 0x57}, QColor{0xFE, 0xBC, 0x2E}, QColor{0x28, 0xC8, 0x40}};
            painter.setPen(Qt::NoPen);
            for (std::size_t index = 0; index < dotColors.size(); ++index)
            {
                painter.setBrush(dotColors[index]);
                painter.drawEllipse(QPoint{firstDotX + static_cast<int>(index) * dotSpacing, dotY}, dotRadius, dotRadius);
            }

            if (!style.editorTitle.isEmpty())
            {
                auto titleFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
                titleFont.setPixelSize(std::max(8, static_cast<int>(std::lround(22.0 * scale))));
                painter.setFont(titleFont);
                painter.setPen(style.editorTitleColor);

                const auto metrics   = QFontMetrics{titleFont};
                const auto titleX    = firstDotX + 2 * dotSpacing + dotRadius + scaledPixelSpan(12, scale);
                const auto available = std::max(1, card.right() - titleX - scaledPixelSpan(16, scale));
                const auto title     = metrics.elidedText(style.editorTitle, Qt::ElideRight, available);
                const auto baseline  = card.top() + chrome / 2 + (metrics.ascent() - metrics.descent()) / 2;
                painter.drawText(QPoint{titleX, baseline}, title);
            }
        }

        painter.drawImage(QPoint{card.left(), card.top() + chrome}, scaledImage);
        painter.restore();

        if (style.border && style.borderPixels > 0)
        {
            const auto penWidth   = scaledPixelSpan(style.borderPixels, scale);
            const auto borderRect = card.adjusted(penWidth / 2, penWidth / 2, -penWidth / 2, -penWidth / 2);
            if (!borderRect.isEmpty())
            {
                const auto borderRadius = std::min(radius, std::min(borderRect.width(), borderRect.height()) / 2);
                painter.setPen(QPen{style.borderColor, static_cast<qreal>(penWidth)});
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(borderRect, borderRadius, borderRadius);
            }
        }

        if (!style.watermarkText.isEmpty())
        {
            QFont watermarkFont{};
            watermarkFont.setPixelSize(std::max(8, static_cast<int>(std::lround(20.0 * scale))));
            painter.setFont(watermarkFont);

            const auto metrics   = QFontMetrics{watermarkFont};
            const auto margin    = scaledPixelSpan(16, scale);
            const auto available = std::max(1, card.width() - 2 * margin);
            const auto text      = metrics.elidedText(style.watermarkText, Qt::ElideRight, available);
            const auto textWidth = metrics.horizontalAdvance(text);
            const auto x         = style.watermarkCorner == ShareWatermarkCorner::BottomRight ? card.right() - margin - textWidth : card.left() + margin;
            const auto baseline  = card.bottom() - margin - metrics.height() + metrics.ascent();

            // 水印贴着卡片下沿，渐变时的底色以终点色为准：拿上沿色去判深浅会在上下反差大的模板上看不见
            const auto base      = opaqueColor(style.gradient ? style.backgroundBottom : style.backgroundTop);
            const auto luminance = 0.299 * static_cast<double>(base.red()) + 0.587 * static_cast<double>(base.green()) + 0.114 * static_cast<double>(base.blue());
            painter.setPen(luminance >= 140.0 ? QColor{0x33, 0x33, 0x33} : QColor{Qt::white});
            painter.setOpacity(static_cast<qreal>(style.watermarkOpacity) / 255.0);
            painter.drawText(QPoint{x, baseline}, text);
        }

        painter.end();
        return canvas;
    }

} // namespace GyanisScribe::Core
