#include "Main/Application.h"

#include "Main/AppController.h"
#include "App/SingleInstance.h"
#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Core/Hotkey/IHotkeyManager.h"
#include "Gui/Theme/DesignTokens.h"
#include "Gui/Theme/Motion.h"
#include "Platform/AnimationSettings.h"

#include <QFileInfo>

#include <cstdio>
#include <format>
#include <iostream>

namespace
{
    using GyanisScribe::App::CommandLineAction;
    /// 与 GitHub 账号同名，QSettings 的组织键与安装路径都取这个值
    constexpr const char *kOrganizationName = "Gyanis";

    /// 内部标识名，进注册表与配置目录，保持英文
    constexpr const char *kApplicationName = "GyanisScribe";
} // namespace

Application::Application(int &argc, char **argv, const GyanisScribe::App::CommandLineRequest &request) : QApplication(argc, argv)
{
    // 一次性的命令行动作（截图/贴图/取色/自检）：不锁单实例，也不跟常驻实例抢全局热键——
    // 它是「跑一件事就走」的启动，脚本在托盘常驻时照样能用。
    // 托盘与主窗口仍随控制器现一下身：那是控制器构造的一部分，单独剪掉等于再造一条装配路
    const bool transientAction = GyanisScribe::App::isTransientCommandLineAction(request.action);

    // 身份三件套必须排在任何 Paths/Settings 调用之前，否则 QStandardPaths 会给出无组织名的路径
    setOrganizationName(QLatin1String(kOrganizationName));
    setApplicationName(QLatin1String(kApplicationName));
    // 品牌名不进翻译目录：切英文后用户仍然要能在任务栏与窗口标题里认出「智录」这个产品
    setApplicationDisplayName(QStringLiteral("智录"));
    setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    // 正文档字号在这一处给一次：各窗口不再各自设字体，点值随系统 DPI 与字体缩放走
    auto bodyFont = font();
    bodyFont.setPointSize(static_cast<int>(GyanisScribe::Gui::kTextRolePointSizeBody));
    setFont(bodyFont);
    // 系统关掉「显示动画」时全局动效一起瞬时到位：这一档在界面建起来之前装好，
    // 第一枚 Toast 与第一张贴图就不会先按允许动画跑一拍
    GyanisScribe::Gui::setAnimationsEnabled(GyanisScribe::Platform::animationsEnabled());
    // 关窗不等于退出进程的默认逻辑只留给「关掉的是贴图/标注这类浮窗」：主窗口自己的 closeEvent
    // 会发 quitRequested，与托盘菜单的「退出」走同一个收场。不设这一行时，关掉任何一枚贴图都会
    // 把整个程序带走，托盘图标与挂在屏幕上的其它贴图一起消失
    setQuitOnLastWindowClosed(false);

    const auto logDirectory = GyanisScribe::Base::Paths::logDirectory();
    if (const auto ensured = GyanisScribe::Base::Paths::ensureDirectory(logDirectory); !ensured)
    {
        // 目录都建不出来时初始化必然也失败，原因走 Logger 的 stderr 兜底报出去；
        // 不提前 return：日志坏了不该让用户用不了截图，剩下的步骤照常走
        GyanisScribe::Base::Logger::error(std::format("启动时日志不可用：{}", ensured.error().message));
    } else if (const auto initialized = GyanisScribe::Base::Logger::initialize(logDirectory); !initialized)
    {
        GyanisScribe::Base::Logger::error(std::format("启动时日志初始化失败：{}", initialized.error().message));
    } else
    {
        GyanisScribe::Base::Logger::info(
                std::format("智录 {} 启动，日志目录 {}", applicationVersion().toStdString(), GyanisScribe::Base::Paths::toDisplayText(logDirectory).toStdString()));
    }

    // 单实例探测排在控制器与热键之前：落败的那一次启动不该装托盘、不该建主窗口、更不该去抢那七个
    // 全局热键。实测（本机连开两次进程）：排在后面时第二份会把整棵树建完（设置页那四条 ffmpeg
    // 警告就是它建主窗口留下的）、把七个热键挨个抢一遍（全部回 1409「已被占用」）才退出——
    // 用户那一侧看到的是第二个托盘图标一闪，外加一次白启动
    if (transientAction)
    {
        // 一次性启动不争单实例那把锁；控制器照建——自检量的正是这一条装配路，
        // 控制器不在时它只是一次空跑，失败也看不出来。
        // 这一路不恢复也不回写上次的贴图：那一刻的桌面不属于这个进程，--capture 还会把旧贴图截进图里
        GyanisScribe::Base::Logger::info("命令行动作启动：不锁单实例");
        m_isPrimaryInstance = true;
        m_controller        = std::make_unique<GyanisScribe::Main::AppController>(false);
    } else
    {
        m_singleInstance    = std::make_unique<GyanisScribe::App::SingleInstance>();
        m_isPrimaryInstance = m_singleInstance->acquire();
    }
    if (!m_isPrimaryInstance)
    {
        // 唤醒与退出的判断在 Main 里；界面这一侧只留一条日志，别的一概不建
        GyanisScribe::Base::Logger::info("本次启动不是主实例：不建界面、不登记热键，交给 Main 去唤前一份");
    } else if (!transientAction)
    {
        m_controller = std::make_unique<GyanisScribe::Main::AppController>();
        registerStartupHotkeys();

        // 有人再来启动时唤前一份：唤的是控制器管着的那些窗口，因此这条接线只在主实例这一支上
        QObject::connect(m_singleInstance.get(), &GyanisScribe::App::SingleInstance::secondLaunchRequested, this,
                         [this]
                         {
                             GyanisScribe::Base::Logger::info("又有人启动智录：把设置窗口唤到前台，不再开第二份");
                             m_controller->openMainWindow(GyanisScribe::Main::MainWindowPage::General);
                         });
    } else if (request.action == CommandLineAction::SelfTest)
    {
        // 自检量的正是启动路上的热键登记：这一格照样登记一遍，结果只进日志与自检输出，不据此判红。
        // 其余一次性动作（截一张、贴一张、取色）不碰全局热键，常驻实例的登记不被打断。
        registerStartupHotkeys();
    }

    // 就绪判据只看日志有没有接管成功：界面在日志坏掉时照样能用，但 CI 的自检要把这一格判红
    m_isReady = GyanisScribe::Base::Logger::isInitialized();
}

