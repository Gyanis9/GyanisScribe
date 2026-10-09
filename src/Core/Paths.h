/**
 * @file Paths.h
 * @brief 应用各用途目录的默认取值，全部经 QStandardPaths 解析
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <filesystem>

namespace GyanisScribe::Core
{

    /**
     * @brief 目录提供者
     * @details 只给默认值，不读设置：用户改过的截图/录屏目录由 Settings 提供，由调用方（App 层）在这里之外
     *          做替换。这样 Core 不依赖 Settings，容器内无 GUI 也能跑用例。
     * @note 全部方法都要在 QCoreApplication 存在之后调用，QStandardPaths 的取值依赖应用与组织名已设置。
     */
    class Paths
    {
    public:
        /**
         * @brief 应用可写数据根目录
         * @return 已存在或即将存在的目录路径
         */
        [[nodiscard]] static std::filesystem::path applicationDataDirectory();

        /**
         * @brief 日志目录
         * @return 位于可写数据目录下的 logs
         */
        [[nodiscard]] static std::filesystem::path logDirectory();

        /**
         * @brief 缓存目录
         * @return 系统缓存位置下的应用子目录，历史缩略图放这里
         */
        [[nodiscard]] static std::filesystem::path cacheDirectory();

        /**
         * @brief 截图默认目录
         * @return 用户图片目录下的 GyanisScribe
         */
        [[nodiscard]] static std::filesystem::path defaultScreenshotDirectory();

        /**
         * @brief 录屏默认目录
         * @return 用户视频目录下的 GyanisScribe
         */
        [[nodiscard]] static std::filesystem::path defaultRecordingDirectory();

        /**
         * @brief 确保目录存在
         * @param directory 目标目录
         * @return 成功返回 void；失败返回原因，文案里带目录与系统错误
         */
        [[nodiscard]] static Expected<void> ensureDirectory(const std::filesystem::path &directory);
    };

} // namespace GyanisScribe::Core
