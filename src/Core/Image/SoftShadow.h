/**
 * @file SoftShadow.h
 * @brief 柔和投影：一圈按距离平方衰减的阴影，成品装饰与美化卡片共用
 * @author Gyanis
 * @date 2026-11-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QImage>
#include <QRect>
#include <QSize>

namespace GyanisScribe::Core
{

    /// 投影紧贴卡片那一圈的不透明度（0-255）：轻量档，只把卡片从底上抬起来一点
    inline constexpr int kSoftShadowMaximumAlpha = 96;

    /**
     * @brief 画一层柔和投影
     * @param canvasSize 画布尺寸，像素
     * @param cardRect 卡片矩形，像素；阴影画在它外面
     * @param cornerRadius 卡片圆角半径，像素；0 表示直角
     * @param shadowPixels 投影向外延伸的宽度，像素；非正时返回空图
     * @param maximumAlpha 紧贴卡片那一圈的 alpha，越界时夹到 0-255
     * @return ARGB32_Premultiplied 的透明层；画布或卡片为空时返回空图
     * @details 距离取像素索引到圆角矩形边界的有符号距离：半径 0 时与旧的
     *          「横竖各自超出几格再取斜边」逐像素同解。只遍历卡片四周的投影带，
     *          卡片内部与投影半径之外的像素都不写。
     */
    [[nodiscard]] QImage makeSoftShadowLayer(const QSize &canvasSize, const QRect &cardRect, int cornerRadius, int shadowPixels, int maximumAlpha = kSoftShadowMaximumAlpha);

} // namespace GyanisScribe::Core
