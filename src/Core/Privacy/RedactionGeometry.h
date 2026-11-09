/**
 * @file RedactionGeometry.h
 * @brief 打码框的纯几何：物理框换成画布逻辑框，一维码扫描线扩成实体框
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QRect>
#include <QSize>

#include <optional>

namespace GyanisScribe::Core::Privacy
{

    /// 每个打码框四周留的逻辑像素：OCR 词框常贴着笔画，留边才盖得住抗锯齿那一圈
    inline constexpr int kRedactionPaddingPixels = 2;

    /**
     * @brief 一维条形码纵向要盖住的额外逻辑像素（上下合计）
     * @details zxing 给一维码的定位框是一条接近零高度的扫描线，直接拿来打码只盖住一行像素；
     *          上下各扩一半，条码本体才整个落在遮挡里。二维码的定位框本来框住整个码，不走这一档。
     */
    inline constexpr int kBarcodeVerticalExpansionPixels = 8;

    /**
     * @brief 把一维条形码的扫描线定位框扩成能盖住条码本体的物理框
     * @param scanline zxing 返回的定位框，原图物理像素
     * @param imageSize 原图物理尺寸，扩完夹回这块矩形
     * @param logicalSize 画布逻辑尺寸；纵向的额外逻辑像素按两维比例折成物理高度
     * @return 扩好并夹回图像的框；尺寸退化时返回空矩形
     */
    [[nodiscard]] QRect expandBarcodeScanline(const QRect &scanline, QSize imageSize, QSize logicalSize);

    /**
     * @brief 把检测器给出的物理框换算成画布逻辑坐标的打码框
     * @param physicalBox 命中项的物理框，原图物理像素
     * @param imageSize 原图物理尺寸
     * @param logicalSize 画布逻辑尺寸
     * @return 四周留 kRedactionPaddingPixels 逻辑像素、取整后夹回画布的框；
     *         源框为空、尺寸退化或夹完为空时返回空
     * @details 两个方向各按自己的倍比算：不允许先按屏幕倍比再折一次，那会在非同倍比画布上错位。
     */
    [[nodiscard]] std::optional<QRect> logicalRedactionRect(const QRect &physicalBox, QSize imageSize, QSize logicalSize);

} // namespace GyanisScribe::Core::Privacy
