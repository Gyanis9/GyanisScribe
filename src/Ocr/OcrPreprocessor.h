/**
 * @file OcrPreprocessor.h
 * @brief 送识别之前的图像预处理
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QImage>

namespace GyanisScribe::Ocr
{

    /// 放大倍率的上限：再大只是把内存吃掉，识别率不会继续涨
    constexpr int kMaximumEnlargeFactor = 8;

    /// 中值滤波的窗口半径上限：超过 2 就已经会把小字抹掉
    constexpr int kMaximumDenoiseRadius = 2;

    /**
     * @brief 转灰度
     * @param image 输入图，任意格式
     * @return Format_Grayscale8 的图；输入为空时返回空图
     * @note 亮度按整数加权和算（0.299/0.587/0.114 的 256 倍取整），不用浮点：
     *       两端编译器对浮点乘加的收缩不同，灰度值差 1 就能让二值化把同一条边判到两侧
     */
    [[nodiscard]] QImage toGrayscale(const QImage &image);

    /**
     * @brief 二值化
     * @param gray 灰度图（Format_Grayscale8），其它格式先过 toGrayscale
     * @param threshold 判定阈值，0-255；小于阈值的判成黑（0），否则判成白（255）
     * @return 只有 0 与 255 两值的灰度图
     * @note 等于阈值算白：文字像素一般偏暗，把边界值算进黑侧会让细字变粗并粘连
     */
    [[nodiscard]] QImage binarize(const QImage &gray, quint8 threshold = 128);

    /**
     * @brief 整数倍放大
     * @param image 输入图
     * @param factor 放大倍率，会被夹进 [1, kMaximumEnlargeFactor]；1 表示原样返回
     * @return 硬边放大后的图（不做平滑：平滑会把锯齿糊成灰边，二值化时反而丢笔画）
     */
    [[nodiscard]] QImage enlarge(const QImage &image, int factor);

    /**
     * @brief 中值滤波去噪
     * @param gray 灰度图
     * @param radius 窗口半径，会被夹进 [0, kMaximumDenoiseRadius]；0 表示原样返回
     * @return 去噪后的灰度图
     * @note 判据是「孤立的噪点被抹掉、成片的笔画留着」：半径 1 时一个孤立黑点会翻成白，
     *       而 3×3 的黑块仍是黑
     */
    [[nodiscard]] QImage denoise(const QImage &gray, int radius = 1);

    /**
     * @brief 一步到位的预处理链：灰度 → 去噪 → 放大 → 二值化
     * @param image 原图
     * @param enlargeFactor 放大倍率
     * @param threshold 二值化阈值
     * @return 处理好的灰度图，可直接交给后端
     */
    [[nodiscard]] QImage prepareForRecognition(const QImage &image, int enlargeFactor = 2, quint8 threshold = 128);

} // namespace GyanisScribe::Ocr
