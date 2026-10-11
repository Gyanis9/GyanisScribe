#include "Capture/ScreenCaptureSourceFactory.h"

#include "Capture/QScreenCaptureSource.h"
#include "Core/Logger.h"

#ifdef Q_OS_WIN
#include "Capture/DxgiCaptureSource.h"
#endif

namespace GyanisScribe::Capture
{
    std::unique_ptr<IScreenCaptureSource> createScreenCaptureSource()
    {
#ifdef Q_OS_WIN
        // 先试 DXGI：它是抓帧那一格的中位耗时从 34ms 量级降到毫秒级的关键（README 性能实测）。
        // 建不起来不是错误——远程会话、老驱动、非 Windows 都走不到这一档，回落到 Qt 就好
        if (auto dxgi = std::make_unique<DxgiCaptureSource>(); dxgi->isUsable())
        {
            Core::Logger::info("采集原语：DXGI 桌面重复");
            return dxgi;
        }
        Core::Logger::warning("DXGI 桌面重复不可用（D3D11 设备建不起来），本次回落 Qt 的抓帧原语");
#endif
        Core::Logger::info("采集原语：Qt grabWindow");
        return std::make_unique<QScreenCaptureSource>();
    }

} // namespace GyanisScribe::Capture
