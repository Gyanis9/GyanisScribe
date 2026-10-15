/**
 * @file HighlightTool.h
 * @brief 高亮标注工具
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/PenTool.h"

namespace GyanisScribe::Core
{

    /**
     * @brief 半透明宽笔迹
     * @details 走 PenTool 那条折线的路，只把画笔换成宽而半透明、方头的一支：交互与命中判据完全共用。
     */
    class HighlightTool : public PenTool
    {
    public:
        /**
         * @brief 构造工具，种类登记为 ToolKind::Highlight
         */
        HighlightTool();

        /**
         * @brief 显示名，见 AnnotationTool::displayName()
         * @return 「高亮」
         */
        [[nodiscard]] QString displayName() const override;

    protected:
        /**
         * @brief 画一条带颜色的粗笔迹，见 PenTool::paintPolyline()
         * @param painter 目标画家
         * @param shape 图形，取 points、color、lineWidth 与 highlightAlpha（透明度）
         * @details 重写父类接口：线宽放大到四倍、颜色按图形自己的透明度档、笔头方而平，
         *          这样压过去的字仍读得出来；此外与画笔工具行为一致。
         */
        void paintPolyline(QPainter &painter, const AnnotationShape &shape) const override;

        /**
         * @brief 命中判据，见 AnnotationTool::hitTestShape()
         * @param shape 待判图形
         * @param point 画布逻辑坐标
         * @param tolerance 允许的偏差，逻辑像素
         * @return true 落在笔迹覆盖范围内
         * @details 重写父类接口：高亮的笔迹比普通画笔宽四倍，容差按同一个倍数放宽，
         *          否则视觉上一条宽带子却只有中间一线点得中。
         */
        bool hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const override;
    };

} // namespace GyanisScribe::Core
