/**
 * @file PpOcrOnnxEngine.h
 * @brief 基于 ONNX Runtime 与 PP-OCRv6 模型的识别后端
 * @author Gyanis
 * @date 2026-11-14
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
#include "Core/Ocr/IOcrEngine.h"

#include <QString>
#include <QStringList>

#include <filesystem>
#include <memory>

namespace GyanisScribe::Plugins
{

    /// 检测模型的固定文件名
    inline constexpr const char *kPpOcrDetModelFileName = "PP-OCRv6_det_small.onnx";

    /// 识别模型的固定文件名
    inline constexpr const char *kPpOcrRecModelFileName = "PP-OCRv6_rec_small.onnx";

    /// 识别字表的固定文件名
    inline constexpr const char *kPpOcrDictFileName = "ppocrv6_dict.txt";

    /**
     * @brief PaddleOCR 的 PP-OCRv6 后端
     * @details 模型目录里放检测模型、识别模型与字表三件；onnxruntime.dll 按「环境变量
     *          GYANISSCRIBE_ONNXRUNTIME_DLL、程序目录、系统搜索路径」的顺序动态加载，
     *          接口表从 OrtGetApiBase 现取，构建时不需要导入库。
     *          一个实例一个 ONNX Runtime 环境，两个会话首次识别时创建并一直复用；
     *          调用方（识别线程）串行调用，本类不做并发保护。
     * @note 模型文件损坏或运行库版本不符不算「没认出字」：recognize() 返回 InternalError
     *       并写明是哪个文件，不能回一个空结果让用户以为图不清楚。
     */
    class PpOcrOnnxEngine final : public Core::IOcrEngine
    {
    public:
        /**
         * @brief 用模型目录构造后端
         * @param modelDirectory 存放三个模型文件的目录，可以尚不存在
         * @details 构造期只做文件检查与运行库加载，不建 ONNX Runtime 会话——
         *          会话创建要读几十 MB 的模型，留到第一次识别时做
         */
        explicit PpOcrOnnxEngine(std::filesystem::path modelDirectory);

        /**
         * @brief 释放会话、运行环境与运行库句柄
         */
        ~PpOcrOnnxEngine() override;

        /**
         * @brief 后端显示名，见 Core::IOcrEngine::name()
         * @return 「PaddleOCR」
         */
        [[nodiscard]] QString name() const override;

        /**
         * @brief 可用性，见 Core::IOcrEngine::isAvailable()
         * @return true 表示三个模型文件都在、非空，且运行库与接口表都取到了
         */
        [[nodiscard]] bool isAvailable() const override;

        /**
         * @brief 不可用原因，见 Core::IOcrEngine::unavailableReason()
         * @return 中文「原因 + 替代做法」；可用时为空串
         */
        [[nodiscard]] QString unavailableReason() const override;

        /**
         * @brief 可选语言，见 Core::IOcrEngine::supportedLanguages()
         * @return 模型在位时返回 zh-Hans 与 en；不在位时为空表
         * @note 本后端的两个模型都按简体中文与英文的混合字表训练，两种标签走同一条识别路径
         */
        [[nodiscard]] QStringList supportedLanguages() const override;

        /**
         * @brief 识别，见 Core::IOcrEngine::recognize()
         * @param image 待识别图（物理像素），返回的方框就在这一张图的坐标系里
         * @param language 语言标签，空串按默认语言
         * @return 成功返回结果（一行都没认出来也算成功）
         * @return 失败返回原因：图太小、语言不支持、后端不可用、模型或运行库出错
         * @note 检测框经缩放比例折回原图坐标后裁剪，识别在裁剪图上做，方框始终与原图同系
         */
        [[nodiscard]] Base::Expected<Core::OcrResult> recognize(const QImage &image, const QString &language) override;

    private:
        struct Impl;

        /// ONNX Runtime 的资源与识别流程，头文件不暴露运行库的类型
        std::unique_ptr<Impl> m_impl;
    };

} // namespace GyanisScribe::Plugins
