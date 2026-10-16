/**
 * @file PolylineTool.h
 * @brief 折线标注工具：点一下落一个折点，双击（或换工具前的收笔）落定
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
     * @brief 多段折线
     * @details 交互是「点击落点式」：按下落一个折点、移动出到光标的预览段，双击收笔（视图把双击
     *          接到引擎的 finishPendingStroke()）。所以松手不等于落定——`end()` 在没收笔时交回空，
     *          在途图形留在工具里继续等下一个折点。
     * @note 折点按点击顺序记在 points 里；预览点不进 points，落定后才把最后那段补上。
     */
    class PolylineTool : public AnnotationTool
    {
    public:
        /**
         * @brief 构造工具，种类为 ToolKind::Polyline
         */
        PolylineTool();

        /**
         * @brief 显示名，见 AnnotationTool::displayName()
         * @return 「折线」
         */
        [[nodiscard]] QString displayName() const override;

        /**
         * @brief 落一个折点，见 AnnotationTool::begin()
         * @param point 画布逻辑坐标
         * @details 重写基类接口：第一次按下起头（拿到样式默认值），之后每按一下追加一个折点；
         *          与上一点重合的按下不重复记。松手不落定，见 end()。
         */
        void begin(const QPoint &point) override;

        /**
         * @brief 移动预览点，见 AnnotationTool::update()
         * @param point 当前逻辑坐标
         * @details 重写基类接口：预览点只影响画出来的最后一段，不进折点表。
         */
        void update(const QPoint &point) override;

        /**
         * @brief 松手：没收笔就把在途图形留在手里，见 AnnotationTool::end()
         * @return 收到收笔请求时交回整条折线；否则交回空且**不丢弃**在途图形
         */
        std::optional<AnnotationShape> end() override;

        /**
         * @brief 收笔在即，见 AnnotationTool::requestFinish()
         */
        void requestFinish() override;

    protected:
        /**
         * @brief 画折线与最后那段预览，见 AnnotationTool::paintShape()
         * @param painter 目标画家
         * @param shape 图形，取 points、color、lineWidth
         * @param context 绘制现场，本工具不使用
         * @details 重写基类纯虚接口：圆头圆角连成一条；在途时把「最后一个折点到预览点」那一段也画上，
         *          用户才看得见下一段会落在哪。
         */
        void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const override;

        /**
         * @brief 命中判据，见 AnnotationTool::hitTestShape()
         * @param shape 待判图形
         * @param point 画布逻辑坐标
         * @param tolerance 允许的偏差，逻辑像素
         * @return true 落在折线附近
         */
        [[nodiscard]] bool hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const override;

        /**
         * @brief 落定判据，见 AnnotationTool::shapeIsUsable()
         * @param shape 在途图形
         * @return true 至少两个折点
         */
        [[nodiscard]] bool shapeIsUsable(const AnnotationShape &shape) const override;

    private:
        QPoint m_previewPoint;            ///< 预览段的终点：跟随光标
        bool   m_previewValid    = false; ///< 预览点有效没有：落定之后清掉，已落定的折线不再接尾巴
        bool   m_finishRequested = false; ///< 是否收到过收笔请求（双击或切换前的收笔）
    };

} // namespace GyanisScribe::Core
