/**
 * @file PenTool.h
 * @brief 自由画笔工具
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
     * @brief 自由线条
     * @details 轨迹按采样点存成折线，不做平滑：平滑会引入一个没人能预测的曲线，
     *          而用户要的就是他手走过的路。要更细的笔迹就调高采样密度，不是换算法。
     */
    class PenTool : public AnnotationTool
    {
    public:
        /**
         * @brief 构造工具，种类为 ToolKind::Pen
         */
        PenTool();

        /**
         * @brief 显示名，见 AnnotationTool::displayName()
         * @return 「画笔」
         */
        [[nodiscard]] QString displayName() const override;

        /**
         * @brief 起笔，见 AnnotationTool::begin()
         * @param point 画布逻辑坐标
         * @details 重写基类接口：轨迹类要的是「一路经过哪些点」，所以这里把采样点起成一条只有一个点。
         */
        void begin(const QPoint &point) override;

        /**
         * @brief 行进，见 AnnotationTool::update()
         * @param point 当前逻辑坐标
         * @details 重写基类接口：基类只留首尾两点，这里把每个采样点追加进去；
         *          与上一点重合的采样不追加，否则静止不动会把同一点堆成千把个。
         */
        void update(const QPoint &point) override;

    protected:
        /**
         * @brief 轨迹类子种的构造入口
         * @param kind 工具种类，高亮工具用它把种类登记成 Highlight
         */
        explicit PenTool(ToolKind kind);

        /**
         * @brief 画一条折线，见 AnnotationTool::paintShape()
         * @param painter 目标画家
         * @param shape 图形，取 points
         * @param context 绘制现场，本工具不使用
         * @details 重写基类纯虚接口：圆头圆角，避免折角处出现毛刺。
         */
        void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const override;

        /**
         * @brief 命中判据，见 AnnotationTool::hitTestShape()
         * @param shape 待判图形
         * @param point 画布逻辑坐标
         * @param tolerance 允许的偏差，逻辑像素
         * @return true 落在笔迹附近
         * @details 重写基类接口：一条绕圈的画笔在其空心区域里也会被外接框判成命中。
         */
        [[nodiscard]] bool hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const override;

        /**
         * @brief 落定判据，见 AnnotationTool::shapeIsUsable()
         * @param shape 在途图形
         * @return true 至少两个采样点
         */
        [[nodiscard]] bool shapeIsUsable(const AnnotationShape &shape) const override;

        /**
         * @brief 轨迹类工具的共同绘制入口
         * @param painter 目标画家
         * @param shape 图形，取 points、color
         * @details 留成一个可重写的接缝：高亮工具复用同一条折线，只换画笔的宽度与透明度。
         */
        virtual void paintPolyline(QPainter &painter, const AnnotationShape &shape) const;
    };

} // namespace GyanisScribe::Core
