/**
 * @file ShapeTransform.h
 * @brief 图形几何的纯函数：外接框伸缩、图形整条映射与旋转
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/AnnotationShape.h"
#include "Annotation/IAnnotationEngine.h"

#include <QPointF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace GyanisScribe::Annotation
{

    /// 伸缩拖动夹住的最小边长，逻辑像素：再小就没有可抓的把手了
    inline constexpr qreal kMinimumResizedBoxSide = 2.0;

    /**
     * @brief 按手柄与拖动点算出新的外接框
     * @param box 起点外接框
     * @param handle 抓住的手柄（八个方向之一；None 与 Rotate 不动框）
     * @param point 拖动点在图形局部坐标下的位置
     * @param minimumSide 夹住的最小边长
     * @return 新框；拖过对面一条边时把边夹在最小边长上，不做翻转
     * @note 不做翻转是有意的：翻转会换来翻转手柄语义与取整方向两套特例，收益只是镜像一条图形
     */
    [[nodiscard]] inline QRectF resizedBox(const QRectF &box, const SelectionHandle handle, const QPointF &point, const qreal minimumSide = kMinimumResizedBoxSide)
    {
        qreal left   = box.left();
        qreal top    = box.top();
        qreal right  = box.right();
        qreal bottom = box.bottom();

        switch (handle)
        {
            case SelectionHandle::TopLeft:
                left = std::min(point.x(), right - minimumSide);
                top  = std::min(point.y(), bottom - minimumSide);
                break;
            case SelectionHandle::Top:
                top = std::min(point.y(), bottom - minimumSide);
                break;
            case SelectionHandle::TopRight:
                right = std::max(point.x(), left + minimumSide);
                top   = std::min(point.y(), bottom - minimumSide);
                break;
            case SelectionHandle::Right:
                right = std::max(point.x(), left + minimumSide);
                break;
            case SelectionHandle::BottomRight:
                right  = std::max(point.x(), left + minimumSide);
                bottom = std::max(point.y(), top + minimumSide);
                break;
            case SelectionHandle::Bottom:
                bottom = std::max(point.y(), top + minimumSide);
                break;
            case SelectionHandle::BottomLeft:
                left   = std::min(point.x(), right - minimumSide);
                bottom = std::max(point.y(), top + minimumSide);
                break;
            case SelectionHandle::Left:
                left = std::min(point.x(), right - minimumSide);
                break;
            default:
                break;
        }
        return QRectF{QPointF{left, top}, QPointF{right, bottom}};
    }

    /**
     * @brief 把一条图形按「旧框 → 新框」的仿射映射成新的一份
     * @param shape 源图形
     * @param from 旧的外接框
     * @param to 新的外接框
     * @return 映射后的图形
     * @details 矩形、采样点与锚点按同一套系数映射（以框中心为不动点，等价于按边映射）；
     *          笔宽、字号与马赛克粒度按两轴里变化大的那一边缩放——它们没法沿一个方向单独拉长，
     *          取大的那一边最贴近「拉多少就长多少」的直觉。
     *          某一轴的旧框长为 0（竖直线、横线）时该轴取纯平移：零长度没有倍数可算，
     *          按中心平移至少让线段跟着走，而不是留在原地跳。
     */
    [[nodiscard]] inline AnnotationShape shapeInBox(const AnnotationShape &shape, const QRectF &from, const QRectF &to)
    {
        const qreal factorX = from.width() > 0.0 ? to.width() / from.width() : 1.0;
        const qreal factorY = from.height() > 0.0 ? to.height() / from.height() : 1.0;
        const qreal scalar  = std::max(std::abs(factorX), std::abs(factorY));

        const auto mapX     = [&](const qreal x) { return to.center().x() + (x - from.center().x()) * factorX; };
        const auto mapY     = [&](const qreal y) { return to.center().y() + (y - from.center().y()) * factorY; };
        const auto mapPoint = [&](const QPoint &point) { return QPoint{static_cast<int>(std::lround(mapX(point.x()))), static_cast<int>(std::lround(mapY(point.y())))}; };

        AnnotationShape result = shape;
        if (!shape.rect.isNull())
        {
            result.rect =
                    QRectF{QPointF{mapX(shape.rect.left()), mapY(shape.rect.top())}, QPointF{mapX(shape.rect.right()), mapY(shape.rect.bottom())}}.normalized().toAlignedRect();
        }
        for (int index = 0; index < shape.points.size(); ++index)
        {
            result.points[index] = mapPoint(shape.points[index]);
        }
        result.position        = mapPoint(shape.position);
        result.lineWidth       = std::max(1, static_cast<int>(std::lround(shape.lineWidth * scalar)));
        result.fontSize        = std::max(4, static_cast<int>(std::lround(shape.fontSize * scalar)));
        result.mosaicBlockSize = std::clamp(static_cast<int>(std::lround(shape.mosaicBlockSize * scalar)), 1, 64);
        return result;
    }

    /**
     * @brief 绕中心正向旋转一个点
     * @param point 待转的点
     * @param center 旋转中心
     * @param degrees 角度，顺时针为正（与 QPainter::rotate 同向，y 轴向下）
     * @return 转后的点
     */
    [[nodiscard]] inline QPointF rotatedPoint(const QPointF &point, const QPointF &center, const qreal degrees)
    {
        const qreal   radians = degrees * std::numbers::pi / 180.0;
        const qreal   sine    = std::sin(radians);
        const qreal   cosine  = std::cos(radians);
        const QPointF offset  = point - center;
        return center + QPointF{offset.x() * cosine - offset.y() * sine, offset.x() * sine + offset.y() * cosine};
    }

    /**
     * @brief 反向旋转一个点：把画布上的点映回图形的局部坐标
     * @param point 画布上的点
     * @param center 旋转中心
     * @param degrees 图形的旋转角
     * @return 局部坐标下的点
     * @note 命中测试与手柄拖动都把事件点先映回来，再走与未旋转时完全相同的那条判据
     */
    [[nodiscard]] inline QPointF unrotatedPoint(const QPointF &point, const QPointF &center, const qreal degrees)
    {
        return rotatedPoint(point, center, -degrees);
    }

    /**
     * @brief 局部外接框绕中心旋转后的轴对齐外接框
     * @param box 局部外接框
     * @param degrees 旋转角
     * @return 包住四个转后角点的轴对齐框；角度为 0 时就是原框
     */
    [[nodiscard]] inline QRectF rotatedBoxBounds(const QRectF &box, const qreal degrees)
    {
        if (degrees == 0.0)
        {
            return box;
        }

        const QPointF center = box.center();
        const QPointF first  = rotatedPoint(box.topLeft(), center, degrees);
        qreal         left   = first.x();
        qreal         right  = first.x();
        qreal         top    = first.y();
        qreal         bottom = first.y();
        for (const auto &corner: {box.topRight(), box.bottomRight(), box.bottomLeft()})
        {
            const QPointF turned = rotatedPoint(corner, center, degrees);
            left                 = std::min(left, turned.x());
            right                = std::max(right, turned.x());
            top                  = std::min(top, turned.y());
            bottom               = std::max(bottom, turned.y());
        }
        return QRectF{QPointF{left, top}, QPointF{right, bottom}};
    }

    /**
     * @brief 点相对中心的方位角
     * @param point 画布上的点
     * @param center 中心
     * @return 角度，0 指向右侧、顺时针为正
     */
    [[nodiscard]] inline qreal angleDegreesOf(const QPointF &point, const QPointF &center)
    {
        return std::atan2(point.y() - center.y(), point.x() - center.x()) * 180.0 / std::numbers::pi;
    }

    /**
     * @brief 把角度差归一到 (-180, 180]
     * @param degrees 原始角度差
     * @return 归一后的角度差
     * @details 拖着旋转柄绕过对侧时角度差会一步跨过 ±180：不归一的话图形会跟着猛转一圈，
     *          归一之后那一跨只是同样的十几度。
     */
    [[nodiscard]] inline qreal normalizedAngleDelta(const qreal degrees)
    {
        qreal wrapped = std::fmod(degrees + 180.0, 360.0);
        if (wrapped <= 0.0)
        {
            wrapped += 360.0;
        }
        return wrapped - 180.0;
    }

} // namespace GyanisScribe::Annotation
