/**
 * @file BlurTool.h
 * @brief 高斯模糊标注工具
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/AnnotationTool.h"

#include <QImage>

namespace GyanisScribe::Core
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

        /**
         * @brief 整张底图的模糊结果重建了几次
         * @return 重建次数，用于确认重绘命中缓存
         */
        [[nodiscard]] int cacheBuildCountForTesting() const noexcept;

    protected:
        /**
         * @brief 糊一块区域，见 AnnotationTool::paintShape()
         * @param painter 目标画家
         * @param shape 图形，取 rect
         * @param context 绘制现场，用 source 取整张底图、用 pixelScale 把逻辑框与逻辑半径换成物理量
         * @details 重写基类纯虚接口：对整张底图做三趟方框模糊（三趟叠加近似高斯），再把框住的那一块
         *          裁出来贴回去。模糊按整张底图采样，框沿一个半径以内的像素会取到框外的邻居，
         *          与「先裁出框内那一块再糊」的边界值不同。
         *          缩放因子非正或源图为空时什么都不画——宁可看不见效果，也不往用户脸上盖东西。
         */
        void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const override;

    private:
        mutable QImage m_blurredCanvas;        ///< 整张底图在当前半径下的模糊结果：重绘只从这里拷一块
        mutable qint64 m_blurredSourceKey = 0; ///< m_blurredCanvas 对应的源图身份
        mutable int    m_blurredRadius    = 0; ///< m_blurredCanvas 的模糊半径，物理像素
        mutable int    m_cacheBuilds      = 0; ///< 重建次数：源图或半径变化时才递增
    };

} // namespace GyanisScribe::Core
