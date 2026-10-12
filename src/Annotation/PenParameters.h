/**
 * @file PenParameters.h
 * @brief 一整套画笔样式参数，供引擎下发默认值、撤销栈保存改样式前后两份
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/AnnotationShape.h"

#include <QColor>

namespace GyanisScribe::Annotation
{

    /**
     * @brief 一整套画笔样式参数
     * @details 每加一档样式旋钮（填充档、双向箭头、马赛克粒度……）如果都给 setPenParameters 与
     *          restyleShapeAt 追加一个位置参数，调用点很快会排到七八个同型 int，串位这类错误
     *          编译期看不出来。打包成一个结构体后，新增旋钮只动这里与消费它的那几个工具。
     * @note 与 AnnotationShape 里的样式字段同解：改这里的字段就是改「新笔画出去是什么样」，
     *       落到已画的某一条上要经 restyleShapeAt。两边的默认值都从 AnnotationShape.h 的
     *       阶梯表取值，不存在「开窗第一笔与按钮读数错开」的两份数字。
     */
    struct PenParameters
    {
        QColor  color           = QColor(255, 0, 0);                            ///< 画笔颜色
        int     lineWidth       = 3;                                            ///< 线宽，逻辑像素
        int     fontSize        = 16;                                           ///< 字号，磅
        int     fillStyle       = 0;                                            ///< 填充档：0 无、1 半透明、2 实心（仅矩形与椭圆用）
        bool    doubleHeaded    = false;                                        ///< 双向箭头：两端各画一个箭头（仅箭头工具用）
        int     mosaicBlockSize = kMosaicBlockSizes[kDefaultMosaicBlockStep];   ///< 马赛克方块边长，逻辑像素（仅马赛克工具用）
        int     highlightAlpha  = kHighlightAlphas[kDefaultHighlightAlphaStep]; ///< 高亮透明度，0-255（仅高亮工具用）
        QString fontFamily;                                                     ///< 字体族，空串=跟随应用默认字体（仅文字工具用）
        bool    textBold       = false;                                         ///< 粗体（仅文字工具用）
        bool    textItalic     = false;                                         ///< 斜体（仅文字工具用）
        bool    textOutline    = false;                                         ///< 反色描边（仅文字工具用）
        bool    textBackground = false;                                         ///< 反色底板（仅文字工具用）

        [[nodiscard]] bool operator==(const PenParameters &) const = default;
    };

    /**
     * @brief 取一条图形身上的样式参数
     * @param shape 图形
     * @return 该图形的样式字段，其余字段（位置、轨迹、文本）不取
     */
    [[nodiscard]] inline PenParameters penParametersOf(const AnnotationShape &shape)
    {
        return PenParameters{.color           = shape.color,
                             .lineWidth       = shape.lineWidth,
                             .fontSize        = shape.fontSize,
                             .fillStyle       = shape.fillStyle,
                             .doubleHeaded    = shape.doubleHeaded,
                             .mosaicBlockSize = shape.mosaicBlockSize,
                             .highlightAlpha  = shape.highlightAlpha,
                             .fontFamily      = shape.fontFamily,
                             .textBold        = shape.textBold,
                             .textItalic      = shape.textItalic,
                             .textOutline     = shape.textOutline,
                             .textBackground  = shape.textBackground};
    }

} // namespace GyanisScribe::Annotation
