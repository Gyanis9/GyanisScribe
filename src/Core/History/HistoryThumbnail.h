/**
 * @file HistoryThumbnail.h
 * @brief 历史列表用的方形缩略图：统一尺寸、统一圆角
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QImage>
#include <QSize>

namespace GyanisScribe::Core
{

    /// 缩略图边长，逻辑像素：需求要求整列表统一尺寸，混排会让列表像拼凑的
    inline constexpr int kThumbnailEdge = 96;

    /// 缩略图圆角半径，逻辑像素，与工具栏按钮的圆角同一档
    inline constexpr int kThumbnailCornerRadius = 6;

    /**
     * @brief 把一张图裁成统一尺寸的圆角缩略图
     * @param source 原图
     * @param target 目标尺寸，正方形
     * @param cornerRadius 圆角半径
     * @return 目标尺寸的缩略图；原图为空或目标尺寸不成立时返回空图
     * @details 按「填满再裁」而不是「装进去留白」：留白的缩略图在网格里大小不一，
     *          用户扫一眼看不出哪张是同一批截图。
     */
    [[nodiscard]] QImage makeThumbnail(const QImage &source, const QSize &target, int cornerRadius);

} // namespace GyanisScribe::Core
