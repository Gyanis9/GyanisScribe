/**
 * @file MagnifierLayout.h
 * @brief 放大镜插图的纯几何：源框到插图框的位置算法
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QRect>
#include <QSize>

#include <cmath>

namespace GyanisScribe::Annotation
{

    /// 放大倍数：插图里的内容是源框的几倍大。固定 2 倍——放得再大只是把同一批像素摊得更糊
    inline constexpr qreal kMagnifierZoom = 2.0;

    /// 源框与插图之间的间隙，逻辑像素
    inline constexpr int kMagnifierGap = 12;

    /**
     * @brief 按源框算出插图框的位置
     * @param source 源框（画布逻辑坐标）
     * @param canvasLogicalSize 画布逻辑尺寸，用来把插图收在画布内
     * @return 插图框：默认摆在源框右侧、上边对齐；右边放不下就改摆左侧；上下越界时夹回画布内
     * @note 两侧都放不下时允许与源框重叠：宁可叠一点也不把插图推到画布外看不见。
     *       插图位置在绘制那一刻算，跟着源框走——移动与缩放源框，插图自然跟过去。
     */
    [[nodiscard]] inline QRect magnifierInsetRectFor(const QRect &source, const QSize &canvasLogicalSize)
    {
        const int insetWidth  = qMax(1, static_cast<int>(std::lround(source.width() * kMagnifierZoom)));
        const int insetHeight = qMax(1, static_cast<int>(std::lround(source.height() * kMagnifierZoom)));

        int insetX = source.right() + 1 + kMagnifierGap;
        if (insetX + insetWidth > canvasLogicalSize.width())
        {
            insetX = source.left() - kMagnifierGap - insetWidth;
        }
        insetX = qBound(0, insetX, qMax(0, canvasLogicalSize.width() - insetWidth));

        const int insetY = qBound(0, source.top(), qMax(0, canvasLogicalSize.height() - insetHeight));
        return QRect{insetX, insetY, insetWidth, insetHeight};
    }

} // namespace GyanisScribe::Annotation
