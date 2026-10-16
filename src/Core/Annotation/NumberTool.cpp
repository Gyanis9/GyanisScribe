#include "Core/Annotation/NumberTool.h"

#include "Base/Text.h"

#include <QFont>
#include <algorithm>

#include <QPainter>

namespace GyanisScribe::Core
{
    namespace
    {
        // 圆牌直径相对字号的倍数：数字要居中且留白，太挤会看不出是个序号
        constexpr double kBadgeDiameterPerFontSize = 1.8;
    } // namespace

    NumberTool::NumberTool() : AnnotationTool(ToolKind::Number)
    {
    }

    QString NumberTool::displayName() const
    {
        return GyanisScribe::Base::uiText("序号");
    }

    std::optional<AnnotationShape> NumberTool::end()
    {
        // 编号取交回这一刻**当前组**的条数：起笔之后又被撤销一条的话，这里跟着退一号，不会跳出两个 3
        m_pending.number      = nextNumber();
        m_pending.numberGroup = m_activeGroup;
        m_pending.text        = QString::number(m_pending.number);
        if (auto &named = m_nextNumbers[static_cast<std::size_t>(m_activeGroup - 1)]; named)
        {
            *named = m_pending.number + 1;
        }
        return AnnotationTool::end();
    }

    void NumberTool::setNextNumber(const int number)
    {
        m_nextNumbers[static_cast<std::size_t>(m_activeGroup - 1)] = std::max(1, number);
    }

    void NumberTool::clearNextNumber() noexcept
    {
        m_nextNumbers[static_cast<std::size_t>(m_activeGroup - 1)].reset();
    }

    int NumberTool::nextNumber() const noexcept
    {
        if (const auto &named = m_nextNumbers[static_cast<std::size_t>(m_activeGroup - 1)]; named)
        {
            return *named;
        }

        int inGroup = 0;
        for (int index = 0; index < shapeCount(); ++index)
        {
            if (shapeAt(index).numberGroup == m_activeGroup)
            {
                ++inGroup;
            }
        }
        return inGroup + 1;
    }

    void NumberTool::setNumberGroup(const int group) noexcept
    {
        m_activeGroup = std::clamp(group, 1, kNumberGroupCount);
    }

    int NumberTool::numberGroup() const noexcept
    {
        return m_activeGroup;
    }

    bool NumberTool::shapeIsUsable(const AnnotationShape &shape) const
    {
        Q_UNUSED(shape);
        return true;
    }

    QRect NumberTool::localBoundingBoxOf(const AnnotationShape &shape) const
    {
        const int diameter = qMax(1, static_cast<int>(shape.fontSize * kBadgeDiameterPerFontSize));
        return {shape.position.x() - diameter / 2, shape.position.y() - diameter / 2, diameter, diameter};
    }

    void NumberTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        Q_UNUSED(context);

        const double diameter = qMax(1.0, shape.fontSize * kBadgeDiameterPerFontSize);
        const QRectF badge(shape.position.x() - diameter / 2.0, shape.position.y() - diameter / 2.0, diameter, diameter);

        painter.setPen(Qt::NoPen);
        painter.setBrush(shape.color);
        painter.drawEllipse(badge);

        QFont font = painter.font();
        font.setPointSize(shape.fontSize);
        font.setBold(true);
        painter.setFont(font);
        // 反色保证在红、蓝、黑这些牌面上都读得出来
        painter.setPen(shape.color.lightness() > 128 ? QColor(0, 0, 0) : QColor(255, 255, 255));
        painter.drawText(badge, Qt::AlignCenter, shape.text);
    }

} // namespace GyanisScribe::Core
