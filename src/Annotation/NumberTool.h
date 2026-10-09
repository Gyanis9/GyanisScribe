/**
 * @file NumberTool.h
 * @brief 序号标注工具
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
     * @brief 带编号的圆形标记
     * @details 编号在本工具的图形条数上自然递增，而不是记一个会漏计的全局计数器：
     *          撤销一条序号后，下一条接着那个号走，界面上不会出现两个 3。
     */
    class NumberTool : public AnnotationTool
    {
    public:
        /**
         * @brief 构造工具，种类为 ToolKind::Number
         */
        NumberTool();

        /**
         * @brief 显示名，见 AnnotationTool::displayName()
         * @return 「序号」
         */
        [[nodiscard]] QString displayName() const override;

        /**
         * @brief 收笔并落定编号，见 AnnotationTool::end()
         * @return 带上编号的图形
         * @details 重写基类接口：编号在交回这一刻按当时的图形条数 +1 取——起笔之后又被撤销一条
         *          也不会串号；实际入库仍由引擎走撤销栈。
         */
        std::optional<AnnotationShape> end() override;

    protected:
        /**
         * @brief 落定判据，见 AnnotationTool::shapeIsUsable()
         * @param shape 在途图形
         * @return 恒为真
         * @details 重写基类接口：序号是点一下落的，没有尺寸可言，外接框判据在这里不适用。
         */
        bool shapeIsUsable(const AnnotationShape &shape) const override;

        /**
         * @brief 圆牌的外接框，见 AnnotationTool::boundingBoxOf()
         * @param shape 图形，取 position 与 fontSize
         * @return 以落点为中心的圆牌方框
         * @details 重写基类接口：基类把文本框的左上角摆在落点上，而圆牌是围着落点画的，
         *          不重写就会让选中框偏移半个牌面。
         */
        [[nodiscard]] QRect boundingBoxOf(const AnnotationShape &shape) const override;

        /**
         * @brief 画一个带数字的圆牌，见 AnnotationTool::paintShape()
         * @param painter 目标画家
         * @param shape 图形，取 number、position、color、fontSize
         * @param context 绘制现场，本工具不使用
         * @details 重写基类纯虚接口：圆牌直径随字号走，数字用反色保证在深浅底上都读得出。
         */
        void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const override;
    };

} // namespace GyanisScribe::Annotation
