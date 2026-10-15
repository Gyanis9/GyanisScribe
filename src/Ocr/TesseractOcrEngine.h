/**
 * @file TesseractOcrEngine.h
 * @brief 基于 Tesseract 的识别后端
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Ocr/IOcrEngine.h"

#include <filesystem>
#include <memory>

namespace tesseract
{
    class TessBaseAPI;
} // namespace tesseract

namespace GyanisScribe::Ocr
{

    /**
     * @brief Tesseract 后端
     * @details 只走 Tesseract 的 C 接口（不用 C++ 头），因为该接口的符号与生命周期规则稳定，
     *          而且我们自己管句柄即可：句柄按「一次调用一个语言」复用，换语言时重新 Init，
     *          不做常驻多语言实例——多语言会一直占着几百 MB 的训练数据。
     */
    class TesseractOcrEngine final : public IOcrEngine
    {
    public:
        /**
         * @brief 用语言包目录构造后端
         * @param tessDataDirectory 存放 *.traineddata 的目录，须已存在
         */
        explicit TesseractOcrEngine(std::filesystem::path tessDataDirectory);

        /**
         * @brief 析构并释放 Tesseract 句柄
         */
        ~TesseractOcrEngine() override;

        /**
         * @brief 后端显示名，见 IOcrEngine::name()
         * @return 「Tesseract」
         */
        [[nodiscard]] QString name() const override;

        /**
         * @brief 可用性，见 IOcrEngine::isAvailable()
         * @return true 表示语言包目录里至少有一个受支持语言的 traineddata
         */
        [[nodiscard]] bool isAvailable() const override;

        /**
         * @brief 不可用原因，见 IOcrEngine::unavailableReason()
         * @return 目录里缺哪些语言包；可用时为空串
         */
        [[nodiscard]] QString unavailableReason() const override;

        /**
         * @brief 可选语言，见 IOcrEngine::supportedLanguages()
         * @return 语言包目录里实际存在的那些标签
         */
        [[nodiscard]] QStringList supportedLanguages() const override;

        /**
         * @brief 识别，见 IOcrEngine::recognize()
         * @param image 待识别图
         * @param language 语言标签，取 zh-Hans 或 en（本后端映射成 chi_sim/eng）
         * @return 成功返回结果；图太小、语言不支持、初始化失败各给对应原因
         */
        [[nodiscard]] Base::Expected<OcrResult> recognize(const QImage &image, const QString &language) override;

        /**
         * @brief 把对外语言标签换成 Tesseract 的模型名
         * @param languageTag 对外标签
         * @return 模型文件名里那一段，认不出时返回原标签
         * @note 这张映射表是本程序对外的唯一口径：界面上看到的标签与磁盘上的文件名不是一回事，
         *       各处自己映射就会各写一份，改一次要翻三个文件
         */
        [[nodiscard]] static QString modelFileNameFor(const QString &languageTag);

    private:
        /**
         * @brief 确保句柄已按目标语言初始化
         * @param languageTag 语言标签
         * @return 成功返回空载荷；失败返回原因
         */
        [[nodiscard]] Base::Expected<void> ensureLoaded(const QString &languageTag);

        /// 语言包目录（构造时给定，不随设置变化）
        std::filesystem::path m_dataDirectory;

        /// Tesseract 句柄，销毁由 TessBaseAPIDestroy 负责
        std::unique_ptr<tesseract::TessBaseAPI, void (*)(tesseract::TessBaseAPI *)> m_api;

        QString m_loadedLanguage; ///< 当前已初始化的语言标签，空表示还没初始化
    };

} // namespace GyanisScribe::Ocr
