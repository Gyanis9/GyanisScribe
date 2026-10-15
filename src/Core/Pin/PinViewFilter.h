/**
 * @file PinViewFilter.h
 * @brief 贴图的视图滤镜：灰度与反色只改「看」，不改贴图手里的像素
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QImage>

namespace GyanisScribe::Core
{

    /**
     * @brief 按视图态算一张用于显示的像素
     * @param source 原图，可为空图（空图原样交回）
     * @param grayscale true 表示转灰度
     * @param inverted true 表示反色
     * @return 两档都关时原样交回（不拷贝）；都开着时先灰度、后反色
     * @details 视图滤镜不进成品：复制、保存、标注、识别拿的都是原始像素——与旋转、翻转同一条
     *          规矩（它们都只是「看」的变换）。灰度按 luma（r×11 + g×16 + b×5)/32 折成三通道
     *          同值，alpha 原样保留；反色走 QImage::InvertRgb，同样不碰 alpha——
     *          截图与文字卡片里的透明区不能因为看了一眼灰度或反色就变成实心。
     */
    [[nodiscard]] QImage pinViewFiltered(const QImage &source, bool grayscale, bool inverted);

} // namespace GyanisScribe::Core
