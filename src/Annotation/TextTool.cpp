#include "Annotation/TextTool.h"

#include <QFont>
#include <QPainter>

namespace GyanisScribe::Annotation
{

    TextTool::TextTool() : AnnotationTool(ToolKind::Text)
    {
    }

    QString TextTool::displayName() const
    {
        return QStringLiteral("文字");
    }

    void TextTool::setPendingText(const QString &text)
    {
        m_pending.text = text;
        // 文本落点后外接框由基类按字号量出来，这里不自己拼宽度，两处会算出两个数
        m_pending.rect = QRect();
    }

    QString TextTool::pendingText() const
    {
        return m_pending.text;
    }

    bool TextTool::shapeIsUsable(const AnnotationShape &shape) const
    {
        return !shape.text.trimmed().isEmpty();
    }

    void TextTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        Q_UNUSED(context);

        QFont font = painter.font();
        font.setPointSize(shape.fontSize);
        painter.setFont(font);
        painter.setPen(shape.color);
        // 基线锚点：position 就是文字左上角，与 boundingBoxOf 里那个矩形同一个起点
        painter.drawText(shape.position, shape.text);
    }

} // namespace GyanisScribe::Annotation
