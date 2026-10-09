/**
 * @file RectangleTool.h
 * @brief 矩形标注工具
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/AnnotationTool.h"

namespace GyanisScribe::Annotation
{

    /**
     * @brief 矩形框
     * @details 只描边不填充：框住的内容要能看清，填充会把它盖掉。
     */
    class RectangleTool : public AnnotationTool
    {
    public:
        /**
         * @brief 构造工具，种类为 ToolKind::Rectangle
         */
        RectangleTool();

        /**
         * @brief 显示名，见 AnnotationTool::displayName()
         * @return 「矩形」
         */
        [[nodiscard]] QString displayName() const override;

    protected:
        /**
         * @brief 画一个矩形框，见 AnnotationTool::paintShape()
         * @param painter 目标画家
         * @param shape 图形，取 rect、color、lineWidth
         * @param context 绘制现场，本工具不使用
         * @details 重写基类纯虚接口：按 shape.rect 描边，画笔宽度取 shape.lineWidth。
         */
        void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const override;
    };

} // namespace GyanisScribe::Annotation
