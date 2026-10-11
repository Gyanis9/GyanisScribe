/**
 * @file BlurTool.h
 * @brief 高斯模糊标注工具
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/AnnotationTool.h"

namespace GyanisScribe::Annotation
{

    /// 模糊半径，逻辑像素：够盖住小字，又不至于把整块糊成一片
    inline constexpr double kBlurRadiusLogical = 6.0;

    /**
     * @brief 把框住的区域糊掉
     * @details 与马赛克同一族但用途不同：马赛克是「读不出」，模糊是「还看得见轮廓、认不出字」，
     *          对着截图做说明时后者更常用（保留一点版面感）。取的是底图自己的像素，
     *          不是叠一层色——叠色换个显示器或放大就可能又读得出来。
     */
    class BlurTool : public AnnotationTool
    {
    public:
        /**
         * @brief 构造工具，种类为 ToolKind::Blur
         */
        BlurTool();

        /**
         * @brief 显示名，见 AnnotationTool::displayName()
         * @return 「高斯模糊」
         */
        [[nodiscard]] QString displayName() const override;

    protected:
        /**
         * @brief 糊一块区域，见 AnnotationTool::paintShape()
         * @param painter 目标画家
         * @param shape 图形，取 rect
         * @param context 绘制现场，用 source 取被盖那块的原像素、用 pixelScale 把逻辑半径换成物理半径
         * @details 重写基类纯虚接口：把源图那一块抠出来做三趟方框模糊（三趟叠加近似高斯），再画回去。
         *          缩放因子非正或源图为空时什么都不画——宁可看不见效果，也不往用户脸上盖东西。
         */
        void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const override;
    };

} // namespace GyanisScribe::Annotation
