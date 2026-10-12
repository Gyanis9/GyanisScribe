/**
 * @file StrokePen.h
 * @brief 描边类工具共用的那支笔：颜色、线宽、线型一处拼，六处画
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/AnnotationShape.h"

#include <QPen>

namespace GyanisScribe::Annotation
{

    /**
     * @brief 按图形自己的颜色、线宽与线型拼一支描边笔
     * @param shape 图形，取 color、lineWidth、lineStyle
     * @param capStyle 笔帽形状（默认圆头；直线类给平头更好看）
     * @return 可直接摆给画家的笔
     * @details 矩形、椭圆、直线、箭头、画笔、折线六件共用这一处：颜色、线宽、线型各只在这里拼一次，
     *          将来加一档笔的样子（比如点划线）六处跟着一起动，不会只改到一半。
     *          线型档越界时按最近的一档夹取——留一个界外的值只会画不出东西。
     */
    [[nodiscard]] inline QPen strokePenFor(const AnnotationShape &shape, const Qt::PenCapStyle capStyle = Qt::RoundCap)
    {
        const auto step = static_cast<std::size_t>(std::clamp(shape.lineStyle, 0, static_cast<int>(kAnnotationLineStyles.size()) - 1));

        QPen pen(shape.color);
        pen.setWidth(shape.lineWidth);
        pen.setStyle(kAnnotationLineStyles[step]);
        pen.setCapStyle(capStyle);
        return pen;
    }

} // namespace GyanisScribe::Annotation
