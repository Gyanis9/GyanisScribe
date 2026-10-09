#include "App/Application.h"

#include "Core/Logger.h"
#include "Core/Paths.h"

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
    setApplicationDisplayName(QStringLiteral("智录"));
    setApplicationVersion(QStringLiteral(PROJECT_VERSION));

    const auto logDirectory = GyanisScribe::Core::Paths::logDirectory();
    if (const auto ensured = GyanisScribe::Core::Paths::ensureDirectory(logDirectory); !ensured)
    {
        // 目录都建不出来时初始化必然也失败，直接把原因走 Logger 的 stderr 兜底报出去
        GyanisScribe::Core::Logger::error(std::format("启动时日志不可用：{}", ensured.error().message));
        return;
    }
    if (const auto initialized = GyanisScribe::Core::Logger::initialize(logDirectory); !initialized)
    {
        GyanisScribe::Core::Logger::error(std::format("启动时日志初始化失败：{}", initialized.error().message));
        return;
    }
    GyanisScribe::Core::Logger::info(std::format("智录 {} 启动，日志目录 {}", applicationVersion().toStdString(), logDirectory.string()));
}

Application::~Application()
{
    // 对象析构即冲刷：静态库里的退出顺序不可控，把冲刷挂在唯一确定的持有者身上
    GyanisScribe::Core::Logger::shutdown();
}
