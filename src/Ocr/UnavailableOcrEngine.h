/**
 * @file UnavailableOcrEngine.h
 * @brief 没有可用识别后端时的桩实现
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Text.h"
#include "Ocr/IOcrEngine.h"

namespace GyanisScribe::Ocr
{

    /**
     * @brief 「本机认不出字」的那一份后端
     * @details 语言包没就位、或本平台没有编进任何后端时用它：识别一律返回 DependencyMissing 并写清怎么补，
     *          而不是回一个空结果——空结果与「一行都没认出来」长得一样，用户会以为是图不清楚。
     */
    class UnavailableOcrEngine : public IOcrEngine
    {
    public:
        /**
         * @brief 用给出的原因构造桩
         * @param reason 为什么没有后端，中文且要含替代做法
         */
        explicit UnavailableOcrEngine(
                QString reason = GyanisScribe::Core::uiText("本程序没有可用的文字识别后端。请在设置里指定 OCR 语言包目录（tessdata），或安装系统自带的识别组件"));

        /**
         * @brief 析构桩
         */
        ~UnavailableOcrEngine() override;

        /**
         * @brief 后端显示名，见 IOcrEngine::name()
         * @return 「无可用识别后端」
         */
        [[nodiscard]] QString name() const override;

        /**
         * @brief 可用性，见 IOcrEngine::isAvailable()
         * @return 恒为 false
         */
        [[nodiscard]] bool isAvailable() const override;

        /**
         * @brief 不可用原因，见 IOcrEngine::unavailableReason()
         * @return 构造时给的那句话
         */
        [[nodiscard]] QString unavailableReason() const override;

        /**
         * @brief 可选语言，见 IOcrEngine::supportedLanguages()
         * @return 空列表：没有后端就没有语言可言
         */
        [[nodiscard]] QStringList supportedLanguages() const override;

        /**
         * @brief 识别，见 IOcrEngine::recognize()
         * @param image 待识别图，本实现不使用
         * @param language 语言标签，本实现不使用
         * @return 恒为失败：DependencyMissing + 构造时给的原因
         */
        [[nodiscard]] Core::Expected<OcrResult> recognize(const QImage &image, const QString &language) override;

    private:
        QString m_reason; ///< 为什么没有后端
    };

} // namespace GyanisScribe::Ocr
