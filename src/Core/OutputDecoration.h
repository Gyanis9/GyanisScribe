/**
 * @file OutputDecoration.h
 * @brief 成品出程序之前的装饰：一圈边框与一圈轻投影（纯图像计算，不碰界面）
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QColor>
#include <QImage>
#include <QSize>

namespace GyanisScribe::Core
{

    /// 边框的宽度，物理像素：两圈足够把浅色截图从浅色底上分出来，再宽就抢画面
    inline constexpr int kOutputBorderPixels = 2;

    /// 投影向外延伸的宽度，物理像素
    inline constexpr int kOutputShadowPixels = 16;

    /// 投影紧贴成品那一圈的不透明度（0-255）：轻量档，只把成品从底上「抬」起来一点
    inline constexpr int kOutputShadowMaximumAlpha = 96;

    /// 边框颜色：中灰，深浅底上都看得出边界，又不像纯黑那样把成品框死
    inline const QColor kOutputBorderColor{0x80, 0x80, 0x80};

    /**
     * @brief 装饰之后的画布尺寸
     * @param imageSize 成品图尺寸，物理像素
     * @param border 加不加边框
     * @param shadow 加不加投影
     * @return 两档都关时与原尺寸相同；开着的那几档各自向四周撑开
     */
    [[nodiscard]] QSize decoratedOutputSize(const QSize &imageSize, bool border, bool shadow);

    /**
     * @brief 把边框与投影画到一张新图上
     * @param image 成品图，物理像素
     * @param border 加不加边框
     * @param shadow 加不加投影
     * @param fillBackground 目标格式带不了透明通道（JPG）时给真：投影底下先垫一层白，
     *        否则透明区在保存时会被压成不可预期的底色
     * @return 装好饰的新图；两档都关且不用垫底时原样交回入参（不复制、不改动）
     * @details 投影取「四周对称、无偏移」的柔和渐隐：截图贴出去时四边匀称，不会一边重一边轻。
     *          位置关系是「边框在外、投影最外」：投影紧贴边框外沿并按距离平方衰减。
     */
    [[nodiscard]] QImage decorateForOutput(const QImage &image, bool border, bool shadow, bool fillBackground);

} // namespace GyanisScribe::Core
