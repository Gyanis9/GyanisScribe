#include "App/Application.h"

#include "Core/Logger.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStringList>

#include <format>

/**
 * @brief 进程入口
 * @details 这里暂时没有窗口：托盘与主窗口在 UI 那一批进来，本批次保证的是「起得来、日志写得出、
 *          热键登记走通、截图链路已接上」。不带参数直接跑它会安静地等在事件循环里，属预期。
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 事件循环退出码；自检模式下按启动路径是否走通返回 0 或 1
 */
int main(int argc, char *argv[])
{
    Application application(argc, argv);

    // --selftest：把启动路径跑完就退出。给 CI 与本机一个「起得来」的判据，
    // 同时不长期占着用户机器上的全局热键
    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest")))
    {
        const auto &report = application.hotkeyReport();
        GyanisScribe::Core::Logger::info(std::format("自检：热键登记 {} 项、未登记 {} 项，日志就绪 {}", report.registered, report.failures.size(), application.isReady()));
        return application.isReady() ? 0 : 1;
    }

    return QApplication::exec();
}
