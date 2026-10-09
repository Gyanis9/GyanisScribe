/**
 * @file TextTool.h
 * @brief 文字标注工具
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/AnnotationTool.h"

#include <QString>

namespace GyanisScribe::Annotation
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

        /**
         * @brief 在途文字的内容
         * @return 当前设进去的文本
         */
        [[nodiscard]] QString pendingText() const;

    protected:
        /**
         * @brief 落定判据，见 AnnotationTool::shapeIsUsable()
         * @param shape 在途图形
         * @return true 文本非空
         * @details 重写基类接口：基类按外接框尺寸判，而文字是点一下落的，框宽为零也照样该成立。
         */
        bool shapeIsUsable(const AnnotationShape &shape) const override;

        /**
         * @brief 画一段文字，见 AnnotationTool::paintShape()
         * @param painter 目标画家
         * @param shape 图形，取 text、position、color、fontSize
         * @param context 绘制现场，本工具不使用
         * @details 重写基类纯虚接口：字号用 pointSize，与基类算外接框时用的是同一个值，
         *          选中框才不会和字错位。
         */
        void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const override;
    };

} // namespace GyanisScribe::Annotation
