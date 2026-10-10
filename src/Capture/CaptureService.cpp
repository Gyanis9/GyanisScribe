#include "Capture/CaptureService.h"

#include "Capture/CaptureOverlay.h"
#include "Capture/IScreenCaptureSource.h"
#include "Capture/ScreenUtils.h"
#include "Core/Logger.h"
#include "Core/Text.h"
#include "Platform/IPlatformIntegration.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"

#include <QGuiApplication>
#include <QScreen>

#include <format>

namespace GyanisScribe::Capture
{

    CaptureService::CaptureService(const IScreenCaptureSource &source, Settings::ISettingsStore &settings, const Platform::IPlatformIntegration &integration, QObject *parent) :
        ICaptureService(parent), m_source(source), m_settings(settings), m_integration(integration)
    {
    }

    CaptureService::~CaptureService()
    {
        closeOverlays();
    }

    void CaptureService::captureFullScreen(const QString &screenName)
    {
        deliver(m_source.grabScreen(screenName));
    }

    void CaptureService::captureRegion(const QRect &logicalRegion, const QString &screenName)
    {
        const auto frame = m_source.grabScreen(screenName);
        if (!frame)
        {
            // 整帧都没抓到就谈不上裁剪：原因交回，别在这里补一个「按 0 处理」的形状
            deliver(frame);
            return;
        }

        const auto cropped = cropToLogicalRect(*frame, logicalRegion);
        if (!cropped)
        {
            deliver(cropped);
            return;
        }

        // 选区记账排在成功之后：失败也写就会把用户上次的好选区顶掉
        if (const auto stored = m_settings.setValue(Settings::kCaptureLastRegion, QVariant(regionToText(logicalRegion))); !stored)
        {
            Core::Logger::warning(std::format("上次截图区域没能记下：{}", stored.error().message));
        }
        if (const auto storedScreen = m_settings.setValue(Settings::kCaptureLastScreen, QVariant(frame->screenName)); !storedScreen)
        {
            Core::Logger::warning(std::format("上次截图所在屏没能记下：{}", storedScreen.error().message));
        }

        deliver(cropped);
    }

    void CaptureService::captureWindow(quintptr windowHandle)
    {
        deliver(m_source.grabWindow(windowHandle));
    }

    void CaptureService::captureLastRegion()
    {
        const auto storedText = m_settings.stringValue(Settings::kCaptureLastRegion, QString());
        if (storedText.isEmpty())
        {
            deliver(Core::Expected<Core::CapturedImage>(
                    std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("还没有可复用的上次区域：先框选截图一次，或改用区域截图")})));
            return;
        }

        const auto region = regionFromText(storedText.toStdString());
        if (!region)
        {
            deliver(Core::Expected<Core::CapturedImage>(std::unexpected(region.error())));
            return;
        }

        const auto screenName = m_settings.stringValue(Settings::kCaptureLastScreen, QString());
        captureRegion(*region, screenName);
    }

    void CaptureService::startInteractive()
    {
        closeOverlays();

        const auto screens = QGuiApplication::screens();
        if (screens.isEmpty())
        {
            deliver(Core::Expected<Core::CapturedImage>(
                    std::unexpected(Core::Error{Core::ErrorCode::ScreenUnavailable, GyanisScribe::Core::uiTextUtf8("进入框选失败：Qt 没有报出任何屏幕。请在 GUI 会话里重试")})));
            return;
        }

        // 候选要在这批遮罩建出来之前取：遮罩自己是一层盖住全屏的窗口，取晚了它会被列成第一个候选，
        // 于是每一格悬停都「认出整屏」，点一下就等于截全屏
        const auto candidates = windowCandidates();

        for (auto *screen: screens)
        {
            const auto frame = m_source.grabScreen(screen->name());
            if (!frame)
            {
                // 有一块屏抓不出来就别铺半套遮罩：用户会以为其余那块屏也是可用的
                deliver(frame);
                closeOverlays();
                return;
            }

            auto *overlay = new CaptureOverlay(frame->image, screen->geometry(), screen->devicePixelRatio());
            // 只留与这块屏有交集的那几扇，并裁到屏内：跨屏的窗口截到的就是它在屏上看得见的那部分
            std::vector<QRect> onThisScreen;
            for (const auto &rect: candidates)
            {
                if (const auto clipped = rect.intersected(screen->geometry()); !clipped.isNull() && clipped.width() > 0 && clipped.height() > 0)
                {
                    onThisScreen.push_back(clipped);
                }
            }
            overlay->setWindowCandidates(onThisScreen);
            QObject::connect(overlay, &CaptureOverlay::regionSelected, this,
                             [this, screen, overlay](const QRect &region)
                             {
                                 closeOverlays();
                                 captureRegion(region, screen->name());
                             });
            QObject::connect(overlay, &CaptureOverlay::dismissed, this, &CaptureService::cancelInteractive);
            m_overlays.push_back(QPointer<CaptureOverlay>(overlay));
            overlay->show();
            overlay->activateWindow();
        }
    }

    void CaptureService::cancelInteractive()
    {
        const auto hadOverlays = !m_overlays.empty();
        closeOverlays();
        if (hadOverlays)
        {
            emit captureCancelled();
        }
    }

    void CaptureService::deliver(const Core::Expected<Core::CapturedImage> &result)
    {
        if (result)
        {
            Core::Logger::info(std::format("截图完成：{}×{} 像素，逻辑 {}×{}，屏 {}", result->image.width(), result->image.height(), result->logicalSize.width(),
                                           result->logicalSize.height(), result->screenName.toStdString()));
            emit captured(*result);
            return;
        }

        Core::Logger::error(std::format("截图失败：{}", result.error().message));
        emit captureFailed(result.error());
    }

    void CaptureService::closeOverlays()
    {
        // 逐张收掉并清向量：遮罩是顶层窗口，留着不关就会一直压住屏幕
        for (auto &overlay: m_overlays)
        {
            if (overlay)
            {
                overlay->close();
                overlay->deleteLater();
            }
        }
        m_overlays.clear();
    }

    std::vector<QRect> CaptureService::windowCandidates() const
    {
        if (!m_integration.capabilities().windowEnumeration)
        {
            // 平台给不出候选就不是「少一个悬停认窗」，别的照常：这里不报错也不拦框选
            return {};
        }

        const auto windows = m_integration.screenWindows();
        if (!windows)
        {
            Core::Logger::warning(std::format("框选时没拿到窗口候选，这一轮只做手动框选：{}", windows.error().message));
            return {};
        }

        std::vector<QRect> rects;
        rects.reserve(windows->size());
        for (const auto &window: *windows)
        {
            rects.push_back(window.logicalGlobal);
        }
        return rects;
    }

} // namespace GyanisScribe::Capture
