#include "Plugins/CaptureSources/ScreenCaptureSourceFactory.h"

#include "Plugins/CaptureSources/QScreenCaptureSource.h"
#include "Base/Logger.h"

#ifdef Q_OS_WIN
#include "Plugins/CaptureSources/DxgiCaptureSource.h"
#endif

namespace GyanisScribe::Plugins
{
    std::unique_ptr<Core::IScreenCaptureSource> createScreenCaptureSource()
    {
#ifdef Q_OS_WIN
        // 先试 DXGI：它是抓帧那一格的中位耗时从 34ms 量级降到毫秒级的关键（README 性能实测）。
        // 建不起来不是错误——远程会话、老驱动、非 Windows 都走不到这一档，回落到 Qt 就好
        if (auto dxgi = std::make_unique<DxgiCaptureSource>(); dxgi->isUsable())
        {
            Base::Logger::info("采集原语：DXGI 桌面重复");
            return dxgi;
        }
        Base::Logger::warning("DXGI 桌面重复不可用（D3D11 设备建不起来），本次回落 Qt 的抓帧原语");
#endif
        Base::Logger::info("采集原语：Qt grabWindow");
        return std::make_unique<QScreenCaptureSource>();
    }

} // namespace GyanisScribe::Plugins
