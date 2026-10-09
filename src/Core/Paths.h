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

#include <QString>

#include <filesystem>
#include <string_view>

class QDateTime;

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
        /**
         * @brief 路径换成能直接拼进界面文案的字符串
         * @param path 文件路径
         * @return UTF-16 的那串路径
         * @details 走 wstring 而不是 narrow string：Windows 上 narrow 按本地 ANSI 码页解释，
         *          中文用户名下的截图目录会被读成另一条不存在的路。
         */
        [[nodiscard]] static QString toDisplayText(const std::filesystem::path &path);

        /**
         * @brief 把 toDisplayText 的结果换回路径
         * @param text 界面里的那串路径
         * @return 文件路径
         */
        [[nodiscard]] static std::filesystem::path fromDisplayText(const QString &text);

        [[nodiscard]] static Expected<void> ensureDirectory(const std::filesystem::path &directory);

        /**
         * @brief 生成导出图像的文件名
         * @param moment 导出时刻
         * @param extension 扩展名，不带点，例如 png、mp4
         * @return 形如 GyanisScribe_20261009_235959_123.png 的文件名
         * @details 命名规则的唯一出处：贴图保存与标注保存共用同一个格式，两处各写一份就会在
         *          某一天变成同一目录里两套文件名。毫秒位让同一秒内的多次导出不互相覆盖。
         */
        [[nodiscard]] static QString imageFileName(const QDateTime &moment, std::string_view extension);
    };

} // namespace GyanisScribe::Core
