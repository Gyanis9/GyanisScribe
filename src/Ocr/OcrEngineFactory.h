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

namespace GyanisScribe::Platform
{
    class IPlatformIntegration;
} // namespace GyanisScribe::Platform

namespace GyanisScribe::Settings
{
    class ISettingsStore;
} // namespace GyanisScribe::Settings

namespace GyanisScribe::Ocr
{

    /// 用户可见的默认语言标签（也是设置项 Ocr/Language 的默认值）
    constexpr const char *kDefaultOcrLanguage = "zh-Hans";

    /**
     * @brief 找语言包目录
     * @param settings 配置来源，读 Ocr/TessdataDirectory
     * @return 按「配置 → TESSDATA_PREFIX → 应用数据目录下的 tessdata」顺序第一个存在的目录；都没有时返回空路径
     * @note 返回空路径不是错误：那是「本机还没放语言包」，由调用方给出中文指引
     */
    [[nodiscard]] std::filesystem::path locateTessDataDirectory(const Settings::ISettingsStore &settings);

    /**
     * @brief 挑一个能用的后端
     * @param settings 配置来源
     * @param integration 平台能力来源
     * @return 本构建没有编入任何后端，恒为 UnavailableOcrEngine；原因里区分「语言包没找到」与
     *         「语言包在但后端没编进来」两种
     * @note 永远返回非空对象：界面拿到的后端要么能用，要么能用中文说明为什么不能用，
     *       不需要每处都判一次空指针
     */
    [[nodiscard]] std::unique_ptr<IOcrEngine> createOcrEngine(const Settings::ISettingsStore &settings, const Platform::IPlatformIntegration &integration);

    /**
     * @brief 本程序认识的语言标签
     * @return 供设置页下拉用的标签列表，顺序即显示顺序
     */
    [[nodiscard]] QStringList supportedOcrLanguageTags();

} // namespace GyanisScribe::Ocr
