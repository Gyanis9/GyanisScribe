/**
 * @file PpOcrInput.h
 * @brief PP-OCR 检测与识别模型的输入张量构造
 * @author Gyanis
 * @date 2026-11-14
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QImage>

#include <vector>

namespace GyanisScribe::Core
{

    /// 检测输入的宽高必须是这个数的整数倍：主干一路 32 倍下采样，补边对齐后特征图才不会错位
    constexpr int kPpOcrDetMultiple = 32;

    /// 检测输入的长边上限
    constexpr int kPpOcrDetMaxSide = 1280;

    /// 识别输入的固定高度
    constexpr int kPpOcrRecHeight = 48;

    /// 识别输入的宽度上限
    constexpr int kPpOcrRecMaxWidth = 2400;

    /**
     * @brief 一张可以直接喂给 ONNX 模型的输入张量
     * @note 数值按通道平面排开：先是 B 通道整幅，再 G，再 R，与 PaddleOCR 训练时的内存布局一致
     */
    struct PpOcrTensor
    {
        std::vector<float> data;         ///< 三通道平面的浮点值，长度等于 3 乘宽乘高
        int                width  = 0;   ///< 张量宽，检测输入是补边之后的宽度
        int                height = 0;   ///< 张量高，检测输入是补边之后的高度
        double             scaleX = 1.0; ///< 原图横向缩到张量时用的比例，补边不计入
        double             scaleY = 1.0; ///< 原图纵向缩到张量时用的比例，补边不计入
    };

    /**
     * @brief 构造检测网络的输入
     * @param image 原图，任意格式
     * @param maxSide 长边上限，超过即等比缩小
     * @return 缩放并补边到 32 的倍数后的张量；补边像素按黑色参与归一化；空图返回空张量
     */
    [[nodiscard]] PpOcrTensor buildDetInput(const QImage &image, int maxSide = kPpOcrDetMaxSide);

    /**
     * @brief 构造识别网络的输入
     * @param crop 一张行文本裁剪图
     * @param height 目标高度，按此等比换算宽度
     * @param maxWidth 宽度上限，超出时把宽夹到上限而不是继续拉伸
     * @return 像素映射到 [-1, 1] 的三通道张量；空图返回空张量
     */
    [[nodiscard]] PpOcrTensor buildRecInput(const QImage &crop, int height = kPpOcrRecHeight, int maxWidth = kPpOcrRecMaxWidth);

} // namespace GyanisScribe::Core
