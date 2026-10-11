/**
 * @file CursorPixels.h
 * @brief 把 GDI 交回来的鼠标指针位平面合成一张带 alpha 的图
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QImage>
#include <QRgb>

#include <vector>

namespace GyanisScribe::Capture
{

    /**
     * @brief 一bpp 位图一行的字节数
     * @param width 位图宽度，像素
     * @return 按 32 位对齐后的行字节数
     * @note GDI 的位图行一律四字节对齐，直接拿宽度除 8 会读错行——掩码面会整片错位。
     */
    [[nodiscard]] int cursorMonoRowBytes(int width);

    /**
     * @brief 把指针的位平面合成一张 Format_ARGB32 的图
     * @param colorPlane 颜色面，自上而下的 32 位 BGRA，长度 width*height；单色指针传空表
     * @param maskPlane AND 掩码，1bpp 行对齐位流；单色指针时 AND 面之后紧跟同样大小的 XOR 面
     * @param width 形状宽度，像素
     * @param height 形状高度，像素（单色指针传折半后的值，掩码里那两张平面各占这么高）
     * @param isMono 是否单色指针，即只有「AND + XOR」两张平面而没有颜色面
     * @return 合成好的图；尺寸非正时返回空图
     * @details 三种指针要分开解：带 alpha 的现代指针透明与否写在颜色面里，掩码那一格忽略即可
     *          （按掩码硬切会把抗锯齿边切成锯齿）；老式彩色指针没有 alpha，透明只能由 AND 面判；
     *          单色指针按 AND/XOR 两个位组合出「透明、黑、白」三态。
     */
    [[nodiscard]] QImage composeCursorImage(const std::vector<QRgb> &colorPlane, const std::vector<unsigned char> &maskPlane, int width, int height, bool isMono);

} // namespace GyanisScribe::Capture