Application::~Application()
{
    // 控制器先走：它负责注销热键、收掉遮罩；日志最后冲刷，才能把这些收尾也记下来
    m_controller.reset();
    GyanisScribe::Base::Logger::shutdown();
}


bool Application::tryBecomePrimaryInstance()
{
    // 探测在构造里做过一次——那时候才知道要不要建控制器、登记热键。这里不再调 acquire()：
    // 第二次调用会连上自己刚建的那条管道，把本进程判成「非主实例」
    return m_singleInstance != nullptr && m_isPrimaryInstance;
}

bool Application::wakePrimaryInstance()
{
    return m_singleInstance != nullptr && m_singleInstance->notifyExisting();
}

bool Application::isReady() const noexcept
{
    return m_isReady;
}

void Application::registerStartupHotkeys()
{
    if (m_controller == nullptr)
    {
        return;
    }
    if (const auto report = m_controller->registerHotkeys(); report)
    {
        m_hotkeyReport = *report;
        for (const auto &failure: m_hotkeyReport.failures)
        {
            GyanisScribe::Base::Logger::warning(std::format("热键「{}」没能登记：{}", failure.combination.toStdString(), failure.error.message));
        }
    } else
    {
        GyanisScribe::Base::Logger::error(std::format("热键登记没走通：{}", report.error().message));
    }
}

const GyanisScribe::Core::HotkeyReloadReport &Application::hotkeyReport() const noexcept
{
    return m_hotkeyReport;
}

