/**
 * @file OcrPreprocessor.h
 * @brief 送识别之前的图像预处理
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QImage>

namespace GyanisScribe::Ocr
{

    /// 放大倍率的上限：两条通道都实测过——Tesseract 在 7pt 小字上 1× 平均置信度 0.446、2× 0.920，
    /// 3× 与 4× 又掉回 0.73 上下；系统识别那边同样到这一档就够，再往上只是把内存吃掉
    constexpr int kMaximumEnlargeFactor = 2;

    /**
     * @brief 转灰度
     * @param image 输入图，任意格式
     * @return Format_Grayscale8 的图；输入为空时返回空图
     * @note 亮度按整数加权和算（0.299/0.587/0.114 的 256 倍取整），不用浮点：
     *       两端编译器对浮点乘加的收缩不同，灰度值差 1 就能把同一条边判到两侧
     */
    [[nodiscard]] QImage toGrayscale(const QImage &image);

    /**
     * @brief 整数倍放大
     * @param image 输入图
     * @param factor 放大倍率，会被夹进 [1, kMaximumEnlargeFactor]；1 表示原样返回
     * @return 硬边放大后的图（不做平滑：平滑会把笔画边缘糊成灰边）
     */
    [[nodiscard]] QImage enlarge(const QImage &image, int factor);

    /**
     * @brief 送识别前的那一步：灰度 → 放大
     * @param image 原图
     * @param enlargeFactor 放大倍率，1 表示不放大
     * @return 处理好的灰度图，可直接交给后端
     * @warning 这里刻意没有二值化与去噪。实测（Windows 系统识别，小字灰底带噪点的一张）：
     *          原样 4 词、灰度+放大 4 词且把「741g」认回「7419」、再加二值化与中值滤波后 0 词。
     *          系统引擎自带阈值处理，喂进去一张只剩两色的图会把它的行分割一起打掉。
     */
    [[nodiscard]] QImage prepareForRecognition(const QImage &image, int enlargeFactor = kMaximumEnlargeFactor);

} // namespace GyanisScribe::Ocr
