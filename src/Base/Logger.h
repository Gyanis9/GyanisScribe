/**
 * @file Logger.h
 * @brief 全局日志入口，基于 spdlog 提供文件轮转与控制台输出
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <filesystem>
#include <source_location>
#include <string_view>

namespace GyanisScribe::Base
{

    /**
     * @brief 日志严重级，与 spdlog 的级别一一对应
     */
    enum class Severity
    {
        Debug,   ///< 仅本机诊断，默认不落盘
        Info,    ///< 关键流程节点
        Warning, ///< 已降级但还能用
        Error,   ///< 操作失败，必须同时给出用户可见的原因
    };

    /**
     * @brief 进程级日志门面
     * @details 未初始化时写入退化为 stderr 而不是丢弃：「所有错误必须有日志」不该依赖调用顺序。
     */
    class Logger
    {
    public:
        /**
         * @brief 打开轮转文件与控制台两路输出
         * @param logDirectory 日志目录，不存在时尝试创建
         * @param minimumSeverity 落盘最低级别，低于它的调用直接返回
         * @return 成功返回空载荷；失败返回原因（目录建不出来或文件被占用）
         * @note 重复调用直接返回成功而不重建 sink，避免启动路径上把已打开的句柄踢掉
         */
        [[nodiscard]] static Expected<void> initialize(const std::filesystem::path &logDirectory, Severity minimumSeverity = Severity::Info);

        /**
         * @brief 冲刷并关闭所有 sink，进程退出前调用
         */
        static void shutdown() noexcept;

        /**
         * @brief 日志是否已接管输出
         * @return true 已初始化，写入进文件与控制台
         * @return false 未初始化，写入只进 stderr
         */
        [[nodiscard]] static bool isInitialized() noexcept;

        /**
         * @brief 写一条日志
         * @param severity 严重级
         * @param message 正文，中文
         * @param location 调用点，默认取当前帧，供 source_location 定位
         */
        static void log(Severity severity, std::string_view message, const std::source_location &location = std::source_location::current());

        /**
         * @brief 写一条 Info 级日志
         * @param message 正文，中文
         * @param location 调用点
         */
        static void info(std::string_view message, const std::source_location &location = std::source_location::current());

        /**
         * @brief 写一条 Warning 级日志
         * @param message 正文，中文
         * @param location 调用点
         */
        static void warning(std::string_view message, const std::source_location &location = std::source_location::current());

        /**
         * @brief 写一条 Error 级日志
         * @param message 正文，中文
         * @param location 调用点
         */
        static void error(std::string_view message, const std::source_location &location = std::source_location::current());

    private:
        Logger() = default;
    };

} // namespace GyanisScribe::Base
