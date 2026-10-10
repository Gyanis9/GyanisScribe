#include "Capture/QScreenCaptureSource.h"

#include "Capture/ScreenUtils.h"
#include "Core/Logger.h"
#include "Core/Text.h"

#include <QApplication>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QWindow>

#include <format>

namespace GyanisScribe::Capture
{
    namespace
    {
        // 一帧的来源屏幕：光标落在哪块就用哪块的缩放因子，抓窗口时也按它所属的那块算
        QScreen *screenForCursor()
        {
            if (auto *screen = screenAtGlobalPoint(QCursor::pos()); screen != nullptr)
            {
                return screen;
            }
            return QGuiApplication::primaryScreen();
        }

        Core::CapturedImage assembleImage(const QImage &grabbed, const QScreen *screen, const QSize &logicalSize)
        {
            Core::CapturedImage image;
            image.image = grabbed.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            // 抓回来的图按物理像素交回：Qt 有时会把 dpr 标在图上，这里统一清成 1，
            // 免得下游既乘一次又除一次
            image.image.setDevicePixelRatio(1.0);
            image.devicePixelRatio = screen != nullptr ? screen->devicePixelRatio() : 1.0;
            image.logicalSize      = logicalSize;
            image.screenName       = screen != nullptr ? screen->name() : QString();
            return image;
        }
    } // namespace

    Core::Expected<Core::CapturedImage> QScreenCaptureSource::grabScreen(const QString &screenName) const
    {
        QScreen *target = nullptr;
        if (screenName.isEmpty())
        {
            target = screenForCursor();
        } else
        {
            target = screenByName(screenName);
            if (target == nullptr)
            {
                return std::unexpected(
                        Core::Error{Core::ErrorCode::ScreenUnavailable,
                                    GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("找不到名为「{}」的屏幕：多半已经被拔掉。请在不填屏幕名的情况下重抓一次（那样按光标所在屏走）"),
                                                screenName.toStdString())});
            }
        }

        if (target == nullptr)
        {
            return std::unexpected(Core::Error{Core::ErrorCode::ScreenUnavailable, GyanisScribe::Core::uiTextUtf8("Qt 没有报出任何屏幕：请在 QGuiApplication 建好之后再抓图")});
        }

        // 先转成目标指针以外的量：grabWindow 里 Qt 自己会再取一次屏幕几何
        const QImage grabbed = target->grabWindow(0).toImage();
        if (grabbed.isNull() || grabbed.width() <= 0 || grabbed.height() <= 0)
        {
            return std::unexpected(Core::Error{
                    Core::ErrorCode::ScreenUnavailable,
                    GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("抓「{}」这块屏拿到了空图：本平台可能不允许直接抓取屏幕。请检查会话类型（远程会话、Wayland 沙箱）后重试"),
                                target->name().toStdString())});
        }

        return assembleImage(grabbed, target, target->geometry().size());
    }

    Core::Expected<Core::CapturedImage> QScreenCaptureSource::grabWindow(quintptr windowHandle) const
    {
        if (windowHandle == 0)
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("抓窗口失败：传进来的窗口句柄是空的。请确认窗口还没有被关闭，或改用区域截图")});
        }

        // 本进程的顶层窗口里有它自己的逻辑几何与所属屏幕，用它算最准
        const auto topLevelWindows = QGuiApplication::topLevelWindows();
        for (auto *window: topLevelWindows)
        {
            if (window != nullptr && window->winId() == windowHandle)
            {
                QScreen     *target  = window->screen() != nullptr ? window->screen() : screenForCursor();
                const QImage grabbed = target->grabWindow(windowHandle).toImage();
                if (grabbed.isNull() || grabbed.width() <= 0)
                {
                    return std::unexpected(Core::Error{Core::ErrorCode::PlatformUnsupported,
                                                       GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("本平台抓不出窗口像素（句柄 {}）。请改用区域截图框住那个窗口"), windowHandle)});
                }
                return assembleImage(grabbed, target, window->geometry().size());
            }
        }

        // 别的进程的窗口：拿不到它的逻辑几何，只能按抓回来的像素除以缩放因子回算
        auto        *target  = screenForCursor();
        const QImage grabbed = target != nullptr ? target->grabWindow(windowHandle).toImage() : QImage();
        if (grabbed.isNull() || grabbed.width() <= 0)
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::PlatformUnsupported,
                                GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("抓不到句柄 {} 对应的窗口：本平台不允许抓取他人窗口，或窗口已关闭。请改用区域截图"), windowHandle)});
        }

        const auto  dpr = target->devicePixelRatio();
        const QSize logicalSize(qRound(static_cast<double>(grabbed.width()) / dpr), qRound(static_cast<double>(grabbed.height()) / dpr));
        Core::Logger::info(std::format("按外部窗口抓取：句柄 {}，像素 {}×{}，按 dpr {} 回算逻辑尺寸 {}×{}", windowHandle, grabbed.width(), grabbed.height(), dpr,
                                       logicalSize.width(), logicalSize.height()));
        return assembleImage(grabbed, target, logicalSize);
    }

} // namespace GyanisScribe::Capture
