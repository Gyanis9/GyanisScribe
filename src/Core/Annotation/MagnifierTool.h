/**
 * @file MagnifierTool.h
 * @brief 放大镜标注工具：把框住的一块区域放大成插图
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/AnnotationTool.h"

namespace GyanisScribe::Core
{

    /**
     * @brief 把框住的区域放大成一张插图
     * @details 拖出一个源框，框里的底图像素按固定倍数放大后画成旁边的插图，源框自己围一圈虚线
     *          标出「放大的是这里」。插图取的是底图自己的像素（与马赛克、模糊同一条规矩），
     *          别的标注不进插图——它是一面放大的镜子，不是二次拍照。
     * @note 命中与选中框只看源框：插图是展示件，拖源框就是拖整条，没有独立的插图语义。
     */
    class MagnifierTool : public AnnotationTool
    {
    public:
        /**
         * @brief 构造工具，种类为 ToolKind::Magnifier
         */
        MagnifierTool();

        /**
         * @brief 显示名，见 AnnotationTool::displayName()
         * @return 「放大镜」
         */
        [[nodiscard]] QString displayName() const override;

    protected:
        /**
         * @brief 画插图与源框虚线，见 AnnotationTool::paintShape()
         * @param painter 目标画家
         * @param shape 图形，取 rect（源框）
         * @param context 绘制现场，用 source 取被放大那块的原像素、用 pixelScale 把逻辑尺寸换成物理尺寸
         * @details 重写基类纯虚接口：源图那一块抠出来按 SmoothTransformation 放大贴进插图框，
         *          四周再围边框；缩放因子非正或源图为空时什么都不画。
         */
        void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const override;
    };

} // namespace GyanisScribe::Core
