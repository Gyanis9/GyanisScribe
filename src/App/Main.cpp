#include "App/Application.h"
#include "App/CommandLine.h"

#include "App/CrashGuard.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Text.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStringList>

#include <cstdio>
#include <format>
#include <iostream>

namespace
{
    /// 把主进程的 argv 收成 QStringList：参数要在 QApplication 之前读（命令行动作影响装配方式），
    /// 按本地码页转——与日志文件名那条同一处口径
    [[nodiscard]] QStringList argumentsFromMain(const int argc, char *argv[])
    {
        QStringList arguments;
        for (int index = 1; index < argc; ++index)
        {
            arguments << QString::fromLocal8Bit(argv[index]);
        }
        return arguments;
    }
} // namespace

/**
 * @brief 进程入口
 * @details 启动顺序是刻意的：参数先解析（命令行动作决定装配方式：一次性动作不锁单实例、不登记热键），
 *          应用对象再建，命令行动作在崩溃兜底与单实例之前就走掉——它们要量的或做的只有那一件事，
 *          而且不能在用户机器上长期占着全局热键。
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 事件循环退出码；自检按启动路径给出 0/1，截图按落盘成败给出 0/1，参数写错给出 2
 */
int main(int argc, char *argv[])
{
    const auto parsed = GyanisScribe::App::parseStartupArguments(argumentsFromMain(argc, argv));
    if (!parsed.errorText.isEmpty())
    {
        std::fprintf(stderr, "%s\n%s\n", parsed.errorText.toLocal8Bit().constData(), GyanisScribe::App::commandLineUsageText().toLocal8Bit().constData());
        return 2;
    }
    if (parsed.request.action == GyanisScribe::App::CommandLineAction::Help)
    {
        std::cout << GyanisScribe::App::commandLineUsageText().toLocal8Bit().constData() << std::endl;
        return 0;
    }
    if (parsed.request.action == GyanisScribe::App::CommandLineAction::Version)
    {
        std::printf("智录（GyanisScribe）%s\n", PROJECT_VERSION);
        return 0;
    }

    Application application(argc, argv, parsed.request);

    // 命令行动作：截图三档、贴图与取色各自收场，普通启动落到下面常驻那一路
    if (parsed.request.action != GyanisScribe::App::CommandLineAction::None)
    {
        return application.executeCommandLine(parsed.request);
    }

    // 兜底要在任何可能崩的步骤之前装好：装晚了那条崩就只是「程序没了」
    GyanisScribe::App::installCrashDumpWriter(GyanisScribe::Core::Paths::logDirectory(), QCoreApplication::applicationVersion().toStdWString());
    GyanisScribe::App::installTerminateHandler();

    // 单实例：第二次启动把前一份唤出来然后自己退出，而不是两个实例抢同一批全局热键
    if (!application.tryBecomePrimaryInstance())
    {
        const auto woken = application.wakePrimaryInstance();
        GyanisScribe::Core::Logger::info(woken ? "已有一个智录在运行：把它唤到前台后本次启动退出" : "已有一个智录在运行，但没能通知到它：本次启动退出，避免两份实例抢全局热键");
        return 0;
    }

    try
    {
        return QApplication::exec();
    } catch (...)
    {
        // 事件分发里的异常默认会走到 terminate；这里接住是为了给出退出码与那句人话
        const auto message =
                GyanisScribe::App::crashMessageFor(std::current_exception(), GyanisScribe::Core::Paths::toDisplayText(GyanisScribe::Core::Paths::logDirectory()).toStdString());
        GyanisScribe::Core::Logger::error(message.toStdString());
        return GyanisScribe::App::kCrashExitCode;
    }
}
