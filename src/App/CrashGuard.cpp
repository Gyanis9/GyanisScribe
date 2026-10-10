#include "App/CrashGuard.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Text.h"

#include <cstdlib>
#include <exception>
#include <format>

namespace
{
    /// 从异常指针里问出一句话；问不出类型时也给出可读的兜底描述
    std::string describeException(const std::exception_ptr &error)
    {
        if (!error)
        {
            return "没有异常信息（可能是直接 abort 或栈被破坏）";
        }
        try
        {
            std::rethrow_exception(error);
        } catch (const std::exception &raised)
        {
            return raised.what();
        } catch (...)
        {
            return "抛出的不是 std::exception 派生的对象，具体类型问不出来";
        }
    }
} // namespace

namespace GyanisScribe::App
{
    QString crashMessageFor(const std::exception_ptr &error, const std::string_view logDirectory)
    {
        const auto text = QString::fromUtf8(
                GyanisScribe::Core::uiFormat(
                        GyanisScribe::Core::uiTextUtf8("智录遇到未处理的错误：{}。现场已尽量落到日志，请到 {} 找最近一份日志；如果反复出现，请把日志与出错的操作步骤一起反馈"),
                        describeException(error), logDirectory)
                        .c_str());
        return text;
    }

    void installTerminateHandler()
    {
        // 记完再 abort：先冲刷日志，否则 spdlog 的异步队列可能连同这条一起丢
        std::set_terminate(
                []
                {
                    const auto message = crashMessageFor(std::current_exception(), Core::Paths::logDirectory().string());
                    Core::Logger::error(message.toStdString());
                    Core::Logger::shutdown();
                    std::abort();
                });
    }

} // namespace GyanisScribe::App