int Application::runOneShotCapture(const GyanisScribe::App::CommandLineRequest &request)
{
    using GyanisScribe::App::captureOutputsContain;

    int  exitCode = 1;
    bool finished = false;
    // 点了 pin 就要留在桌面上：那一张贴图活在本进程里，退出就把它一起带走（与 --pin 同一档做法）
    const bool keepRunning = GyanisScribe::App::captureOutputsContain(request.outputs, GyanisScribe::App::CaptureOutput::Pin);
    // 落盘路径走标准输出、失败原因走标准错误：脚本按这两条接着干活；
    // 文本按本地码页打（控制台那一侧的编码），与日志文件名那条同一处口径
    QObject::connect(m_controller.get(), &GyanisScribe::Main::AppController::commandLineCaptureDelivered, this,
                     [&exitCode, &finished, keepRunning](const QString &savedPath)
                     {
                         // 没点名 file 时这一轮没有路径可打：空串不打，免得脚本把空行当成文件名
                         if (!savedPath.isEmpty())
                         {
                             std::cout << savedPath.toLocal8Bit().constData() << '\n';
                         }
                         exitCode = 0;
                         finished = true;
                         if (!keepRunning)
                         {
                             QCoreApplication::quit();
                         }
                     });
    QObject::connect(m_controller.get(), &GyanisScribe::Main::AppController::commandLineActionFailed, this,
                     [&exitCode, &finished](const QString &reason)
                     {
                         std::fprintf(stderr, "%s\n", reason.toLocal8Bit().constData());
                         exitCode = 1;
                         finished = true;
                         QCoreApplication::quit();
                     });

    // 抓帧当下就是同步完成的（整屏那一路尤其如此）：结果可能在 exec() 之前就到，
    // 而那时 quit() 是个空动作。失败且不保活时没有可等的东西：进去就再没人叫停
    m_controller->startCommandLineCapture(request);
    if (GyanisScribe::App::shouldRunEventLoopAfterOneShot(finished, keepRunning, exitCode))
    {
        QApplication::exec();
    }
    return exitCode;
}

int Application::executeCommandLine(const GyanisScribe::App::CommandLineRequest &request)
{
    using GyanisScribe::App::CommandLineAction;

    switch (request.action)
    {
        case GyanisScribe::App::CommandLineAction::SelfTest:
        {
            const auto &report = hotkeyReport();
            // 可见顶层窗口数也要报出来：托盘装不上时入口就只剩主窗口，这一格是那条降级真的落了地的读数
            int visibleTopLevels = 0;
            for (auto *widget: QApplication::topLevelWidgets())
            {
                if (widget->isVisible())
                {
                    ++visibleTopLevels;
                }
            }
            GyanisScribe::Base::Logger::info(
                    std::format("自检：热键登记 {} 项、未登记 {} 项，日志就绪 {}，可见顶层窗口 {} 个", report.registered, report.failures.size(), isReady(), visibleTopLevels));
            return isReady() ? 0 : 1;
        }
        case GyanisScribe::App::CommandLineAction::CaptureFullScreen:
        case GyanisScribe::App::CommandLineAction::CaptureLastRegion:
        case GyanisScribe::App::CommandLineAction::CaptureWindow:
            if (m_controller == nullptr)
            {
                std::fprintf(stderr, "命令行动作没能开始：控制器没建起来（多半是日志目录不可用）\n");
                return 1;
            }
            return runOneShotCapture(request);
        case GyanisScribe::App::CommandLineAction::PinFile:
        {
            if (m_controller == nullptr)
            {
                std::fprintf(stderr, "命令行动作没能开始：控制器没建起来（多半是日志目录不可用）\n");
                return 1;
            }
            const auto pinned = m_controller->pinImageFile(request.filePath);
            if (!pinned)
            {
                std::fprintf(stderr, "%s\n", pinned.error().message.c_str());
                return 1;
            }
            // 贴图是常驻动作：贴出去之后留在桌面上，事件循环跑到用户自己关掉
            return QApplication::exec();
        }
        case GyanisScribe::App::CommandLineAction::ColorPick:
            if (m_controller == nullptr)
            {
                std::fprintf(stderr, "命令行动作没能开始：控制器没建起来（多半是日志目录不可用）\n");
                return 1;
            }
            m_controller->startColorPick();
            return QApplication::exec();
        default:
            return 0;
    }
}
