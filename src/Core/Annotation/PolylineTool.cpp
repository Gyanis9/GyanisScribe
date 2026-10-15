#include "Core/Annotation/PolylineTool.h"
#include "Core/Annotation/StrokePen.h"

#include "Core/Annotation/AnnotationGeometry.h"
#include "Base/Text.h"

#include <QPainter>
#include <QPainterPath>

namespace GyanisScribe::Core
{
    PolylineTool::PolylineTool() : AnnotationTool(ToolKind::Polyline)
    {
    }

    QString PolylineTool::displayName() const
    {
        return GyanisScribe::Base::uiText("折线");
    }

    void PolylineTool::begin(const QPoint &point)
    {
        if (m_pending.points.isEmpty())
        {
            // 起头：借基类把样式默认值填上，再把折点表起成只有这一个
            AnnotationTool::begin(point);
            m_pending.points = {point};
        } else if (m_pending.points.back() != point)
        {
            // 之后的每一下按下都是「落一个折点」；与上一点重合的那一下不重复记
            m_pending.points.append(point);
            m_pending.rect = m_pending.rect.united(QRect(point, QSize(1, 1)));
        }
        m_previewPoint = point;
        m_previewValid = true;
    }

    void PolylineTool::update(const QPoint &point)
    {
        m_previewPoint = point;
        m_previewValid = true;
    }

    std::optional<AnnotationShape> PolylineTool::end()
    {
        if (!m_finishRequested)
        {
            // 松手不等于落定：还在等下一个折点（双击或切工具前收笔，见 requestFinish）
            return std::nullopt;
        }

        // 最后一个折点到预览点那一段，在收笔这一刻并进折点表（先并再清，清了就没了）
        if (m_previewValid && !m_pending.points.isEmpty() && m_previewPoint != m_pending.points.back())
        {
            m_pending.points.append(m_previewPoint);
        }
        m_finishRequested = false;
        m_previewValid    = false;
        return AnnotationTool::end();
    }

    void PolylineTool::requestFinish()
    {
        m_finishRequested = true;
    }

    void PolylineTool::paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const
    {
        Q_UNUSED(context);
        if (shape.points.isEmpty())
        {
            return;
        }

        auto pen = strokePenFor(shape, Qt::RoundCap);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        QPainterPath path;
        path.moveTo(shape.points.front());
        for (int index = 1; index < shape.points.size(); ++index)
        {
            path.lineTo(shape.points[index]);
        }
        // 在途时把到预览点的那一段也画出来：用户看得见下一段落在哪。
        // 判据用「预览点有效」而不是「这条是第几条」：落定之后预览点就失效了，已落定的折线不会被接上尾巴
        if (m_previewValid && m_previewPoint != shape.points.back())
        {
            path.lineTo(m_previewPoint);
        }
        painter.drawPath(path);
    }

    bool PolylineTool::hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const
    {
        return squaredDistanceToPolyline(point, shape.points) <= static_cast<double>(tolerance) * tolerance;
    }

    bool PolylineTool::shapeIsUsable(const AnnotationShape &shape) const
    {
        return shape.points.size() >= 2;
    }

} // namespace GyanisScribe::Core
