#include "App/Application.h"

#include "App/CrashGuard.h"

#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Text.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QStringList>
#include <QWidget>

#include <format>

namespace
{
    /**
     * @brief 摆出命令行参数这一条路
     * @param application 已经建好的应用对象（--version 要用它的应用名与版本号）
     * @return 自检开关
     * @details --help/--version 由 Qt 打印后直接结束进程；不认识的参数也当场退出而不是
     *          装成「没带参数」安静地起一个界面——那样用户会以为那条参数生效了。
     */
    bool parseCommandLine(QCoreApplication &application)
    {
        QCommandLineParser parser;
        // 这两句是给用户看的文案，走文案目录：装了英文语言包时 --help 也该出英文
        parser.setApplicationDescription(GyanisScribe::Core::uiText("智录：截图、标注、贴图、取色、录屏与文字识别"));
        parser.addHelpOption();
        parser.addVersionOption();

        const QCommandLineOption selfTest{QStringLiteral("selftest"), GyanisScribe::Core::uiText("把启动路径跑完就退出：热键登记与日志就绪算通过（退出码 0），否则 1")};
        parser.addOption(selfTest);
        parser.process(application);
        return parser.isSet(selfTest);
    }
} // namespace

/**
 * @brief 进程入口
 * @details 启动顺序是刻意的：应用对象先建（日志目录、配置与主题都依赖它），参数再解析，
 *          自检分支在崩溃兜底与单实例之前就走掉——自检要量的就是「不带这些副作用能不能起得来」，
 *          而且不能在用户机器上长期占着全局热键。
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 事件循环退出码；自检模式下按启动路径是否走通返回 0 或 1
 */
int main(int argc, char *argv[])
{
    Application application(argc, argv);

    // --selftest：把启动路径跑完就退出。给 CI 与本机一个「起得来」的判据
    if (parseCommandLine(application))
    {
        const auto &report = application.hotkeyReport();
        // 可见顶层窗口数也要报出来：托盘装不上时入口就只剩主窗口，这一格是那条降级真的落了地的读数
        int visibleTopLevels = 0;
        for (auto *widget: QApplication::topLevelWidgets())
        {
            if (widget->isVisible())
            {
                ++visibleTopLevels;
            }
        }
        GyanisScribe::Core::Logger::info(std::format("自检：热键登记 {} 项、未登记 {} 项，日志就绪 {}，可见顶层窗口 {} 个", report.registered, report.failures.size(),
                                                     application.isReady(), visibleTopLevels));
        return application.isReady() ? 0 : 1;
    }

    // 兜底要在任何可能崩的步骤之前装好：装晚了那条崩就只是「程序没了」
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
