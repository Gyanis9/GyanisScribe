#include "Annotation/TextTool.h"

#include "Annotation/TextFont.h"
#include "Annotation/TextLayout.h"
#include "Core/Text.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

namespace GyanisScribe::Annotation
{
    namespace
    {
        /// 描边线宽相对字号的比例：太大糊成一坨、太小看不见
        constexpr double kTextOutlineWidthPerFontSize = 0.125;

        /// 底板透明度（0-255）：近实体又透一点底，压得住杂背景
        constexpr int kTextPlateAlpha = 200;

        /// 与字色反差的一色：亮字配黑、深字配白——描边与底板共用这一条判据（与序号牌同解）
        QColor contrastingColorFor(const QColor &color)
        {
            return color.lightness() > 128 ? QColor(0, 0, 0) : QColor(255, 255, 255);
        }
    } // namespace

    TextTool::TextTool() : AnnotationTool(ToolKind::Text)
    {
    }

    QString TextTool::displayName() const
    {
        return GyanisScribe::Core::uiText("文字");
    }

    void TextTool::setPendingText(const QString &text)
    {
        m_pending.text = text;
        // 文本落点后外接框由基类按字号量出来，这里不自己拼宽度，两处会算出两个数
        m_pending.rect = QRect();
    }


    bool TextTool::shapeIsUsable(const AnnotationShape &shape) const
    {
        return !shape.text.trimmed().isEmpty();
    }

    void TextTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        Q_UNUSED(context);

        if (shape.text.isEmpty())
        {
            return;
        }

        // 字体拼装与外接框量度共用 textFontFor：字体族、粗体、斜体一处改，画与量跟着一起动
        const QFont font = textFontFor(shape);

        // 整块占位与逐行落笔都按 TextLayout.h 的口径来：面板垫哪、描边围哪、选中框盖哪三处同解
        const QFontMetricsF metrics(font);
        const QRectF        textBox = textBoxOf(shape, metrics);

        const QStringList lines        = textLinesOf(shape.text);
        const qreal       lineStep     = textLineStepOf(metrics);
        const auto        lineOriginOf = [&shape, lineStep](const int index)
        {
            // position 是首行基线（drawText 的口径），往下每行加一个行距
            return QPointF{static_cast<qreal>(shape.position.x()), static_cast<qreal>(shape.position.y()) + static_cast<qreal>(index) * lineStep};
        };

        if (shape.textBackground)
        {
            const double padding = qMax(3.0, shape.fontSize / 4.0);
            auto         plate   = contrastingColorFor(shape.color);
            plate.setAlpha(kTextPlateAlpha);
            painter.setPen(Qt::NoPen);
            painter.setBrush(plate);
            painter.drawRoundedRect(textBox.adjusted(-padding, -padding, padding, padding), shape.fontSize / 5.0, shape.fontSize / 5.0);
        }

        if (shape.textOutline)
        {
            // 描边走字形路径的笔画：线宽给两倍，外半边就是露出来的反差轮廓，内半边随后被字色盖住
            QPainterPath glyphs;
            for (int index = 0; index < lines.size(); ++index)
            {
                glyphs.addText(lineOriginOf(index), font, lines[index]);
            }
            const double outlineWidth = qBound(1.0, shape.fontSize * kTextOutlineWidthPerFontSize, 6.0);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(contrastingColorFor(shape.color), outlineWidth * 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(glyphs);
            painter.setPen(Qt::NoPen);
            painter.setBrush(shape.color);
            painter.drawPath(glyphs);
            return;
        }

        painter.setFont(font);
        painter.setPen(shape.color);
        for (int index = 0; index < lines.size(); ++index)
        {
            // Qt 的 drawText 不收换行，逐行按各自的基线落笔；单行内容与老写法逐像素同解
            painter.drawText(lineOriginOf(index), lines[index]);
        }
    }

} // namespace GyanisScribe::Annotation
