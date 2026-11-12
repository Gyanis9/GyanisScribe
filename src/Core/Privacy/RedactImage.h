/**
 * @file RedactImage.h
 * @brief 把一批物理打码框落到一张静态图上
 * @author Gyanis
 * @date 2026-11-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Annotation/AnnotationTool.h"

#include <QImage>
#include <QRect>
#include <QSize>

#include <span>

namespace GyanisScribe::Core::Privacy
{

    /**
     * @brief 一次静态图打码的产物
     */
    struct RedactedImage
    {
        QImage image;            ///< 打码后的图，与输入同尺寸
        int    appliedCount = 0; ///< 实际交给引擎的框数
    };

    /**
     * @brief 把物理框批量打码到一张静态图上
     * @param image 待打码的图，物理像素
     * @param physicalBoxes 打码框，与原图同坐标系
     * @param tool 只接受马赛克或高斯模糊
     * @param logicalSize 画布逻辑尺寸，物理框按它换算成逻辑框
     * @param mosaicBlockSize 马赛克方块边长，逻辑像素
     * @return 成功返回副本与有效框数；坏输入或全部框被夹掉时返回中文原因
     * @note 只操作副本：离屏引擎、离屏画布，不碰调用方的标注引擎与撤销栈。
     */
    [[nodiscard]] Base::Expected<RedactedImage> redactImage(const QImage &image, std::span<const QRect> physicalBoxes, GyanisScribe::Core::ToolKind tool,
                                                            QSize logicalSize, int mosaicBlockSize);

} // namespace GyanisScribe::Core::Privacy
