/**
 * @file Win32Autostart.h
 * @brief 开机启动项要写的那条命令行的拼法
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QString>

#include <string_view>

namespace GyanisScribe::Platform
{

    /// Run 键下那一条的名字：写与摘用的是同一个名字，改名时两处一起改
    inline constexpr std::string_view kAutostartValueName = "GyanisScribe";

    /**
     * @brief 拼出开机启动项要写的那条命令行
     * @param executablePath 可执行体的完整路径（QCoreApplication::applicationFilePath()）
     * @return 带引号的命令行；路径为空或全是空白时返回空串，调用方按「取不到自身路径」报错
     * @details 必须带引号：装到 Program Files 这类带空格的目录下时，不加引号的路径会被
     *          Windows 按空格切成两截，开机时起不来，而且不留任何提示。
     */
    [[nodiscard]] QString autostartCommandLine(const QString &executablePath);

} // namespace GyanisScribe::Platform
