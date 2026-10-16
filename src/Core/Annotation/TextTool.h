/**
 * @file TextTool.h
 * @brief 文字标注工具
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/AnnotationTool.h"

#include <QString>

namespace GyanisScribe::Core
{

    /**
     * @brief 在画布上放一段文字
     * @details 文本内容不在工具里编辑：工具只收「已定稿的一段文本 + 一个落点」。
     *          输入框属于界面层（AnnotationToolbar 那一批），它把 setPendingText() 填好再走 begin/end，
     *          这样容器里没有界面的情况下也能把文字标注跑进用例。
     */
    class TextTool : public AnnotationTool
    {
    public:
        /**
         * @brief 构造工具，种类为 ToolKind::Text
         */
        TextTool();

        /**
         * @brief 显示名，见 AnnotationTool::displayName()
         * @return 「文字」
         */
        [[nodiscard]] QString displayName() const override;

        /**
         * @brief 给在途的那条文字设定内容
         * @param text 文本，允许为空（空文本在收笔时会被判退化而丢弃）
         * @note 必须在 begin() 之后、end() 之前调用；end() 之后这条内容随在途图形一并清掉
         */
        void setPendingText(const QString &text);


    protected:
        /**
         * @brief 落定判据，见 AnnotationTool::shapeIsUsable()
         * @param shape 在途图形
         * @return true 文本非空
         * @details 重写基类接口：基类按外接框尺寸判，而文字是点一下落的，框宽为零也照样该成立。
         */
        [[nodiscard]] bool shapeIsUsable(const AnnotationShape &shape) const override;

        /**
         * @brief 画一段文字，见 AnnotationTool::paintShape()
         * @param painter 目标画家
         * @param shape 图形，取 text、position（基线起点）、color、fontSize、fontFamily、textBold、
         *              textItalic、textOutline、textBackground
         * @param context 绘制现场，本工具不使用
         * @details 重写基类纯虚接口：字体经 TextFont.h 的 textFontFor 拼装，与基类算外接框
         *          用的是同一处，选中框才不会和字错位。底板与描边都取与字色反差的一色：
         *          底板是垫在字形后面的半透明圆角板、描边是围在字形外一圈的轮廓，两者可同开。
         */
        void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const override;
    };

} // namespace GyanisScribe::Core
