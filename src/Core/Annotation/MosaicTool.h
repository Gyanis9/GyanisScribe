/**
 * @file MosaicTool.h
 * @brief 马赛克标注工具
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/AnnotationTool.h"

#include <QImage>

#include <cstdint>

namespace GyanisScribe::Core
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

        /**
         * @brief 上一笔成品重算了几次
         * @return 重算次数，用于确认静止重绘命中缓存
         */
        [[nodiscard]] int cacheBuildCountForTesting() const noexcept;

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

    private:
        mutable QImage        m_lastImage;       ///< 上一笔的成品：命中缓存时直接贴它
        mutable QRect         m_lastTarget;      ///< 上一笔成品的逻辑目标矩形
        mutable std::uint64_t m_lastKey     = 0; ///< 上一笔的缓存键：源图、倍比与几何一起混出来
        mutable int           m_cacheBuilds = 0; ///< 重算次数，缓存键不匹配时才递增
    };

} // namespace GyanisScribe::Core
