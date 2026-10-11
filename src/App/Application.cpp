#include "App/Application.h"

#include "App/AppController.h"
#include "App/SingleInstance.h"
#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Hotkey/IHotkeyManager.h"
#include "Ui/DesignTokens.h"

#include <format>

namespace
{
    /// 与 GitHub 账号同名，QSettings 的组织键与安装路径都取这个值
    constexpr const char *kOrganizationName = "Gyanis";

    /// 内部标识名，进注册表与配置目录，保持英文
    constexpr const char *kApplicationName = "GyanisScribe";
} // namespace

Application::Application(int &argc, char **argv) : QApplication(argc, argv)
{
    // 身份三件套必须排在任何 Paths/Settings 调用之前，否则 QStandardPaths 会给出无组织名的路径
    setOrganizationName(QLatin1String(kOrganizationName));
    setApplicationName(QLatin1String(kApplicationName));
    // 品牌名不进翻译目录：切英文后用户仍然要能在任务栏与窗口标题里认出「智录」这个产品
    setApplicationDisplayName(QStringLiteral("智录"));
    setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    // 正文档字号在这一处给一次：各窗口不再各自设字体，点值随系统 DPI 与字体缩放走
    auto bodyFont = font();
    bodyFont.setPointSize(static_cast<int>(GyanisScribe::Ui::kTextRolePointSizeBody));
    setFont(bodyFont);
    // 主窗口关掉就回托盘：这是常驻工具，退出只走托盘菜单那一条路。
    // Qt 的默认是「最后一个窗口关掉就退进程」，不设这一行时用户关掉设置窗会把程序整个带走，
    // 托盘图标与挂在屏幕上的贴图一起消失
    setQuitOnLastWindowClosed(false);

    const auto logDirectory = GyanisScribe::Core::Paths::logDirectory();
    if (const auto ensured = GyanisScribe::Core::Paths::ensureDirectory(logDirectory); !ensured)
    {
        // 目录都建不出来时初始化必然也失败，原因走 Logger 的 stderr 兜底报出去；
        // 不提前 return：日志坏了不该让用户用不了截图，剩下的步骤照常走
        GyanisScribe::Core::Logger::error(std::format("启动时日志不可用：{}", ensured.error().message));
    } else if (const auto initialized = GyanisScribe::Core::Logger::initialize(logDirectory); !initialized)
    {
        GyanisScribe::Core::Logger::error(std::format("启动时日志初始化失败：{}", initialized.error().message));
    } else
    {
        GyanisScribe::Core::Logger::info(
                std::format("智录 {} 启动，日志目录 {}", applicationVersion().toStdString(), GyanisScribe::Core::Paths::toDisplayText(logDirectory).toStdString()));
    }

    // 单实例探测排在控制器与热键之前：落败的那一次启动不该装托盘、不该建主窗口、更不该去抢那六个
    // 全局热键。实测（本机连开两次进程）：排在后面时第二份会把整棵树建完（设置页那四条 ffmpeg
    // 警告就是它建主窗口留下的）、把六个热键挨个抢一遍（全部回 1409「已被占用」）才退出——
    // 用户那一侧看到的是第二个托盘图标一闪，外加一次白启动
    m_singleInstance    = std::make_unique<GyanisScribe::App::SingleInstance>();
    m_isPrimaryInstance = m_singleInstance->acquire();
    if (!m_isPrimaryInstance)
    {
        // 唤醒与退出的判断在 Main 里；界面这一侧只留一条日志，别的一概不建
        GyanisScribe::Core::Logger::info("本次启动不是主实例：不建界面、不登记热键，交给 Main 去唤前一份");
    } else
    {
        m_controller = std::make_unique<GyanisScribe::App::AppController>();

        // 热键登记失败不阻断启动：组合键被别的程序占了也要能让用户从界面里操作
        if (const auto report = m_controller->registerHotkeys(); report)
        {
            m_hotkeyReport = *report;
            for (const auto &failure: m_hotkeyReport.failures)
            {
                GyanisScribe::Core::Logger::warning(std::format("热键「{}」没能登记：{}", failure.combination.toStdString(), failure.error.message));
            }
        } else
        {
            GyanisScribe::Core::Logger::error(std::format("热键登记没走通：{}", report.error().message));
        }

        // 有人再来启动时唤前一份：唤的是控制器管着的那些窗口，因此这条接线只在主实例这一支上
        QObject::connect(m_singleInstance.get(), &GyanisScribe::App::SingleInstance::secondLaunchRequested, this,
                         [this]
                         {
                             GyanisScribe::Core::Logger::info("又有人启动智录：把设置窗口唤到前台，不再开第二份");
                             m_controller->openMainWindow(GyanisScribe::App::MainWindowPage::General);
                         });
    }

    // 就绪判据只看日志有没有接管成功：界面在日志坏掉时照样能用，但 CI 的自检要把这一格判红
    m_isReady = GyanisScribe::Core::Logger::isInitialized();
}

Application::~Application()
{
    // 控制器先走：它负责注销热键、收掉遮罩；日志最后冲刷，才能把这些收尾也记下来
    m_controller.reset();
    GyanisScribe::Core::Logger::shutdown();
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

const GyanisScribe::Hotkey::HotkeyReloadReport &Application::hotkeyReport() const noexcept
{
    return m_hotkeyReport;
}
