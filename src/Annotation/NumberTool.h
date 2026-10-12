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

        /**
         * @brief 点名下一个号
         * @param number 下一个要落的号，小于 1 会被夹到 1
         * @details 点名之后编号不再跟着现有条数走，而是从这个号开始连排（5、6、7…）；
         *          要回到「按现有条数 +1」的自动口径，用 clearNextNumber()。
         */
        void setNextNumber(int number);

        /**
         * @brief 回到自动编号
         */
        void clearNextNumber() noexcept;

        /**
         * @brief 下一个号会是多少
         * @return 点名过的号，或现有条数 +1
         */
        [[nodiscard]] int nextNumber() const noexcept;

    protected:
        /// 用户点名的下一个号；空表示按现有条数 +1 的自动口径
        std::optional<int> m_nextNumber;

        /**
         * @brief 落定判据，见 AnnotationTool::shapeIsUsable()
         * @param shape 在途图形
         * @return 恒为真
         * @details 重写基类接口：序号是点一下落的，没有尺寸可言，外接框判据在这里不适用。
         */
        bool shapeIsUsable(const AnnotationShape &shape) const override;

        /**
         * @brief 圆牌的局部外接框，见 AnnotationTool::localBoundingBoxOf()
         * @param shape 图形，取 position 与 fontSize
         * @return 以落点为中心的圆牌方框
         * @details 重写基类接口：基类把文本框的左上角摆在落点上，而圆牌是围着落点画的，
         *          不重写就会让选中框偏移半个牌面。
         */
        [[nodiscard]] QRect localBoundingBoxOf(const AnnotationShape &shape) const override;

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
