#include "Platform/IPlatformIntegration.h"

#include <memory>

#ifdef Q_OS_WIN
#include "Platform/Windows/WindowsIntegration.h"
#else
#include "Platform/Linux/LinuxIntegration.h"
#endif

namespace GyanisScribe::Platform
{

    std::unique_ptr<IPlatformIntegration> createPlatformIntegration()
    {
        // 编译期选型而不是运行期试探：容器里这条分支编的就是 Linux 实现，
        // 核心逻辑一旦偷偷依赖 Windows API，会在这一层直接编不过，而不是到运行期才炸
#ifdef Q_OS_WIN
        return std::make_unique<WindowsIntegration>();
#else
        return std::make_unique<LinuxIntegration>();
#endif
    }

} // namespace GyanisScribe::Platform
