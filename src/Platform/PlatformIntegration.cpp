#include "Platform/IPlatformIntegration.h"

#include "Platform/Windows/WindowsIntegration.h"

#include <memory>

namespace GyanisScribe::Platform
{

    std::unique_ptr<IPlatformIntegration> createPlatformIntegration()
    {
        // 编译期选型而不是运行期试探：将来接别的平台时在这一层挂各自的实现，
        // 核心逻辑一旦偷偷依赖某一家的 API，会在这一层直接编不过，而不是到运行期才炸
        return std::make_unique<WindowsIntegration>();
    }

} // namespace GyanisScribe::Platform
