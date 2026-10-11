/**
 * @file BgraFrame.h
 * @brief 从一行行 BGRA 缓冲拷出 QImage（纯函数，DXGI 抓帧与用例共用）
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QImage>
#include <QSize>

#include <QtGlobal>

namespace GyanisScribe::Capture
{

    /**
     * @brief 把一行行 BGRA 像素拷进 QImage
     * @param data 第一行首字节，不得为空
     * @param pitch 行距（字节）；可能大于 size.width() * 4
     * @param size 像素尺寸
     * @return 拷好的 QImage（Format_RGB32）；入参退化时返回空图
     * @details DXGI 桌面重复交回的是 B8G8R8A8、行距按 256 字节对齐的缓冲：行距一旦按
     *          width*4 算，画面从第二行起整体斜切。这一步单独成纯函数，就是为了让它有判据——
     *          那一段在真机上错了的时候，人眼只会看到「录出来的画面是歪的」。
     */
    [[nodiscard]] QImage imageFromBgraRows(const uchar *data, int pitch, const QSize &size);

} // namespace GyanisScribe::Capture
