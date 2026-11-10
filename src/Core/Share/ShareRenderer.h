/**
 * @file ShareRenderer.h
 * @brief 美化卡片渲染：背景、圆角、投影、编辑器外观、水印与预览缩放（纯图像，不碰界面与设置）
 * @author Gyanis
 * @date 2026-11-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Share/ShareStyle.h"

#include <QImage>
#include <QSize>

namespace GyanisScribe::Core
{

    /// 美化画布单边的像素上限
    inline constexpr int kMaximumShareCanvasEdge = 32768;

    /// 美化画布的总像素上限
    inline constexpr qint64 kMaximumShareCanvasPixels = 200LL * 1000 * 1000;

    /**
     * @brief 导出画布尺寸
     * @param imageSize 原图尺寸，像素
     * @param style 样式
     * @return 画布尺寸：图 + 标题栏 + 两侧（内边距 + 投影）
     */
    [[nodiscard]] QSize shareCanvasSize(const QSize &imageSize, const ShareStyle &style) noexcept;

    /**
     * @brief 渲染美化卡片
     * @param image 标注成品，像素
     * @param style 样式
     * @param maximumDimension 大于 0 时按画布宽等比缩小（预览用）；0 表示原尺寸
     * @return 渲染结果，ARGB32_Premultiplied、dpr=1、背景不透明；失败返回中文原因
     */
    [[nodiscard]] Base::Expected<QImage> renderShareImage(const QImage &image, const ShareStyle &style, int maximumDimension = 0);

} // namespace GyanisScribe::Core
