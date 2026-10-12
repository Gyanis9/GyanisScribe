/**
 * @file AnnotationShape.h
 * @brief 一条标注图形的数据记录，全部工具共用同一份形状
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QColor>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

#include <array>
#include <cstddef>

namespace GyanisScribe::Annotation
{

    /// 马赛克粒度阶梯：方块边长，逻辑像素；越大块越粗
    inline constexpr std::array<int, 3> kMosaicBlockSizes{8, 12, 20};
    /// 高亮透明度阶梯：0-255 的不透明度；越大越实
    inline constexpr std::array<int, 3> kHighlightAlphas{60, 110, 170};

    /// 马赛克粒度起点档在阶梯里的下标（「中」）
    inline constexpr std::size_t kDefaultMosaicBlockStep = 1;
    /// 高亮透明度起点档在阶梯里的下标（「中」）
    inline constexpr std::size_t kDefaultHighlightAlphaStep = 1;

    /**
     * @brief 一条图形
     * @details 各档工具共用这一个记录而不是各写一份结构：它们的差别在「怎么画」，不在「记什么」。
     *          各工具只取自己用得上的字段，用不到的字段留默认值——再加一层每工具一份的形状类型
     *          只会让命中测试、移动与序列化各写八遍。
     * @note 坐标一律是画布的逻辑坐标（与遮罩、选区同一坐标系），换算只在绘制与导出那两处发生。
     */
    struct AnnotationShape
    {
        QRect           rect;                                                           ///< 拖拽类图形的外接矩形（矩形、椭圆、马赛克）
        QVector<QPoint> points;                                                         ///< 轨迹类图形的采样点（画笔、高亮、箭头首尾两点）
        QPoint          position;                                                       ///< 锚点（文字、序号）
        QString         text;                                                           ///< 文字内容；序号工具用它存编号文本
        QColor          color           = QColor(255, 0, 0);                            ///< 画笔颜色，默认标注红
        int             lineWidth       = 3;                                            ///< 线宽，逻辑像素
        int             fontSize        = 16;                                           ///< 字号，仅文字与序号使用
        int             number          = 1;                                            ///< 序号值，仅序号工具使用
        int             fillStyle       = 0;                                            ///< 填充档：0 无、1 半透明、2 实心（仅矩形与椭圆用）
        bool            doubleHeaded    = false;                                        ///< 双向箭头：两端各画一个（仅箭头工具用）
        int             mosaicBlockSize = kMosaicBlockSizes[kDefaultMosaicBlockStep];   ///< 马赛克方块边长，逻辑像素（仅马赛克工具用），默认为阶梯里的「中」档
        int             highlightAlpha  = kHighlightAlphas[kDefaultHighlightAlphaStep]; ///< 高亮透明度，0-255（仅高亮工具用），默认为阶梯里的「中」档
    };

} // namespace GyanisScribe::Annotation
