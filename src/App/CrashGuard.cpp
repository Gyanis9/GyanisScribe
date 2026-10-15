#include "App/CrashGuard.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Text.h"

#include <QtGlobal>

#include <windows.h>
#include <dbghelp.h>

#include <cstdlib>
#include <exception>
#include <format>
#include <stdexcept>

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

    /// 转储命名的唯一一处：宽字符 + 调用方给的缓冲区，崩溃处理器里也要能调它（不许分配）
    constexpr std::size_t kDumpNameCapacity = 512;

    int formatCrashDumpFileName(wchar_t *const buffer, const std::size_t capacity, const wchar_t *const applicationVersion, const GyanisScribe::App::CrashStamp &stamp,
                                const std::uint32_t processId)
    {
        // 用 %ls 而不是 %s：宽字符格式串里 %s 的语义随标准库而异（MSVC 当宽串），
        // 命名规则是唯一出处，跨平台时名字拼法不变。
        return std::swprintf(buffer, capacity, L"GyanisScribe_%ls_%04d%02d%02d-%02d%02d%02d_%u.dmp", applicationVersion, stamp.year, stamp.month, stamp.day, stamp.hour,
                             stamp.minute, stamp.second, processId);
    }

    // 崩溃处理器里唯一能读的现场：都在装过滤器之前写好，之后不再改，也不持有所有权
    constexpr std::size_t kPathCapacity                      = 4096;
    wchar_t               g_dumpDirectoryPath[kPathCapacity] = L"";
    wchar_t               g_applicationVersion[128]          = L"unknown";

    /// 把源串截断着拷进固定缓冲区：宁可不完整也不越界（越界会把崩溃现场变成二次故障）
    void copyTruncated(wchar_t *const destination, const std::size_t capacity, const std::wstring_view source)
    {
        const auto taken = source.size() < capacity - 1 ? source.size() : capacity - 1;
        for (std::size_t index = 0; index < taken; ++index)
        {
            destination[index] = source[index];
        }
        destination[taken] = L'\0';
    }

    /**
     * @brief 未处理异常的兜底：开文件、写 minidump、按约定的退出码结束进程
     * @details 只做这三件事：不写日志（日志的锁可能正被崩掉的那条线程持有，等下去就卡死在这里），
     *          不构造 Qt 对象（它们会分配、会取事件泵的锁），也不链回上一个过滤器
     *          ——本程序只有这一处装过滤器。
     */
    LONG WINAPI writeMiniDump(PEXCEPTION_POINTERS pointers)
    {
        if (g_dumpDirectoryPath[0] == L'\0' || pointers == nullptr)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        SYSTEMTIME moment{};
        GetLocalTime(&moment);
        const GyanisScribe::App::CrashStamp stamp{static_cast<int>(moment.wYear), static_cast<int>(moment.wMonth),  static_cast<int>(moment.wDay),
                                                  static_cast<int>(moment.wHour), static_cast<int>(moment.wMinute), static_cast<int>(moment.wSecond)};

        wchar_t fileName[kPathCapacity] = L"";
        if (formatCrashDumpFileName(fileName, kPathCapacity, g_applicationVersion, stamp, GetCurrentProcessId()) <= 0)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        wchar_t fullPath[kPathCapacity] = L"";
        if (std::swprintf(fullPath, kPathCapacity, L"%s\\%s", g_dumpDirectoryPath, fileName) <= 0)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const HANDLE file = CreateFileW(fullPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION stream{};
            stream.ThreadId          = GetCurrentThreadId();
            stream.ExceptionPointers = pointers;
            stream.ClientPointers    = FALSE;
            // 间接引用的内存那一档：栈与线程上下文都在，够定位崩在哪一行，体积又不至于吃掉盘
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpWithIndirectlyReferencedMemory, &stream, nullptr, nullptr);
            CloseHandle(file);
        }

        // 退出码与「未捕获异常」那一类对齐，CI 与用户都按同一个码认崩溃
        TerminateProcess(GetCurrentProcess(), static_cast<uint>(GyanisScribe::App::kCrashExitCode));
        return EXCEPTION_EXECUTE_HANDLER;
    }
} // namespace

namespace GyanisScribe::App
{
    QString crashMessageFor(const std::exception_ptr &error, const std::string_view logDirectory)
    {
        const auto text =
                QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8(
                                                                       "智录遇到未处理的错误：{}。现场已尽量落到日志与崩溃转储，请到 {} 找最近一份日志（同目录下的 .dmp 是转储）；"
                                                                       "如果反复出现，请把日志、转储与出错的操作步骤一起反馈"),
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
                    const auto message = crashMessageFor(std::current_exception(), Core::Paths::toDisplayText(Core::Paths::logDirectory()).toStdString());
                    Core::Logger::error(message.toStdString());
                    Core::Logger::shutdown();
                    std::abort();
                });
    }

    std::wstring crashDumpFileName(const std::wstring_view applicationVersion, const CrashStamp &stamp, const std::uint32_t processId)
    {
        wchar_t buffer[kDumpNameCapacity] = L"";
        if (formatCrashDumpFileName(buffer, kDumpNameCapacity, applicationVersion.data(), stamp, processId) <= 0)
        {
            // 版本号里带不上这些格式符的形状才会走到这里；给一句人话而不是留一个空名字，
            // 空名字会让调用方以为「没有转储」而不去找
            throw std::invalid_argument("崩溃转储的文件名拼不出来：版本号或时间戳的形状不对");
        }
        return std::wstring(buffer);
    }

    void installCrashDumpWriter(const std::filesystem::path &dumpDirectory, const std::wstring_view applicationVersion)
    {
        if (const auto ready = Core::Paths::ensureDirectory(dumpDirectory); !ready)
        {
            Core::Logger::warning(std::format("崩溃转储的目录建不出来，崩了不会留下 minidump：{}", ready.error().message));
            return;
        }

        // 装过滤器之前把现场写进静态缓冲区：处理器里不许再分配，也不许读 std::wstring 的堆内存
        copyTruncated(g_dumpDirectoryPath, kPathCapacity, dumpDirectory.wstring());
        copyTruncated(g_applicationVersion, std::size(g_applicationVersion), applicationVersion.empty() ? std::wstring_view{L"unknown"} : applicationVersion);
        SetUnhandledExceptionFilter(writeMiniDump);
        Core::Logger::info(std::format("崩溃转储会写到 {}", Core::Paths::toDisplayText(dumpDirectory).toStdString()));
    }

} // namespace GyanisScribe::App
