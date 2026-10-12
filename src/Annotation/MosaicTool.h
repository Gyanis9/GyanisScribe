/**
 * @file MosaicTool.h
 * @brief 马赛克标注工具
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
     * @brief 把框住的区域打成人眼读不出的方块
     * @details 取的是底图自己的像素，不是叠一层半透明色：遮挡要「真的看不清」，
     *          半透明色放大或换个显示器就可能又读得出来。
     */
    class MosaicTool : public AnnotationTool
    {
    public:
        /**
         * @brief 构造工具，种类为 ToolKind::Mosaic
         */
        MosaicTool();

        /**
         * @brief 显示名，见 AnnotationTool::displayName()
         * @return 「马赛克」
         */
        [[nodiscard]] QString displayName() const override;

    protected:
        /**
         * @brief 打一块马赛克，见 AnnotationTool::paintShape()
         * @param painter 目标画家
         * @param shape 图形，取 rect 与 mosaicBlockSize（方块边长，逻辑像素）
         * @param context 绘制现场，用 source 取被盖那块的原像素、用 pixelScale 把逻辑框与方块边长换成物理量
         * @details 重写基类纯虚接口：先按方块尺寸缩小再等比放大回去（两段插值都不做平滑），
         *          这是 Qt 原生能力，不必自建像素循环。缩放因子非正或源图为空时什么都不画——
         *          宁可看不见效果，也不把一片随机像素盖到用户脸上。
         */
        void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const override;
    };

} // namespace GyanisScribe::Annotation
