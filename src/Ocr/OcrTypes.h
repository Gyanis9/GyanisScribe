/**
 * @file OcrTypes.h
 * @brief 识别结果的值类型
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QRect>
#include <QString>
#include <QVector>

#include <optional>

namespace GyanisScribe::Ocr
{

    /**
     * @brief 一个被认出的词
     */
    struct OcrWord
    {
        QString text;        ///< 词文本
        QRect   boundingBox; ///< 在原图坐标系里的位置（与传入引擎的那张图同一坐标）
    };

    /**
     * @brief 一行识别结果
     */
    struct OcrLine
    {
        QVector<OcrWord> words;       ///< 行内词，按从左到右的顺序
        QRect            boundingBox; ///< 整行的外接框

        /**
         * @brief 把行内词拼成一行文本
         * @return 词之间以空格相连的文本
         * @note 中日韩文本原本没有空格，拼接时补空格是最不坏的选择：
         *       不补就会把两个词粘成一段谁也查不到的串
         */
        [[nodiscard]] QString text() const;
    };

    /**
     * @brief 一次识别的完整结果
     */
    struct OcrResult
    {
        QString               language;          ///< 实际用的语言标签，如 zh-Hans、en
        QVector<OcrLine>      lines;             ///< 按从上到下顺序的行
        std::optional<double> averageConfidence; ///< 平均置信度，0-1；后端给不出这一路读数时为空，不拿 0 冒充「认得很差」

        /**
         * @brief 把所有行拼成整段文本
         * @return 行间以换行相连的文本；没有行时返回空串
         */
        [[nodiscard]] QString text() const;

        /**
         * @brief 是否为空结果
         * @return true 表示一行都没认出来（不等于识别失败）
         */
        [[nodiscard]] bool isEmpty() const noexcept;
    };

} // namespace GyanisScribe::Ocr
