/**
 * @file EllipseTool.h
 * @brief 椭圆标注工具
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
     * @brief 椭圆（框住的区域是外接矩形）
     */
    class EllipseTool : public AnnotationTool
    {
    public:
        /**
         * @brief 构造工具，种类为 ToolKind::Ellipse
         */
        EllipseTool();

        /**
         * @brief 显示名，见 AnnotationTool::displayName()
         * @return 「椭圆」
         */
        [[nodiscard]] QString displayName() const override;

    protected:
        /**
         * @brief 画一个椭圆框，见 AnnotationTool::paintShape()
         * @param painter 目标画家
         * @param shape 图形，取 rect、color、lineWidth
         * @param context 绘制现场，本工具不使用
         * @details 重写基类纯虚接口：外接矩形按 shape.rect，只描边不填充。
         */
        void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const override;

        /**
         * @brief 命中判据，见 AnnotationTool::hitTestShape()
         * @param shape 待判图形
         * @param point 画布逻辑坐标
         * @param tolerance 允许的偏差，逻辑像素
         * @return true 命中椭圆轮廓那一圈
         * @details 重写基类接口：外接框判据会把椭圆的四个空心角也算成命中，
         *          这里按归一化半径判「在环上」，四角因此点不中。
         */
        [[nodiscard]] bool hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const override;
    };

} // namespace GyanisScribe::Core
