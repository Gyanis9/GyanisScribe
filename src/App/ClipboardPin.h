/**
 * @file ClipboardPin.h
 * @brief 剪贴板内容转成可贴的那张图：图像直接用，纯文本与颜色各自渲染成卡片
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QString>

class QMimeData;

namespace GyanisScribe::App
{

    /// 文本卡片：内边距（逻辑像素）与整卡的宽度上限；宽度封顶之后按词折行
    inline constexpr int       kTextCardPadding           = 12;
    inline constexpr int       kTextCardMaximumWidth      = 640;
    inline constexpr int       kTextCardMinimumWidth      = 120;
    inline constexpr int       kTextCardFontPointSize     = 14;
    inline constexpr qsizetype kMaximumTextCardCharacters = 2000; ///< 超过这个字数的文本不渲染：整卡会长到没法看

    /// 颜色卡片：色块加底部一条写法条
    inline constexpr int kColorCardWidth       = 160;
    inline constexpr int kColorCardHeight      = 100;
    inline constexpr int kColorCardStripHeight = 28;

    /// 识别颜色文本时的长度上限：一行、短、且必须以 # / rgb( / hsl( 开头才算，别把「red」这种词当成颜色
    inline constexpr int kMaximumColorTextLength = 32;

    /**
     * @brief 把剪贴板里的内容转成可贴的那张图
     * @param data 剪贴板的 mime 载荷，可为 nullptr（无人拷贝过时有的平台就返回空指针）
     * @param font 渲染文本卡片用的字体（生产传应用字体，用例传固定字体）
     * @return 成功返回图像：图像原样交回，纯文本与颜色各自渲染成卡片
     * @details 优先序：图像 → 颜色 → 纯文本。颜色只认 `#`、`rgb(`、`hsl(` 开头的短单行，
     *          免得把「red」这类普通单词渲染成色卡；HTML、链接与文件路径不在这条里冒充。
     */
    [[nodiscard]] Core::Expected<QImage> pinContentFrom(const QMimeData *data, const QFont &font);

    /**
     * @brief 把一段纯文本渲染成一张卡片
     * @param text 文本（调用方已去掉首尾空白）
     * @param font 基准字体，字号按卡片那一档统一
     * @return 白底深字、细边、带内边距的图；宽度封顶之后按词折行
     * @details 白底是刻意的：卡片是独立成品，不跟着主题走——深色主题下渲染成深底，
     *          贴到浅色桌面上就看不清了。
     */
    [[nodiscard]] QImage renderTextCard(const QString &text, const QFont &font);

    /**
     * @brief 把一种颜色渲染成一张卡片
     * @param color 颜色
     * @param label 底部条上写的文字（通常是用户复制的那个写法）
     * @return 上块下条的图：色块本体 + 底部写法条
     */
    [[nodiscard]] QImage renderColorCard(const QColor &color, const QString &label);

} // namespace GyanisScribe::App
