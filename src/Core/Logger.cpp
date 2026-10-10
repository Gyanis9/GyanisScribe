#include "Core/Logger.h"

#include "Core/Text.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <system_error>

namespace GyanisScribe::Core
{
    namespace
    {

        /// 单个日志文件大小上限，超出后由 spdlog 轮转，避免长期运行把磁盘写满
        constexpr std::size_t kMaximumLogFileBytes = 5U * 1024U * 1024U;

        /// 轮转保留的历史份数
        constexpr std::size_t kMaximumRotatedFiles = 5U;

        /// 落盘文件名
        constexpr std::string_view kLogFileBaseName = "GyanisScribe.log";

        /**
         * @brief 门面背后的状态
         * @details 有意做成函数内静态、随进程退出自动析构而不是显式 new：Logger 的使用者遍布各模块，
         *          退出顺序无法约束，交给语言保证的析构时机最省事；shutdown() 只负责提前冲刷。
         */
        struct LoggerState
        {
            std::shared_ptr<spdlog::logger> logger; ///< 实际写出的那一路，未初始化时为空
            std::mutex                      mutex;  ///< 保护 logger 的换入换出，日志本身由 spdlog 内部串行化
        };

        LoggerState &state()
        {
            static LoggerState instance;
            return instance;
        }

        spdlog::level::level_enum toSpdlogLevel(Severity severity)
        {
            // 用 switch 而不是查表：新增枚举值时编译器会提醒这里还没跟上
            switch (severity)
            {
                case Severity::Debug:
                    return spdlog::level::debug;
                case Severity::Info:
                    return spdlog::level::info;
                case Severity::Warning:
                    return spdlog::level::warn;
                case Severity::Error:
                    return spdlog::level::err;
            }
            // 上面已穷举，走到这里说明传进了非法取值：按最严重的档处理，宁可多落一条也不静默丢弃
            return spdlog::level::err;
        }

        std::string formatLocation(std::string_view message, const std::source_location &location)
        {
            // 正文在前、调用点在后：日志行首已被 spdlog 的级别与时间戳占住，把可读的中文放前面更好扫
            return std::format("{} [{}:{} in {}]", message, location.file_name(), location.line(), location.function_name());
        }

    } // namespace

    Expected<void> Logger::initialize(const std::filesystem::path &logDirectory, Severity minimumSeverity)
    {
        auto            &current = state();
        std::scoped_lock lock(current.mutex);

        // 已经接管输出就保持原句柄：重复初始化会把打开的文件踢掉，反而丢掉前一条日志
        if (current.logger)
        {
            return {};
        }

        std::error_code error;
        std::filesystem::create_directories(logDirectory, error);
        if (error && !std::filesystem::is_directory(logDirectory))
        {
            return std::unexpected(Error{ErrorCode::FileWriteFailed,
                                         GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("日志目录建不出来：{}（原因 {}）。请把 Paths 里的日志目录改到一个可写的路径再重启"),
                                                     logDirectory.string(), error.message())});
        }

        try
        {
            const auto filePath    = logDirectory / std::string(kLogFileBaseName);
            auto       fileSink    = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(filePath.string(), kMaximumLogFileBytes, kMaximumRotatedFiles);
            auto       consoleSink = std::make_shared<spdlog::sinks::stdout_sink_mt>();

            // 控制台与文件用同一份 logger：级别只解析一次，两处读数不会各说各话
            current.logger = std::make_shared<spdlog::logger>("GyanisScribe", spdlog::sinks_init_list{fileSink, consoleSink});
            current.logger->set_level(toSpdlogLevel(minimumSeverity));
            current.logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
            current.logger->flush_on(spdlog::level::warn);
        } catch (const spdlog::spdlog_ex &exception)
        {
            // spdlog 只抛这一种；文件被别的进程独占时走到这里，交回调用方提示用户
            return std::unexpected(
                    Error{ErrorCode::FileWriteFailed, GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("日志文件打不开：{}（原因 {}）。请关闭占用它的程序，或换一个日志目录"),
                                                                  (logDirectory / std::string(kLogFileBaseName)).string(), exception.what())});
        }

        return {};
    }

    void Logger::shutdown() noexcept
    {
        auto            &current = state();
        std::scoped_lock lock(current.mutex);
        if (current.logger)
        {
            current.logger->flush();
            current.logger.reset();
        }
    }

    bool Logger::isInitialized() noexcept
    {
        return static_cast<bool>(state().logger);
    }

    void Logger::log(Severity severity, std::string_view message, const std::source_location &location)
    {
        auto            &current = state();
        std::scoped_lock lock(current.mutex);

        if (!current.logger)
        {
            // 未初始化也要出声：这条兜底是「所有错误必须有日志」的最后一道，不能因为顺序问题变成静默
            std::fprintf(stderr, "[log:%d] %.*s (%s:%u)\n", static_cast<int>(severity), static_cast<int>(message.size()), message.data(), location.file_name(), location.line());
            return;
        }

        current.logger->log(toSpdlogLevel(severity), formatLocation(message, location));
    }

    void Logger::info(std::string_view message, const std::source_location &location)
    {
        log(Severity::Info, message, location);
    }

    void Logger::warning(std::string_view message, const std::source_location &location)
    {
        log(Severity::Warning, message, location);
    }

    void Logger::error(std::string_view message, const std::source_location &location)
    {
        log(Severity::Error, message, location);
    }

} // namespace GyanisScribe::Core
