#include "App/Application.h"

#include "App/AppController.h"
#include "App/SingleInstance.h"
#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Hotkey/IHotkeyManager.h"

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
        GyanisScribe::Core::Logger::info(std::format("智录 {} 启动，日志目录 {}", applicationVersion().toStdString(), logDirectory.string()));
    }

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

    // 单实例守卫在控制器之后建：第二次启动要唤的是控制器管着的那些窗口
    m_singleInstance = std::make_unique<GyanisScribe::App::SingleInstance>();
    QObject::connect(m_singleInstance.get(), &GyanisScribe::App::SingleInstance::secondLaunchRequested, this,
                     [this]
                     {
                         GyanisScribe::Core::Logger::info("又有人启动智录：把设置窗口唤到前台，不再开第二份");
                         m_controller->openMainWindow(GyanisScribe::App::MainWindowPage::Settings);
                     });

    // 就绪判据只看日志有没有接管成功：界面在日志坏掉时照样能用，但 CI 的自检要把这一格判红
    m_isReady = GyanisScribe::Core::Logger::isInitialized();
}

Application::~Application()
{
    // 控制器先走：它负责注销热键、收掉遮罩；日志最后冲刷，才能把这些收尾也记下来
    m_controller.reset();
    GyanisScribe::Core::Logger::shutdown();
}

GyanisScribe::App::AppController &Application::controller() const noexcept
{
    return *m_controller;
}

bool Application::tryBecomePrimaryInstance()
{
    return m_singleInstance != nullptr && m_singleInstance->acquire();
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
