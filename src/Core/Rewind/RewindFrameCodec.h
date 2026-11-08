/**
 * @file RewindFrameCodec.h
 * @brief 回溯帧的编码、解码与缩略图生成
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Rewind/RewindTypes.h"

#include <QByteArray>
#include <QImage>

namespace GyanisScribe::Core
{
    /**
     * @brief 编码一帧
     * @param image 整屏帧，不能为空
     * @return 成功返回编码字节与缩略图；空图或编码失败返回错误
     */
    [[nodiscard]] Base::Expected<RewindEncodedFrame> encodeRewindFrame(const QImage &image);

    /**
     * @brief 解码一帧
     * @param encodedPixels 编码字节
     * @return 成功返回整帧；空数据或损坏数据返回错误
     */
    [[nodiscard]] Base::Expected<QImage> decodeRewindFrame(const QByteArray &encodedPixels);

    /**
     * @brief 生成缩略图
     * @param image 原图
     * @param maximumEdge 缩略图最大边，必须为正
     * @return 原图不大于上限时原样返回，否则等比缩小
     */
    [[nodiscard]] QImage makeRewindThumbnail(const QImage &image, int maximumEdge = 160);
} // namespace GyanisScribe::Core
