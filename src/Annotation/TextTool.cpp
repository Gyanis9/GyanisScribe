#include "Annotation/TextTool.h"

#include "Annotation/TextFont.h"
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

        // 字形的真实占位：position 是基线（drawText 的口径），框顶从基线往上退一个 ascent，底板围着它摆
        const QFontMetricsF metrics(font);
        const QRectF        textBox(static_cast<qreal>(shape.position.x()), static_cast<qreal>(shape.position.y()) - metrics.ascent(), metrics.horizontalAdvance(shape.text),
                                    metrics.height());

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
            glyphs.addText(QPointF(shape.position), font, shape.text);
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
        // position 是基线起点，与 boundingBoxOf 里那个框同解
        painter.drawText(shape.position, shape.text);
    }

} // namespace GyanisScribe::Annotation
