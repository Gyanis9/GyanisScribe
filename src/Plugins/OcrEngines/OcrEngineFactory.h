/**
 * @file OcrEngineFactory.h
 * @brief 识别后端的选型与模型目录探路（语言词表在 Core/Ocr/OcrLanguage.h）
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Ocr/IOcrEngine.h"
#include "Core/Ocr/OcrLanguage.h"

#include <QString>
#include <QStringList>

#include <filesystem>
#include <memory>
#include <vector>

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::Core
{
    class ISettingsStore;
} // namespace GyanisScribe::Core

namespace GyanisScribe::Plugins
{

    /**
     * @brief PaddleOCR 模型目录的候选顺序
     * @param settings 配置来源，读 Ocr/ModelDirectory
     * @param applicationDirectory 可执行体所在目录，随包的模型在它下面的 ppocr 里
     * @return 按「配置 → 程序目录/ppocr → 应用数据目录/ppocr」排好的候选，不筛存在性
     * @details 单独暴露是为了让查找顺序可测：随包那份必须排在用户数据目录之前，否则装了包还要用户自己放模型。
     */
    [[nodiscard]] std::vector<std::filesystem::path> ppOcrModelCandidateDirectories(const Core::ISettingsStore &settings, const std::filesystem::path &applicationDirectory);

    /**
     * @brief 取第一个放着完整模型的候选目录
     * @param candidates ppOcrModelCandidateDirectories 的结果
     * @return 第一个三件模型文件都存在且非空的目录；都没有时返回空路径
     * @note 只有一部分文件的目录不算「有模型」
     */
    [[nodiscard]] std::filesystem::path firstDirectoryWithModels(const std::vector<std::filesystem::path> &candidates);

    /**
     * @brief 找 PaddleOCR 模型目录
     * @param settings 配置来源，读 Ocr/ModelDirectory
     * @param applicationDirectory 程序目录，随包模型在它下面的 ppocr 里；空表示当前进程的可执行体目录
     * @return 按 ppOcrModelCandidateDirectories 的顺序第一个放着完整模型的目录；都没有时返回空路径
     * @note 返回空路径不是错误：那是「本机还没有识别模型」，由调用方给出中文指引
     */
    [[nodiscard]] std::filesystem::path locatePpOcrModelDirectory(const Core::ISettingsStore &settings, const std::filesystem::path &applicationDirectory = {});

    /**
     * @brief 挑一个能用的后端
     * @param settings 配置来源
     * @param integration 平台能力来源
     * @param applicationDirectory 程序目录，随包模型在它下面的 ppocr 里；空表示当前进程的可执行体目录
     * @return 依次尝试 PaddleOCR 与系统识别；都不可用时交回 UnavailableOcrEngine，
     *         原因先列模型目录与缺的文件，再附系统识别不可用的原因
     * @note 永远返回非空对象：界面拿到的后端要么能用，要么能用中文说明为什么不能用，
     *       不需要每处都判一次空指针
     */
    [[nodiscard]] std::unique_ptr<Core::IOcrEngine> createOcrEngine(const Core::ISettingsStore &settings, const Platform::IPlatformIntegration &integration,
                                                                     const std::filesystem::path &applicationDirectory = {});

} // namespace GyanisScribe::Plugins
