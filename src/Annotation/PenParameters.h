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
     *       落到已画的某一条上要经 restyleShapeAt。
     */
    struct PenParameters
    {
        QColor color        = QColor(255, 0, 0); ///< 画笔颜色
        int    lineWidth    = 3;                 ///< 线宽，逻辑像素
        int    fontSize     = 16;                ///< 字号，磅
        int    fillStyle    = 0;                 ///< 填充档：0 无、1 半透明、2 实心（仅矩形与椭圆用）
        bool   doubleHeaded = false;             ///< 双向箭头：两端各画一个箭头（仅箭头工具用）

        [[nodiscard]] bool operator==(const PenParameters &) const = default;
    };

    /**
     * @brief 取一条图形身上的样式参数
     * @param shape 图形
     * @return 该图形的样式字段，其余字段（位置、轨迹、文本）不取
     */
    [[nodiscard]] inline PenParameters penParametersOf(const AnnotationShape &shape)
    {
        return PenParameters{.color = shape.color, .lineWidth = shape.lineWidth, .fontSize = shape.fontSize, .fillStyle = shape.fillStyle, .doubleHeaded = shape.doubleHeaded};
    }

} // namespace GyanisScribe::Annotation
