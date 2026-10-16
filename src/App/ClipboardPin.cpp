#include "App/ClipboardPin.h"

#include "App/ImageFetcher.h"

#include "Base/Paths.h"
#include "Base/Text.h"
#include "Gui/Pin/ColorCard.h"
#include "Gui/Pin/PinManager.h"

#include <QFontMetrics>
#include <QImageReader>
#include <QMimeData>
#include <QPainter>
#include <QPen>
#include <QUrl>
#include <QVariant>

#include <algorithm>
#include <array>
#include <optional>

namespace GyanisScribe::App
{
    namespace
    {
        /// 一次拖放贴出多张时，后一张相对前一张向右下错开的格数：完全叠住会让人以为只贴上一张
        constexpr int kDropPinCascadeStride = 24;

        /**
         * @brief 看一段文本是不是「一种颜色」
         * @param text 剪贴板里的文本
         * @return 解析出的颜色；不是颜色写法时返回空
         * @details 只认 `#`、`rgb(`、`hsl(` 开头的短单行：QColor::fromString 连 `red` 这种单词都认，
         *          全放开就会把普通英文里恰好出现的颜色词渲染成色卡。
         */
        /**
         * @brief 拆 `rgb(r, g, b)` 与 `hsl(h, s%, l%)` 这两形
         * @param text 已经过前缀粗筛的短单行
         * @return 拆出来的颜色；形状不对、数字不是整数、或取值越界时返回空
         * @details `QColor::fromString` 只认 `#` 与颜色名，这两形得自己拆（实测 fmt 与 hsl 都返回无效色）。
         *          越界不夹取：夹出来的色与用户粘的那串不是同一个，贴出来的是「看着像」的色卡；
         *          认不出就让这段文本走文本卡那条路，用户看得见自己贴的是原文。
         */
        [[nodiscard]] std::optional<QColor> parseFunctionalColor(const QString &text)
        {
            const auto open = text.indexOf(QLatin1Char('('));
            if (open <= 0 || !text.endsWith(QLatin1Char(')')))
            {
                return std::nullopt;
            }
            const auto name  = text.left(open).trimmed().toLower();
            const auto parts = text.mid(open + 1, text.size() - open - 2).split(QLatin1Char(','));
            if (parts.size() != 3)
            {
                return std::nullopt;
            }

            std::array<int, 3>  numbers{};
            std::array<bool, 3> percents{};
            for (std::size_t index = 0; index < numbers.size(); ++index)
            {
                auto part = parts[static_cast<qsizetype>(index)].trimmed();
                if (part.endsWith(QLatin1Char('%')))
                {
                    // 百分号只在 hsl 的饱和与明度那两格上成立：rgb(50%, …) 与 hsl 的色相带它都不是这一套写法
                    percents[index] = true;
                    part.chop(1);
                }
                bool       ok     = false;
                const auto number = part.toInt(&ok);
                if (!ok)
                {
                    return std::nullopt;
                }
                numbers[index] = number;
            }

            const auto within = [](const int value, const int minimum, const int maximum) { return value >= minimum && value <= maximum; };
            if (name == QLatin1String("rgb"))
            {
                if (percents[0] || percents[1] || percents[2] || !within(numbers[0], 0, 255) || !within(numbers[1], 0, 255) || !within(numbers[2], 0, 255))
                {
                    return std::nullopt;
                }
                return QColor{numbers[0], numbers[1], numbers[2]};
            }
            if (name == QLatin1String("hsl"))
            {
                if (percents[0] || !percents[1] || !percents[2] || !within(numbers[0], 0, 360) || !within(numbers[1], 0, 100) || !within(numbers[2], 0, 100))
                {
                    return std::nullopt;
                }
                return QColor::fromHsl(numbers[0], numbers[1] * 255 / 100, numbers[2] * 255 / 100);
            }
            return std::nullopt;
        }
        [[nodiscard]] std::optional<QColor> colorTextOf(const QString &text)
        {
            const auto trimmed = text.trimmed();
            if (trimmed.isEmpty() || trimmed.size() > kMaximumColorTextLength || trimmed.contains(QLatin1Char('\n')))
            {
                return std::nullopt;
            }

            const auto lowered        = trimmed.toLower();
            const auto looksLikeColor = lowered.startsWith(QLatin1Char('#')) || lowered.startsWith(QStringLiteral("rgb(")) || lowered.startsWith(QStringLiteral("hsl("));
            if (!looksLikeColor)
            {
                return std::nullopt;
            }

            // `QColor::fromString` 只认 `#` 与颜色名：rgb()/hsl() 这两形自己拆（实测它俩都返回无效色，
            // 而上面那条前缀白名单正是为它们写的——不补这一句，那两句前缀检查就是死代码）
            const QColor parsed = trimmed.startsWith(QLatin1Char('#')) ? QColor::fromString(trimmed) : QColor{};
            if (!parsed.isValid())
            {
                return parseFunctionalColor(trimmed);
            }
            if (!parsed.isValid())
            {
                return std::nullopt;
            }
            return parsed;
        }
    } // namespace

