/**
 * @file PickGeometry.h
 * @brief 取色的那套像素几何：放大镜取样与摆放、屏幕坐标到抓帧像素的换算
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QPoint>
#include <QRect>

namespace GyanisScribe::Pick
{

    /// 放大镜默认取样的边长，逻辑像素：9x9 足够看清像素格，再多就把屏幕挡完了
    inline constexpr int kMagnifierSampleEdge = 9;

    /// 放大镜默认显示边长，逻辑像素
    inline constexpr int kMagnifierViewEdge = 180;

    /// 放大镜相对光标的默认偏移，逻辑像素：贴着光标会挡住正要看的像素
    inline constexpr int kMagnifierCursorOffset = 18;

    /**
     * @brief 放大镜该画在哪、取哪一块
     * @details 三个量互相牵制：取样范围决定每格多大，显示位置决定会不会挡住光标或跑出屏幕。
     */
    struct MagnifierLayout
    {
        QRect sourceRect;   ///< 取样范围，屏幕坐标，边长为给定取样边长
        QRect viewRect;     ///< 放大镜在屏幕上占的位置，屏幕坐标
        int   cellSize = 1; ///< 一个像素在放大镜里占多少逻辑像素
    };

    /**
     * @brief 算放大镜的取样范围与摆放
     * @param cursor 光标位置，屏幕坐标
     * @param screen 放大镜所在屏幕的几何，屏幕坐标
     * @param available 该屏可用区（去掉任务栏），用来判翻边
     * @param sampleEdge 取样边长，像素
     * @param viewEdge 显示边长，逻辑像素
     * @return 布局；sampleEdge 或 viewEdge 不成立时三个量都不可用
     * @details 取样范围夹在屏幕内而不是回绕：回绕会把屏幕另一头的像素摆到放大镜中间，
     *          用户照着抄的颜色是错的，而放大镜看起来完全正常。
     */
    [[nodiscard]] MagnifierLayout magnifierLayoutFor(const QPoint &cursor, const QRect &screen, const QRect &available, int sampleEdge, int viewEdge);

    /**
     * @brief 屏幕坐标换算到抓帧图像的像素坐标
     * @param globalPos 光标或矩形顶边的屏幕坐标
     * @param screenGeometry 这块屏幕的几何，屏幕坐标
     * @param devicePixelRatio 该屏的缩放比
     * @param imageSize 抓帧图像尺寸，像素
     * @return 图像内的像素坐标，越界夹到边缘
     * @details 高 DPI 下抓帧比逻辑坐标大：只减不乘会在 150% 屏上整体偏到左上角，
     *          取到的颜色是光标左上几像素的那块。
     */
    [[nodiscard]] QPoint framePixelFor(const QPoint &globalPos, const QRect &screenGeometry, qreal devicePixelRatio, const QSize &imageSize);

} // namespace GyanisScribe::Pick
