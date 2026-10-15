#include "Gui/Pin/ColorCard.h"

#include <QFont>
#include <QPainter>
#include <QPen>

namespace GyanisScribe::Gui
{
    QImage renderColorCard(const QColor &color, const QString &label)
    {
        const QSize cardSize{kColorCardWidth, kColorCardHeight};
        QImage      card{cardSize, QImage::Format_ARGB32_Premultiplied};
        card.fill(color);

        QPainter    painter{&card};
        const QRect strip{0, cardSize.height() - kColorCardStripHeight, cardSize.width(), kColorCardStripHeight};
        painter.fillRect(strip, kCardBackground);
        painter.setPen(QPen{kCardBorder, 1});
        painter.drawLine(strip.topLeft(), strip.topRight());

        QFont labelFont = painter.font();
        labelFont.setPointSize(kCardFontPointSize);
        painter.setFont(labelFont);
        painter.setPen(kCardText);
        painter.drawText(strip, Qt::AlignCenter, label);
        painter.end();
        return card;
    }

} // namespace GyanisScribe::Gui
