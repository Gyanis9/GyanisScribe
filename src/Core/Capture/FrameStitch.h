/**
 * @file FrameStitch.h
 * @brief 滚动长截图的接缝：认出两张相邻帧重叠了多少行，并把新的一段接上去
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QImage>

namespace GyanisScribe::Core
{

    /// 允许回溯的最大重叠行数：一屏的高度量级，超过这个数就当没有重叠
    inline constexpr int kMaximumOverlapRows = 1200;

    /// 认定是「真接上了」所需的最小重叠行数：只对上两三行多半是两帧里的空白区恰好相同
    inline constexpr int kMinimumOverlapRows = 24;

    /**
     * @brief 认出两张帧重叠了多少行
     * @param previous 前一张帧，已接在成品下面的那一段
     * @param next 刚抓到的新一帧
     * @param maximumOverlapRows 最多往回看多少行
     * @return 重叠行数；对不上少于 kMinimumOverlapRows 行时返回 0
     * @details 从最长往最短找，取第一个整段相等的：滚动一页往往只露出新的一截，
     *          取「最短的相等段」会把两帧里恰好相同的空白当成接缝，接上去就是重复的一段。
     *          两张帧必须同宽；宽度不同说明中途换了屏或缩放，调用方得先收摊。
     */
    [[nodiscard]] int overlapRowCount(const QImage &previous, const QImage &next, int maximumOverlapRows = kMaximumOverlapRows);

    /**
     * @brief 把新一帧接到成品下面
     * @param stitched 已经接好的那一张
     * @param next 刚抓到的新一帧
     * @return 接好后的新图；两帧宽度不同时报错，内容全重合时接出来的就是原来那一段
     * @details 不新画一份而是造一张更高的图把两段抄进去：调用方要的就是这一张长图本身。
     */
    [[nodiscard]] Base::Expected<QImage> appendFrame(const QImage &stitched, const QImage &next);

} // namespace GyanisScribe::Core
