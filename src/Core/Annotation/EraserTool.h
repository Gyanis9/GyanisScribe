/**
 * @file EraserTool.h
 * @brief 橡皮擦标注工具
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/PenTool.h"

namespace GyanisScribe::Core
{

    /**
     * @brief 沿笔迹擦回底图
     * @details 交互、采样与命中判据共用 PenTool 那条折线的路，画出来的却不是颜色，而是
     *          「这一段扫到的底图像素」——擦掉的是画上去的一切笔迹，露出原图那一块，
     *          与马赛克、模糊同族：它自己也占一层。
     * @note 本工具登记在工具表最后，也就是压在全部工具之上：层序按种类不按时间，
     *       所以擦过之后再用别的工具画，新笔迹在那个区域仍落在擦层下面。
     *       要收回一次擦除，按 Ctrl+Z（每一笔都是一条可撤销的图形）。
     */
    class EraserTool : public PenTool
    {
    public:
        /**
         * @brief 构造工具，种类登记为 ToolKind::Eraser
         */
        EraserTool();

        /**
         * @brief 显示名，见 AnnotationTool::displayName()
         * @return 「橡皮擦」
         */
        [[nodiscard]] QString displayName() const override;

    protected:
        /**
         * @brief 把笔迹扫过的那一块底图画回画布，见 AnnotationTool::paintShape()
         * @param painter 目标画家，已由调用方设好逻辑坐标
         * @param shape 图形，取 points、lineWidth
         * @param context 绘制现场，本工具取底图与物理像素倍比
         * @details 重写父类接口：父类走 paintPolyline 那条只认颜色与线宽的路，而这里要取源图像素，
         *          所以整段绘制自己来。掩膜抠图必须转到带 alpha 的格式再合成：截图多为 RGB32，
         *          没有 alpha 通道时 DestinationIn 会把掩膜外那一片算成乘零，画上去就是一块黑影。
         */
        void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const override;

        /**
         * @brief 命中判据，见 AnnotationTool::hitTestShape()
         * @param shape 待判图形
         * @param point 画布逻辑坐标
         * @param tolerance 允许的偏差，逻辑像素
         * @return true 落在擦除笔迹覆盖范围内
         * @details 重写父类接口：擦除笔迹比普通画笔宽好几倍，容差按同一个倍数放宽，
         *          否则视觉上一条宽笔迹却只有中间一线点得中。
         */
        bool hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const override;
    };

} // namespace GyanisScribe::Core
