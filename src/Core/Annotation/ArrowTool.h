/**
 * @file ArrowTool.h
 * @brief 箭头标注工具
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/AnnotationTool.h"

namespace GyanisScribe::Core
{

    /**
     * @brief 从起点指向终点的箭头；shape.doubleHeaded 为真时两端各有一个箭头，shape.arrowReversed 为真时终点那头不再画头（箭头指回起点）
     */
    class ArrowTool : public AnnotationTool
    {
    public:
        /**
         * @brief 构造工具，种类为 ToolKind::Arrow
         */
        ArrowTool();

        /**
         * @brief 显示名，见 AnnotationTool::displayName()
         * @return 「箭头」
         */
        [[nodiscard]] QString displayName() const override;

    protected:
        /**
         * @brief 画一条带箭头的线，见 AnnotationTool::paintShape()
         * @param painter 目标画家
         * @param shape 图形，取 points 的前两点为首尾；doubleHeaded 为真时起点端也画一个箭头，arrowReversed 为真时把终点那头的头换到起点
         * @param context 绘制现场，本工具不使用
         * @details 重写基类纯虚接口：箭头三角随线宽变长，短线段上会让箭头盖过箭杆，
         *          这里把长度限制在箭杆长度的一半。
         */
        void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const override;

        /**
         * @brief 命中判据，见 AnnotationTool::hitTestShape()
         * @param shape 待判图形
         * @param point 画布逻辑坐标
         * @param tolerance 允许的偏差，逻辑像素
         * @return true 落在箭杆或箭头附近
         * @details 重写基类接口：外接框会把一条斜箭头的整片空白矩形都算成命中。
         */
        bool hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const override;

        /**
         * @brief 落定判据，见 AnnotationTool::shapeIsUsable()
         * @param shape 在途图形
         * @return true 首尾两点距离够长
         * @details 重写基类接口：基类按外接框尺寸判，一条 45° 的短线框出来的矩形也非零，
         *          但箭头本身没有方向，这里改用首尾距离。
         */
        bool shapeIsUsable(const AnnotationShape &shape) const override;
    };

} // namespace GyanisScribe::Core
