/**
 * @file OcrEngineFactory.h
 * @brief 识别后端的选型与语言词表
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Ocr/IOcrEngine.h"

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

namespace GyanisScribe::Ocr
{

    /// 用户可见的默认语言标签（也是设置项 Ocr/Language 的默认值）
    constexpr const char *kDefaultOcrLanguage = "zh-Hans";

    /**
     * @brief 语言包目录的候选顺序
     * @param settings 配置来源，读 Ocr/TessdataDirectory
     * @param applicationDirectory 可执行体所在目录，随包的语言包在它下面的 tessdata 里
     * @return 按「配置 → TESSDATA_PREFIX → 程序目录/tessdata → 应用数据目录/tessdata」排好的候选，不筛存在性
     * @details 单独暴露是为了让查找顺序可测：随包那份必须排在用户数据目录之前，否则装了包还要用户自己放文件。
     */
    [[nodiscard]] std::vector<std::filesystem::path> tessDataCandidateDirectories(const Core::ISettingsStore &settings, const std::filesystem::path &applicationDirectory);

    /**
     * @brief 取第一个真的放着语言包的候选目录
     * @param candidates tessDataCandidateDirectories 的结果
     * @return 第一个含 .traineddata 的目录；都没有时返回空路径
     * @note 只有子目录、没有 traineddata 的目录不算「有语言包」
     */
    [[nodiscard]] std::filesystem::path firstDirectoryWithLanguagePacks(const std::vector<std::filesystem::path> &candidates);

    /**
     * @brief 找语言包目录
     * @param settings 配置来源，读 Ocr/TessdataDirectory
     * @return 按 tessDataCandidateDirectories 的顺序第一个放着语言包的目录；都没有时返回空路径
     * @note 返回空路径不是错误：那是「本机还没有可用的语言包」，由调用方给出中文指引
     */
    [[nodiscard]] std::filesystem::path locateTessDataDirectory(const Core::ISettingsStore &settings);

    /**
     * @brief 挑一个能用的后端
     * @param settings 配置来源
     * @param integration 平台能力来源
     * @return 本构建没有编入任何后端，恒为 UnavailableOcrEngine；原因里区分「语言包没找到」与
     *         「语言包在但后端没编进来」两种
     * @note 永远返回非空对象：界面拿到的后端要么能用，要么能用中文说明为什么不能用，
     *       不需要每处都判一次空指针
     */
    [[nodiscard]] std::unique_ptr<IOcrEngine> createOcrEngine(const Core::ISettingsStore &settings, const Platform::IPlatformIntegration &integration);

    /**
     * @brief 本程序认识的语言标签
     * @return 供设置页下拉用的标签列表，顺序即显示顺序
     */
    [[nodiscard]] QStringList supportedOcrLanguageTags();

} // namespace GyanisScribe::Ocr
