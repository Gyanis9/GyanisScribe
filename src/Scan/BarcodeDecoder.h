/**
 * @file BarcodeDecoder.h
 * @brief 二维码与条形码识别：后端是可选依赖，没编进来时交回明确的错误
 * @author Gyanis
 * @date 2026-10-12
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QImage>
#include <QString>

#include <optional>
#include <vector>

namespace GyanisScribe::Scan
{

    /**
     * @brief 认出来的一个码
     */
    struct BarcodeReading
    {
        QString text;           ///< 码里承载的文本（按 HRI 口径还原，中文等内容原样）
        QString format;         ///< 码制名，如「QR Code」「Code128」；认不出码制时为空
        bool    matrix = false; ///< 是不是二维码（二维码/Data Matrix 一类）；挑选时优先二维
    };

    /**
     * @brief 这一档有没有带上识别后端
     * @return 编进了 zxing-cpp 时为真
     * @details 界面用它决定按钮灰不灰、用例用它决定断言哪一档的行为——与 OCR 那枚宏同一条理由
     */
    [[nodiscard]] bool barcodeBackendAvailable() noexcept;

    /**
     * @brief 从一张图里找出所有二维码与条形码
     * @param image 待识别的图
     * @return 成功返回认出的码列表；本档没编进后端时返回 DependencyMissing
     * @details 「图里一个码都没有」是成功且列表为空，不是错误——调用方要能把这件事与
     *          「后端没编进来」分开说。转灰度后一遍扫全部码制，方向由后端自己掰正。
     */
    [[nodiscard]] Base::Expected<std::vector<BarcodeReading>> readBarcodes(const QImage &image);

    /**
     * @brief 从认出的列表里挑一条报给用户：优先二维
     * @param readings readBarcodes 的产物
     * @return 该用的那一条；列表为空时为空
     * @details 二维码的检测比一维稳得多；一维扫描在二维码的花纹里偶发假阳性
     *          （实测自造 QR 时旁边报出一条 ITF「85」），优先二维正好把这类噪声压到后面。
     *          没有二维时取第一条一维——那是一维场景的正常结果。
     */
    [[nodiscard]] std::optional<BarcodeReading> preferredReading(const std::vector<BarcodeReading> &readings);

} // namespace GyanisScribe::Scan
