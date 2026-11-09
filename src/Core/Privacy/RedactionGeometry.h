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
     * @brief 一维条形码打码框的最小逻辑高度
     * @details zxing 给一维码的定位框往往只覆盖解码行附近，可能不带条码高度；
     *          只按它上下扩几个像素，小尺寸条码仍可能露出上下两截被扫出来。
     *          这里统一撑到 32 逻辑像素高，以定位框中心为轴；距离画布上下边太近时按夹取后的高度算。
     */
    inline constexpr int kBarcodeMinLogicalHeight = 32;

    /**
     * @brief 一维条形码在原框高度之外再补的逻辑像素（上下合计）
     * @details 定位框偶尔带一点高度；在原高度上再补 8，最终高度取它与最小高度中的较大者。
     *          二维码的定位框本来框住整个码，不走这一档。
     */
    inline constexpr int kBarcodeVerticalExpansionPixels = 8;

    /**
     * @brief 把一维条形码的扫描线定位框扩成能盖住条码本体的物理框
     * @param scanline zxing 返回的定位框，原图物理像素
     * @param imageSize 原图物理尺寸，扩完夹回这块矩形
     * @param logicalSize 画布逻辑尺寸；最小高度与额外高度按纵向倍比折成物理像素
     * @return 以扫描线为中心、至少 kBarcodeMinLogicalHeight 逻辑像素高、夹回图像的框；
     *         尺寸退化或定位框为空时返回空矩形
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