    Base::Expected<QImage> pinContentFrom(const QMimeData *data, const QFont &font)
    {
        if (data == nullptr)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("剪贴板里没有可以贴的内容：图像、纯文本或颜色都行。先复制一个再来贴")});
        }

        if (data->hasImage())
        {
            // 有图像那一路、却读不出像素：来源只挂了名目没给内容（受保护内容、跨会话拷贝的那类）
            auto image = qvariant_cast<QImage>(data->imageData());
            if (image.isNull())
            {
                return std::unexpected(
                        Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("剪贴板里的图像读不出来（来源可能只挂了名目、没给像素）：换一张图复制，或先截图再来贴")});
            }
            return image;
        }

        if (!data->hasText())
        {
            return std::unexpected(
                    Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("剪贴板里没有可以贴的内容：图像、纯文本或颜色都行。链接与文件这一类还没有转贴图的做法")});
        }

        const auto text = data->text();
        if (const auto color = colorTextOf(text); color)
        {
            return Gui::renderColorCard(*color, text.trimmed());
        }

        const auto trimmed = text.trimmed();
        if (trimmed.isEmpty())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("剪贴板里的文本是空的（只有空白）：先复制一段文字再来贴")});
        }
        if (trimmed.size() > kMaximumTextCardCharacters)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument,
                                               Base::uiFormat(Base::uiTextUtf8("剪贴板里的文本太长（{} 个字，上限 {}）：整张卡片会长到没法看，先把要贴的那一段截短再试"),
                                                              trimmed.size(), static_cast<int>(kMaximumTextCardCharacters))});
        }
        return renderTextCard(trimmed, font);
    }

    Base::Expected<DropContent> dropContentFrom(const QMimeData *data, const QFont &font)
    {
        if (data == nullptr)
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, Base::uiTextUtf8("拖进来的东西没有可读的内容，贴不出来")});
        }

        // 有文件就按文件走：拖一叠图片进来该贴出一叠，不是只看第一件
        if (data->hasUrls())
        {
            DropContent content;
            for (const auto &url: data->urls())
            {
                // 链接（网页地址等）按文本卡片贴：URL 本身就是一行字，渲染成卡片才立得住
                if (!url.isLocalFile())
                {
                    content.contents.push_back(PinContent{renderTextCard(url.toString(), font), QString(), {}});
                    continue;
                }

                const auto   path = Base::Paths::fromDisplayText(url.toLocalFile());
                QImageReader reader{Base::Paths::toDisplayText(path)};
                // EXIF 里带方向的照片按显示方向摆正：贴出来的该是「相机给我们看的样子」
                reader.setAutoTransform(true);
                const QImage image = reader.read();
                if (image.isNull())
                {
                    content.rejectedNames.append(url.fileName());
                    continue;
                }

                PinContent single;
                single.image = image;
                if (reader.supportsAnimation() && reader.imageCount() > 1)
                {
                    // 动图把来源路径一起带走：贴上去会接着播（与布局恢复那一路同解）
                    single.animatedSource = Base::Paths::toDisplayText(path);
                }
                content.contents.push_back(std::move(single));
            }

            if (!content.contents.empty())
            {
                return content;
            }
            return std::unexpected(
                    Base::Error{Base::ErrorCode::InvalidArgument, Base::uiFormat(Base::uiTextUtf8("拖进来的文件不是图片或读不出来：{}。图片文件、链接与文本都可以贴"),
                                                                                 content.rejectedNames.join(QStringLiteral("、")).toStdString())});
        }

        // 没有文件：退回剪贴板那套单件内容（图像 / 颜色卡 / 文本卡），两条路不会将来只改一处
        const auto single = pinContentFrom(data, font);
        if (!single)
        {
            return std::unexpected(single.error());
        }
        DropContent content;
        // 颜色写法这一路要把「原本是哪个色」一起带下去：贴出来的那张色卡以后能改色
        content.contents.push_back(PinContent{*single, QString(), colorCardColorOf(data)});
        return content;
    }

    Base::Expected<DropPinOutcome> pinDroppedContent(Gui::PinManager &pins, const QMimeData *data, const QPoint &globalTopLeft, const QFont &font)
    {
        const auto content = dropContentFrom(data, font);
        if (!content)
        {
            return std::unexpected(content.error());
        }

        DropPinOutcome outcome;
        for (const auto &single: content->contents)
        {
            const auto at = globalTopLeft + QPoint{kDropPinCascadeStride * outcome.pinnedCount, kDropPinCascadeStride * outcome.pinnedCount};
            pins.addPin(single.image, at, single.animatedSource, single.colorCard);
            ++outcome.pinnedCount;
        }
        outcome.rejectedNames = content->rejectedNames;
        return outcome;
    }

    QImage renderTextCard(const QString &text, const QFont &font)
    {
        QFont cardFont = font;
        cardFont.setPointSize(Gui::kCardFontPointSize);

        const auto metrics   = QFontMetrics{cardFont};
        const auto textWidth = kTextCardMaximumWidth - 2 * kTextCardPadding;
        // 先按词折行；整段里若有一个断不开的长词（URL、base64、一长串无空格的字母），
        // 再把这一档换成按字符断——否则卡片会被那一个词撑到几千像素宽
        auto  wrapFlags = Qt::TextWordWrap;
        QRect wrapped   = metrics.boundingRect(QRect{0, 0, textWidth, 100000}, wrapFlags, text);
        if (wrapped.width() > textWidth)
        {
            wrapFlags = Qt::TextWrapAnywhere;
            wrapped   = metrics.boundingRect(QRect{0, 0, textWidth, 100000}, wrapFlags, text);
        }
        const QSize cardSize{std::max(kTextCardMinimumWidth, wrapped.width() + 2 * kTextCardPadding), wrapped.height() + 2 * kTextCardPadding};

        QImage card{cardSize, QImage::Format_ARGB32_Premultiplied};
        card.fill(Gui::kCardBackground);

        QPainter painter{&card};
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen{Gui::kCardBorder, 1});
        painter.drawRect(QRectF{0.5, 0.5, cardSize.width() - 1.0, cardSize.height() - 1.0});
        painter.setFont(cardFont);
        painter.setPen(Gui::kCardText);
        // 量的时候用的哪一档，画的时候用同一档：两处各写一个会把断行位置分开
        painter.drawText(QRect{kTextCardPadding, kTextCardPadding, wrapped.width(), wrapped.height()}, wrapFlags, text);
        painter.end();
        return card;
    }

    std::optional<QUrl> imageUrlOf(const QMimeData *data)
    {
        if (data == nullptr || !data->hasUrls())
        {
            return std::nullopt;
        }
        const auto urls = data->urls();
        if (urls.size() != 1)
        {
            return std::nullopt;
        }
        return isFetchableImageUrl(urls.front()) ? std::optional<QUrl>{urls.front()} : std::nullopt;
    }

    std::optional<QColor> colorCardColorOf(const QMimeData *data)
    {
        if (data == nullptr || !data->hasText())
        {
            return std::nullopt;
        }
        return colorTextOf(data->text());
    }
} // namespace GyanisScribe::App
