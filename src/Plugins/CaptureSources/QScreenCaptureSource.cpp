#include "Core/Capture/CursorPixels.h"
#include "Plugins/CaptureSources/QScreenCaptureSource.h"

#include "Core/Capture/ScreenUtils.h"
#include "Base/Logger.h"
#include "Base/Text.h"

#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QWindow>

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <format>

namespace GyanisScribe::Plugins
{
    namespace
    {
#ifdef Q_OS_WIN
        std::optional<Core::CursorSprite> windowsCursorSprite()
        {
            CURSORINFO cursor{};
            cursor.cbSize = static_cast<DWORD>(sizeof(cursor));
            // flags 只会是 CURSOR_SHOWING 或 CURSOR_HIDDEN：隐藏的指针不该进图
            if (!GetCursorInfo(&cursor) || cursor.flags != CURSOR_SHOWING || cursor.hCursor == nullptr)
            {
                return std::nullopt;
            }
            // 两张位平面的解法在 CursorPixels 里：那一层能喂进自造的指针，用例验得到
            return Core::cursorSpriteFromHandle(reinterpret_cast<quintptr>(cursor.hCursor));
        }
#endif

        // 一帧的来源屏幕：光标落在哪块就用哪块的缩放因子，抓窗口时也按它所属的那块算
        QScreen *screenForCursor()
        {
            if (auto *screen = Core::screenAtGlobalPoint(QCursor::pos()); screen != nullptr)
            {
                return screen;
            }
            return QGuiApplication::primaryScreen();
        }

        // 抓帧与取指针落点共用这一处「按名认屏」：两条路认的是同一块屏，落点换算才不会错位
        QScreen *screenForGrab(const QString &screenName)
        {
            return screenName.isEmpty() ? screenForCursor() : Core::screenByName(screenName);
        }

        // 帧的组装（逻辑尺寸、缩放因子、屏名）在 ScreenUtils 那一处：DXGI 那一份来源也要
        // 装成同一形状，两处各写一份时缩放因子标错只在特定屏上现形
    } // namespace

    Base::Expected<Base::CapturedImage> QScreenCaptureSource::grabScreen(const QString &screenName) const
    {
        QScreen *target = screenForGrab(screenName);
        if (target == nullptr)
        {
            if (!screenName.isEmpty())
            {
                return std::unexpected(Base::Error{
                        Base::ErrorCode::ScreenUnavailable,
                        GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("找不到名为「{}」的屏幕：多半已经被拔掉。请在不填屏幕名的情况下重抓一次（那样按光标所在屏走）"),
                                                     screenName.toStdString())});
            }
            return std::unexpected(Base::Error{Base::ErrorCode::ScreenUnavailable, GyanisScribe::Base::uiTextUtf8("Qt 没有报出任何屏幕：请在 QGuiApplication 建好之后再抓图")});
        }

        // 先转成目标指针以外的量：grabWindow 里 Qt 自己会再取一次屏幕几何
        const QImage grabbed = target->grabWindow(0).toImage();
        if (grabbed.isNull() || grabbed.width() <= 0 || grabbed.height() <= 0)
        {
            return std::unexpected(
                    Base::Error{Base::ErrorCode::ScreenUnavailable,
                                GyanisScribe::Base::uiFormat(
                                        GyanisScribe::Base::uiTextUtf8("抓「{}」这块屏拿到了空图：本平台可能不允许直接抓取屏幕。请检查会话类型（远程会话、Wayland 沙箱）后重试"),
                                        target->name().toStdString())});
        }

        return Core::frameFromGrabbedImage(grabbed, target, target->geometry().size());
    }

    Base::Expected<Base::CapturedImage> QScreenCaptureSource::grabWindow(quintptr windowHandle) const
    {
        if (windowHandle == 0)
        {
            return std::unexpected(
                    Base::Error{Base::ErrorCode::InvalidArgument, GyanisScribe::Base::uiTextUtf8("抓窗口失败：传进来的窗口句柄是空的。请确认窗口还没有被关闭，或改用区域截图")});
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
                    return std::unexpected(
                            Base::Error{Base::ErrorCode::PlatformUnsupported,
                                        GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("本平台抓不出窗口像素（句柄 {}）。请改用区域截图框住那个窗口"), windowHandle)});
                }
                return Core::frameFromGrabbedImage(grabbed, target, window->geometry().size());
            }
        }

        // 别的进程的窗口：拿不到它的逻辑几何，只能按抓回来的像素除以缩放因子回算
        auto        *target  = screenForCursor();
        const QImage grabbed = target != nullptr ? target->grabWindow(windowHandle).toImage() : QImage();
        if (grabbed.isNull() || grabbed.width() <= 0)
        {
            return std::unexpected(
                    Base::Error{Base::ErrorCode::PlatformUnsupported,
                                GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("抓不到句柄 {} 对应的窗口：本平台不允许抓取他人窗口，或窗口已关闭。请改用区域截图"),
                                                             windowHandle)});
        }

        const auto  dpr = target->devicePixelRatio();
        const QSize logicalSize(qRound(static_cast<double>(grabbed.width()) / dpr), qRound(static_cast<double>(grabbed.height()) / dpr));
        Base::Logger::info(std::format("按外部窗口抓取：句柄 {}，像素 {}×{}，按 dpr {} 回算逻辑尺寸 {}×{}", windowHandle, grabbed.width(), grabbed.height(), dpr,
                                       logicalSize.width(), logicalSize.height()));
        return Core::frameFromGrabbedImage(grabbed, target, logicalSize);
    }

    std::optional<Core::CursorSprite> QScreenCaptureSource::grabCursor(const QString &screenName) const
    {
#ifdef Q_OS_WIN
        auto sprite = windowsCursorSprite();
        if (!sprite)
        {
            return std::nullopt;
        }

        // 落点与抓帧认同一块屏：用同一个解析函数，两处换算才会互指同一个点
        const auto *screen = screenForGrab(screenName);
        if (screen == nullptr)
        {
            return std::nullopt;
        }
        // QCursor::pos() 给的是全局逻辑坐标，减掉该屏的逻辑原点才是 Core::CursorSprite 要的那一格
        sprite->position = QCursor::pos() - screen->geometry().topLeft();
        return sprite;
#else
        // 其余平台没有公开入口能取到系统正在画的那枚指针：交回空值，
        // 由编排层在日志里说明「这次不含指针」，不自己画一枚凭空的箭头
        Q_UNUSED(screenName);
        return std::nullopt;
#endif
    }

} // namespace GyanisScribe::Plugins
