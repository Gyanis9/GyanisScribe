/**
 * @file TextLayout.h
 * @brief 多行文字的排版口径：行怎么切、基线怎么步进、整块占多大——画字与量字共用这一处
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/AnnotationShape.h"

#include <QFontMetricsF>
#include <QRectF>
#include <QStringList>

namespace GyanisScribe::Annotation
{

    /**
     * @brief 文字内容按行切分
     * @param text 内容，以 \n 分行
     * @return 逐行内容；行尾的空行照留（回车敲出来的那一行在版面上占着位）
     * @note 只认 \n：内容从 QTextEdit::toPlainText 来，行末不会再夹一个 \r
     */
    [[nodiscard]] inline QStringList textLinesOf(const QString &text)
    {
        return text.split(QLatin1Char('\n'));
    }

    /**
     * @brief 相邻两行基线之间的距离
     * @param metrics 该图形的字体的量度
     * @return lineSpacing（含行间距），与 QTextLayout 逐行铺开的步进同解
     */
    [[nodiscard]] inline qreal textLineStepOf(const QFontMetricsF &metrics)
    {
        return metrics.lineSpacing();
    }

    /**
     * @brief 整块文字的占位
     * @param shape 图形，取 text 与 position（首行基线起点，drawText 的口径）
     * @param metrics 该图形的字体的量度
     * @return 框：左沿与首行基线起点同 x，框顶从基线往上退一个 ascent；宽取最宽的一行，
     *         高按行数铺开（首行占满 height，此后每行再加一个行距）
     * @details 画字（TextTool::paintShape）与量字（外接框、行内输入框）必须走同一个函数：
     *          各算各的框，多敲一行回车选中框就比字矮一截。单行的取值与多行这一格出现之前
     *          完全一致（宽 = 整串的 advance、高 = metrics.height()），老图样的框不会挪位。
     */
    [[nodiscard]] inline QRectF textBoxOf(const AnnotationShape &shape, const QFontMetricsF &metrics)
    {
        const QStringList lines = textLinesOf(shape.text);
        qreal             width = 0.0;
        for (const auto &line: lines)
        {
            width = qMax(width, metrics.horizontalAdvance(line));
        }

        return {static_cast<qreal>(shape.position.x()), static_cast<qreal>(shape.position.y()) - metrics.ascent(), width,
                metrics.height() + static_cast<qreal>(lines.size() - 1) * textLineStepOf(metrics)};
    }

} // namespace GyanisScribe::Annotation
