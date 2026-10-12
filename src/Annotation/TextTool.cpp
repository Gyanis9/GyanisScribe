#include "Annotation/TextTool.h"

#include "Annotation/TextFont.h"
#include "Core/Text.h"

#include <QPainter>

namespace GyanisScribe::Annotation
{

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

        // 字体拼装与外接框量度共用 textFontFor：字体族、粗体、斜体一处改，画与量跟着一起动
        painter.setFont(textFontFor(shape));
        painter.setPen(shape.color);
        // 基线锚点：position 就是文字左上角，与 boundingBoxOf 里那个矩形同一个起点
        painter.drawText(shape.position, shape.text);
    }

} // namespace GyanisScribe::Annotation
