/**
 * @file PiiDetector.h
 * @brief 从文字识别结果与二维码框里检测敏感信息
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Ocr/OcrTypes.h"
#include "Core/Privacy/PiiTypes.h"

#include <QSize>

#include <vector>

namespace GyanisScribe::Core::Privacy
{

    /**
     * @brief 在本地内存里找出可打码的敏感信息
     * @param ocr 文字识别结果，词框与行框都是原图物理像素坐标
     * @param barcodeBoxes 二维码/条形码的外接框，与原图同坐标系
     * @param imageSize 原图尺寸，所有输出框都会夹回这块矩形
     * @return 按阅读顺序排列、互不包含的命中项；空结果只表示没检出，不代表内容安全
     * @note 纯逻辑：不写盘、不访问网络、不记录识别原文，也不依赖 Widgets
     */
    [[nodiscard]] std::vector<PiiItem> detectPii(const Core::OcrResult &ocr, const std::vector<QRect> &barcodeBoxes, QSize imageSize);

} // namespace GyanisScribe::Core::Privacy
