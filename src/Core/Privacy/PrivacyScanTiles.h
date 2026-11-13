/**
 * @file PrivacyScanTiles.h
 * @brief 长图分带与跨带合并：把一张超高的图切成能进识别后端的带，再把各带结果并回全局坐标
 * @author Gyanis
 * @date 2026-11-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Ocr/OcrTypes.h"

#include <QPoint>
#include <QRect>
#include <QSize>

#include <vector>

namespace GyanisScribe::Core::Privacy
{

    /// 扫描带的物理像素高度：保守取在已知识别上限之下，长图才进得了后端
    inline constexpr int kPrivacyScanBandHeight = 1200;

    /// 相邻带的重叠行数：远大于单行高度，保证每一行至少在一个带里是完整的
    inline constexpr int kPrivacyScanOverlap = 120;

    /**
     * @brief 把一张图按带切开
     * @param imageSize 图尺寸，物理像素
     * @param bandHeight 带高，必须大于 0
     * @param overlap 相邻带重叠行数，必须 ≥ 0 且小于带高
     * @return 带的矩形表；参数不可能或尺寸为空时返回空表
     * @details 末带一律夹到图底，不足一个整带也不补齐：补齐就会越出图外。
     *          重叠必须小于带高，否则步长不为正，带表会原地打转。
     */
    [[nodiscard]] std::vector<QRect> scanTilesForImage(QSize imageSize, int bandHeight, int overlap);

    /**
     * @brief 把一个带的结果并进累计结果
     * @param merged 之前的累计结果
     * @param tileResult 这一个带的结果，坐标是带内的
     * @param tileOrigin 带左上角在图里的坐标
     * @param bandHeight 这一带的高度，传带矩形的实际高度；末带常不足一个整带
     * @param isFirstBand 是不是第一带
     * @param isLastBand 是不是最后一带
     * @return 并好的累计结果；被带边切过的行丢掉——非首带丢贴顶、非末带丢贴底
     * @details 别的字段（语言）只在累计结果为空时从这一带补上。
     *          完全落在重叠区里的行两带都看得完整，贴边规则管不到，靠「文本相同且全局框相交」只留先到的一份，
     *          否则同一个号码会被记两遍。
     */
    [[nodiscard]] OcrResult mergeTileResult(const OcrResult &merged, const OcrResult &tileResult, QPoint tileOrigin, int bandHeight, bool isFirstBand, bool isLastBand);

} // namespace GyanisScribe::Core::Privacy
