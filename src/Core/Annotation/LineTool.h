/**
 * @file LineTool.h
 * @brief 直线标注工具
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/AnnotationTool.h"

namespace GyanisScribe::Core
{

    /**
     * @brief 两点之间的一条直线段
     * @details 与箭头的分别是「有没有方向」：压住边缘、划一条分隔线这类标注不需要箭头，
     *          而箭头那侧的三角在短线上会盖过杆本身，两条不能合成一档。
     */
    class LineTool : public AnnotationTool
    {
    public:
        /**
         * @brief 构造工具，种类为 ToolKind::Line
         */
        LineTool();

        /**
         * @brief 显示名，见 AnnotationTool::displayName()
         * @return 「直线」
         */
        [[nodiscard]] QString displayName() const override;

    protected:
        /**
         * @brief 画一条两端圆角的直线段，见 AnnotationTool::paintShape()
         * @param painter 目标画家
         * @param shape 图形，取 points 的前两点为首尾
         * @param context 绘制现场，本工具不使用
         * @details 重写基类接口：端点用圆头，直角端点在斜线上会露出方角，与其余工具的观感不一致。
         */
        void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const override;

        /**
         * @brief 命中判据，见 AnnotationTool::hitTestShape()
         * @param shape 待判图形
         * @param point 画布逻辑坐标
         * @param tolerance 允许的偏差，逻辑像素
         * @return true 落在线段附近
         * @details 重写基类接口：外接框会把一条斜线的整片空白矩形都算成命中。
         */
        bool hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const override;

        /**
         * @brief 落定判据，见 AnnotationTool::shapeIsUsable()
         * @param shape 在途图形
         * @return true 首尾两点距离够长
         * @details 重写基类接口：基类按外接框判，一条 45° 的短线框出的矩形也非零，但线本身没有长度可言。
         */
        bool shapeIsUsable(const AnnotationShape &shape) const override;
    };

} // namespace GyanisScribe::Core
