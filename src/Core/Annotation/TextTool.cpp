#include "Core/Annotation/TextTool.h"

#include "Core/Annotation/TextFont.h"
#include "Core/Annotation/TextLayout.h"
#include "Base/Text.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace GyanisScribe::Core
{
    namespace
    {
        /// 底板透明度（0-255）：近实体又透一点底，压得住杂背景
        constexpr int kTextPlateAlpha = 200;

        /// 与字色反差的一色：亮字配黑、深字配白——描边与底板共用这一条判据（与序号牌同解）
        QColor contrastingColorFor(const QColor &color)
        {
            return color.lightness() > 128 ? QColor(0, 0, 0) : QColor(255, 255, 255);
        }

        /// 字身那一色：底板用了形状自己的色时换成反差色，否则字压在同类色上等于不存在
        QColor textFillColorFor(const AnnotationShape &shape)
        {
            if (shape.textBackground && shape.textPlateColorStep == kTextPlateColorFromShape)
            {
                return contrastingColorFor(shape.color);
            }
            return shape.color;
        }

        /// 描边粗细按形状自己那一档取：下标夹进阶梯里，手工构造的形状不能把数组读越界
        double textOutlineWidthFor(const AnnotationShape &shape)
        {
            const auto step = std::clamp(shape.textOutlineWidthStep, 0, static_cast<int>(kTextOutlineWidths.size()) - 1);
            // 两端各留一个界：再细就看不见、再粗糊成一坨，这两个界值与这一格出现之前同解
            return qBound(1.0, static_cast<double>(shape.fontSize) * kTextOutlineWidths[static_cast<std::size_t>(step)], 6.0);
        }

        /// 底板那一块的颜色：自动档取与字色反差的一色，白 / 黑是指定色，最后一档用形状自己那支色
        QColor plateColorFor(const AnnotationShape &shape)
        {
            const auto step = std::clamp(shape.textPlateColorStep, 0, kTextPlateColorCount - 1);
            if (step == kTextPlateColorWhite)
            {
                return QColor(255, 255, 255);
            }
            if (step == kTextPlateColorBlack)
            {
                return QColor(0, 0, 0);
            }
            if (step == kTextPlateColorFromShape)
            {
                return shape.color;
            }
            return contrastingColorFor(textFillColorFor(shape));
        }

        /// 描边色按图形自己的档取：自动档走反差色，白 / 黑两档是指定色（只作用在描边上）
        QColor textStrokeColorOf(const AnnotationShape &shape)
        {
            if (shape.textStrokeColorStep == kTextStrokeColorWhite)
            {
                return QColor(255, 255, 255);
            }
            if (shape.textStrokeColorStep == kTextStrokeColorBlack)
            {
                return QColor(0, 0, 0);
            }
            return contrastingColorFor(textFillColorFor(shape));
        }
    } // namespace

    TextTool::TextTool() : AnnotationTool(ToolKind::Text)
    {
    }

    QString TextTool::displayName() const
    {
        return GyanisScribe::Base::uiText("文字");
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
            auto         plate   = plateColorFor(shape);
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
            const double outlineWidth = textOutlineWidthFor(shape);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(textStrokeColorOf(shape), outlineWidth * 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(glyphs);
            painter.setPen(Qt::NoPen);
            painter.setBrush(textFillColorFor(shape));
            painter.drawPath(glyphs);
            return;
        }

        painter.setFont(font);
        painter.setPen(textFillColorFor(shape));
        for (int index = 0; index < lines.size(); ++index)
        {
            // Qt 的 drawText 不收换行，逐行按各自的基线落笔；单行内容与老写法逐像素同解
            painter.drawText(lineOriginOf(index), lines[index]);
        }
    }

} // namespace GyanisScribe::Core
