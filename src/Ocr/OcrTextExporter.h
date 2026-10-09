/**
 * @file OcrTextExporter.h
 * @brief 识别结果的导出：TXT 纯文本与 JSON 结构化
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"
#include "Ocr/OcrTypes.h"

#include <QString>

#include <filesystem>

namespace GyanisScribe::Ocr
{

    /**
     * @brief 导出成 TXT
     * @param result 识别结果
     * @return 与 result.text() 相同的整段文本，行尾用 LF
     */
    [[nodiscard]] QString toTxt(const OcrResult &result);

    /**
     * @brief 导出成 JSON
     * @param result 识别结果
     * @return 缩进 JSON 文本：language、averageConfidence、lines[words[text,boundingBox]]
     * @note 方框按 x/y/width/height 四个整数存，坐标是原图那一个坐标系：
     *       换一张图对不上号，故导出文本里另存图片尺寸供核对
     */
    [[nodiscard]] QString toJson(const OcrResult &result, const QSize &imageSize);

    /**
     * @brief 把文本写进文件
     * @param filePath 目标文件，已存在则覆盖
     * @param text 内容
     * @return 成功返回空载荷；失败返回带路径与原因的 Error
     */
    [[nodiscard]] Core::Expected<void> writeTextFile(const std::filesystem::path &filePath, const QString &text);

} // namespace GyanisScribe::Ocr
