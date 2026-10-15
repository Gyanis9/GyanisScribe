/**
 * @file FillBrush.h
 * @brief 矩形与椭圆的填充档：无 / 半透明 / 实心
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/AnnotationShape.h"

#include <QBrush>

namespace GyanisScribe::Core
{

    /// 半透明档的透明度（0-255）：压得住底色又让底下的字透出来
    inline constexpr int kFillAlpha = 90;

    /**
     * @brief 按图形自己的填充档给它配一支笔刷
     * @param shape 图形，取 color 与 fillStyle
     * @return 0 无（Qt::NoBrush）、1 半透明、2 实心；越界值按无处理
     * @details 档记在每条图形自己身上：同一张图上「框一下」和「涂实」两种都能出现，
     *          档不能只挂在工具上。
     */
    [[nodiscard]] inline QBrush fillBrushFor(const AnnotationShape &shape)
    {
        if (shape.fillStyle == 1)
        {
            auto color = shape.color;
            color.setAlpha(kFillAlpha);
            return QBrush{color};
        }
        if (shape.fillStyle == 2)
        {
            return QBrush{shape.color};
        }
        return QBrush{Qt::NoBrush};
    }

} // namespace GyanisScribe::Core
