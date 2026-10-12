/**
 * @file TextFont.h
 * @brief 按图形自己的文字样式拼字体：画字与量字共用同一处
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/AnnotationShape.h"

#include <QFont>

namespace GyanisScribe::Annotation
{

    /**
     * @brief 按一条图形身上的文字样式拼出字体
     * @param shape 图形，取 fontFamily、fontSize、textBold、textItalic
     * @return 可直接摆给 painter 的字体
     * @details 画字（TextTool::paintShape）与量字（外接框、行内输入框）必须走同一个函数：
     *          各拼各的字体，换一种字体或按一下粗体，选中框就会比实际画的字宽一截或窄一截。
     * @note fontFamily 为空串时跟随应用默认字体（QFont 的默认构造就是它），
     *       这是「不挑字体」与「挑了某个家族」两种记录的区分处。
     */
    [[nodiscard]] inline QFont textFontFor(const AnnotationShape &shape)
    {
        QFont font;
        if (!shape.fontFamily.isEmpty())
        {
            font.setFamily(shape.fontFamily);
        }
        font.setPointSize(shape.fontSize);
        font.setBold(shape.textBold);
        font.setItalic(shape.textItalic);
        return font;
    }

} // namespace GyanisScribe::Annotation
