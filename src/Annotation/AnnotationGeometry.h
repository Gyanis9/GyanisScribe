/**
 * @file AnnotationGeometry.h
 * @brief 标注命中测试用的几何量算
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QPoint>
#include <QPointF>
#include <QVector>

#include <cmath>
#include <limits>

namespace GyanisScribe::Annotation
{

    /**
     * @brief 点到线段的距离平方
     * @param point 待判点
     * @param segmentStart 线段起点
     * @param segmentEnd 线段终点
     * @return 距离的平方，逻辑像素²
     * @note 投影参数夹在 [0,1]：线段两端之外按端点算距离，不能顺着直线延伸出去，
     *       否则一条箭头的延长线上也会被点中
     */
    [[nodiscard]] inline double squaredDistanceToSegment(const QPoint &point, const QPoint &segmentStart, const QPoint &segmentEnd)
    {
        const QPointF vector        = QPointF(segmentEnd) - QPointF(segmentStart);
        const double  lengthSquared = vector.x() * vector.x() + vector.y() * vector.y();
        const QPointF offset        = QPointF(point) - QPointF(segmentStart);

        const double  projection = lengthSquared > 0.0 ? (offset.x() * vector.x() + offset.y() * vector.y()) / lengthSquared : 0.0;
        const double  clamped    = projection < 0.0 ? 0.0 : (projection > 1.0 ? 1.0 : projection);
        const QPointF closest    = QPointF(segmentStart) + vector * clamped;
        const QPointF gap        = QPointF(point) - closest;
        return gap.x() * gap.x() + gap.y() * gap.y();
    }

    /**
     * @brief 点到折线的距离平方
     * @param point 待判点
     * @param polyline 折线的采样点，少于两个时退化成到该点的距离
     * @return 最小的距离平方，逻辑像素²
     */
    [[nodiscard]] inline double squaredDistanceToPolyline(const QPoint &point, const QVector<QPoint> &polyline)
    {
        if (polyline.isEmpty())
        {
            return std::numeric_limits<double>::max();
        }
        if (polyline.size() == 1)
        {
            const QPointF gap = QPointF(point) - QPointF(polyline.front());
            return gap.x() * gap.x() + gap.y() * gap.y();
        }

        double best = std::numeric_limits<double>::max();
        for (int index = 0; index + 1 < polyline.size(); ++index)
        {
            const auto distance = squaredDistanceToSegment(point, polyline[index], polyline[index + 1]);
            if (distance < best)
            {
                best = distance;
            }
        }
        return best;
    }

} // namespace GyanisScribe::Annotation
