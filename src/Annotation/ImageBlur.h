/**
 * @file ImageBlur.h
 * @brief 图像模糊原语：三趟方框模糊近似高斯
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QImage>

namespace GyanisScribe::Annotation
{

    /// 方框模糊的趟数：一趟是方框，三趟的叠加已看不出方块感，再多的收益肉眼分不出
    inline constexpr int kBlurPasses = 3;

    /**
     * @brief 对整张图做三趟可分离方框模糊
     * @param source 源图
     * @param radius 半径，物理像素；非正数或源图为空时原样交回
     * @return 模糊后的图（ARGB32）
     * @details Qt 没有现成的高斯模糊，这里用三趟方框近似：沿行、沿列各滑一遍滑动窗口，
     *          复杂度与半径无关（每像素只做一次加法与一次减法）。边缘按夹取采样，
     *          不然四边会渗进黑色，看上去像多了一圈暗边。
     */
    [[nodiscard]] QImage boxBlurred(const QImage &source, int radius);

} // namespace GyanisScribe::Annotation
