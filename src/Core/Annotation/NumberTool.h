/**
 * @file NumberTool.h
 * @brief 序号标注工具
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/AnnotationTool.h"

#include <array>
#include <optional>

namespace GyanisScribe::Core
{

    /**
     * @brief 带编号的圆形标记
     * @details 编号在本工具「同一计数组」的图形条数上自然递增，而不是记一个会漏计的全局计数器：
     *          撤销一条序号后，下一条接着那个号走，界面上不会出现两个 3。计数组把条数分开数：
     *          第 2 组从自己的 1 开始连排，组与组之间互不影响。
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
         * @return 带上编号与计数组的图形
         * @details 重写基类接口：编号在交回这一刻按**当前组**当时的图形条数 +1 取——起笔之后
         *          又被撤销一条也不会串号；实际入库仍由引擎走撤销栈。
         */
        std::optional<AnnotationShape> end() override;

        /**
         * @brief 点名当前计数组的下一个号
         * @param number 下一个要落的号，小于 1 会被夹到 1
         * @details 点名之后该组编号不再跟着现有条数走，而是从这个号开始连排（5、6、7…）；
         *          要回到「按现有条数 +1」的自动口径，用 clearNextNumber()。各组点各自的名。
         */
        void setNextNumber(int number);

        /**
         * @brief 当前计数组回到自动编号
         */
        void clearNextNumber() noexcept;

        /**
         * @brief 下一个号会是多少（当前计数组）
         * @return 该组点名过的号，或该组现有条数 +1
         */
        [[nodiscard]] int nextNumber() const noexcept;

        /**
         * @brief 切换计数组
         * @param group 组号，夹在 1..kNumberGroupCount
         * @details 每一组各自连排自己的号：切组之后再落牌从那一组的下一号接着走，
         *          各组点过的名也各记各的。
         */
        void setNumberGroup(int group) noexcept;

        /**
         * @brief 当前计数组
         * @return 1..kNumberGroupCount
         */
        [[nodiscard]] int numberGroup() const noexcept;

    protected:
        /// 各组点名的下一个号；空表示按该组现有条数 +1 的自动口径
        std::array<std::optional<int>, kNumberGroupCount> m_nextNumbers{};

        /// 当前计数组，新落的牌记在它上面；下标 = 组号 - 1
        int m_activeGroup = 1;

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

} // namespace GyanisScribe::Core
