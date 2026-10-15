/**
 * @file CursorPixels.h
 * @brief 鼠标指针的形状类型，以及把 GDI 位平面合成成一张带 alpha 的图
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QImage>
#include <QPoint>
#include <QRgb>
#include <QtGlobal>

#include <optional>
#include <vector>

namespace GyanisScribe::Core
{

    /**
     * @brief 鼠标指针的形状与落点：一张与抓帧同尺度的小图，加它的热点和此刻在屏内的位置
     * @details 像素按物理算，与 CapturedImage 的 image 同一口径；形状本身受系统 DPI 支配，
     *          我们只搬位置，不再乘一次缩放因子。
     * @note position 是「最近一次抓的那块屏」内的逻辑坐标，与 grabCursor() 传的屏名同源，
     *       换算是屏内位置乘缩放因子——两处口径不一致就会画歪。
     */
    struct CursorSprite
    {
        QImage image;    ///< 指针形状，Format_ARGB32_Premultiplied，物理像素
        QPoint hotspot;  ///< 尖端在 image 内的物理像素坐标，落点要按它回退
        QPoint position; ///< 尖端在该屏内的逻辑坐标，与抓帧用的那块屏同一个坐标系
    };

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

    /**
     * @brief 从一枚系统指针句柄解出形状与热点
     * @param cursorHandle Win32 的 HCURSOR 值，其它平台传什么都一样返回空
     * @return 解得出时返回形状（position 留空，由抓帧来源补上）；句柄为空、本平台不支持或解不出时返回空
     * @details 收在这一层而不是写在抓帧来源里，是为了让 GDI 那一段（两张平面的行距与上下序、
     *          热点的读法）能被用例喂进一枚自造的指针来验，而不是只能验这台机器此刻正在用的那一枚。
     */
    [[nodiscard]] std::optional<CursorSprite> cursorSpriteFromHandle(quintptr cursorHandle);

} // namespace GyanisScribe::Core
