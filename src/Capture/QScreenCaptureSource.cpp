#include "Capture/CursorPixels.h"
#include "Capture/QScreenCaptureSource.h"

#include "Capture/ScreenUtils.h"
#include "Core/Logger.h"
#include "Core/Text.h"

#include <QApplication>
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QWindow>

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <format>
#include <vector>

namespace GyanisScribe::Capture
{
    namespace
    {
#ifdef Q_OS_WIN
        /// 指针边的可信上限：Windows 的指针最大约 128 物理像素，读到更大的值说明尺寸面读歪了
        constexpr int kMaximumCursorEdge = 256;

        /**
         * @brief GetIconInfo 交出来的两枚位图的收口处
         * @details 文档写明这两枚归调用方销毁；写成析构而不是在每条出口手动 DeleteObject，
         *          是因为这一层有五条提前返回，漏一条就是每次截图漏一枚 GDI 句柄。
         */
        struct IconInfoOwner
        {
            ICONINFO value{};

            IconInfoOwner()                                 = default;
            IconInfoOwner(const IconInfoOwner &)            = delete;
            IconInfoOwner &operator=(const IconInfoOwner &) = delete;

            ~IconInfoOwner()
            {
                if (value.hbmColor != nullptr)
                {
                    DeleteObject(value.hbmColor);
                }
                if (value.hbmMask != nullptr)
                {
                    DeleteObject(value.hbmMask);
                }
            }
        };

        std::optional<CursorSprite> windowsCursorSprite()
        {
            CURSORINFO cursor{};
            cursor.cbSize = static_cast<DWORD>(sizeof(cursor));
            // flags 只会是 CURSOR_SHOWING 或 CURSOR_HIDDEN：隐藏的指针不该进图
            if (!GetCursorInfo(&cursor) || cursor.flags != CURSOR_SHOWING || cursor.hCursor == nullptr)
            {
                return std::nullopt;
            }

            IconInfoOwner owner;
            if (!GetIconInfo(cursor.hCursor, &owner.value))
            {
                return std::nullopt;
            }

            // 单色指针没有 hbmColor，掩码那张是「AND 面 + XOR 面」上下叠起来的，高度要折半
            const bool isMono     = owner.value.hbmColor == nullptr;
            HBITMAP    sizeSource = isMono ? owner.value.hbmMask : owner.value.hbmColor;
            BITMAP     sizeBitmap{};
            if (!GetObjectW(sizeSource, static_cast<int>(sizeof(sizeBitmap)), &sizeBitmap) || sizeBitmap.bmWidth <= 0 || sizeBitmap.bmHeight <= 0)
            {
                return std::nullopt;
            }
            const int width  = sizeBitmap.bmWidth;
            const int height = isMono ? sizeBitmap.bmHeight / 2 : sizeBitmap.bmHeight;
            if (width <= 0 || height <= 0 || width > kMaximumCursorEdge || height > kMaximumCursorEdge)
            {
                Core::Logger::warning(std::format("鼠标指针的尺寸读出来是 {}×{}，超出可信范围，这次截图不含指针", width, height));
                return std::nullopt;
            }

            HDC screenDc = GetDC(nullptr);
            if (screenDc == nullptr)
            {
                return std::nullopt;
            }

            // 颜色面按 32 位取：GDI 会把 1/4/8/24 位的指针都换算过来，省去自己解调色板
            std::vector<QRgb> colorBits(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
            BITMAPINFO        colorInfo{};
            colorInfo.bmiHeader.biSize        = static_cast<DWORD>(sizeof(BITMAPINFOHEADER));
            colorInfo.bmiHeader.biWidth       = width;
            colorInfo.bmiHeader.biHeight      = -height; // 负数即自上而下，与 QImage 的行序一致
            colorInfo.bmiHeader.biPlanes      = 1;
            colorInfo.bmiHeader.biBitCount    = 32;
            colorInfo.bmiHeader.biCompression = BI_RGB;

            std::vector<unsigned char> maskBits;
            int                        maskRows = height;
            if (!isMono)
            {
                maskBits.assign(static_cast<std::size_t>(cursorMonoRowBytes(width)) * static_cast<std::size_t>(height), 0U);
            } else
            {
                // 两平面各 height 行，一次读全：AND 面在前，XOR 面紧随其后
                maskRows = height * 2;
                maskBits.assign(static_cast<std::size_t>(cursorMonoRowBytes(width)) * static_cast<std::size_t>(maskRows), 0U);
            }

            BITMAPINFO maskInfo{};
            maskInfo.bmiHeader.biSize        = static_cast<DWORD>(sizeof(BITMAPINFOHEADER));
            maskInfo.bmiHeader.biWidth       = width;
            maskInfo.bmiHeader.biHeight      = -maskRows;
            maskInfo.bmiHeader.biPlanes      = 1;
            maskInfo.bmiHeader.biBitCount    = 1;
            maskInfo.bmiHeader.biCompression = BI_RGB;

            const bool hasColorPlane = !isMono && GetDIBits(screenDc, owner.value.hbmColor, 0, height, colorBits.data(), &colorInfo, DIB_RGB_COLORS) == height;
            const bool hasMaskPlane  = GetDIBits(screenDc, owner.value.hbmMask, 0, maskRows, maskBits.data(), &maskInfo, DIB_RGB_COLORS) == maskRows;
            ReleaseDC(nullptr, screenDc);

            if (!hasColorPlane && !isMono)
            {
                // 颜色面读不出来时不能只靠掩码画个空壳：整枚指针放弃，让上层照常出图
                Core::Logger::warning(std::format("鼠标指针的颜色面读不出来（{}×{}），这次截图不含指针", width, height));
                return std::nullopt;
            }
            if (!hasMaskPlane)
            {
                Core::Logger::warning(std::format("鼠标指针的掩码面读不出来（{}×{}），这次截图不含指针", width, height));
                return std::nullopt;
            }

            CursorSprite sprite;
            // 三种指针（带 alpha、老式彩色、单色）的位平面合成规则在 CursorPixels 里，
            // 那一层不碰 GDI，容器里也跑得动用例
            sprite.image   = composeCursorImage(colorBits, maskBits, width, height, isMono);
            sprite.hotspot = QPoint{static_cast<int>(owner.value.xHotspot), static_cast<int>(owner.value.yHotspot)};
            if (sprite.image.isNull())
            {
                return std::nullopt;
            }
            return sprite;
        }
#endif

        // 一帧的来源屏幕：光标落在哪块就用哪块的缩放因子，抓窗口时也按它所属的那块算
        QScreen *screenForCursor()
        {
            if (auto *screen = screenAtGlobalPoint(QCursor::pos()); screen != nullptr)
            {
                return screen;
            }
            return QGuiApplication::primaryScreen();
        }

        // 抓帧与取指针落点共用这一处「按名认屏」：两条路认的是同一块屏，落点换算才不会错位
        QScreen *screenForGrab(const QString &screenName)
        {
            return screenName.isEmpty() ? screenForCursor() : screenByName(screenName);
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
        QScreen *target = screenForGrab(screenName);
        if (target == nullptr)
        {
            if (!screenName.isEmpty())
            {
                return std::unexpected(Core::Error{
                        Core::ErrorCode::ScreenUnavailable,
                        GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("找不到名为「{}」的屏幕：多半已经被拔掉。请在不填屏幕名的情况下重抓一次（那样按光标所在屏走）"),
                                                     screenName.toStdString())});
            }
            return std::unexpected(Core::Error{Core::ErrorCode::ScreenUnavailable, GyanisScribe::Core::uiTextUtf8("Qt 没有报出任何屏幕：请在 QGuiApplication 建好之后再抓图")});
        }

        // 先转成目标指针以外的量：grabWindow 里 Qt 自己会再取一次屏幕几何
        const QImage grabbed = target->grabWindow(0).toImage();
        if (grabbed.isNull() || grabbed.width() <= 0 || grabbed.height() <= 0)
        {
            return std::unexpected(
                    Core::Error{Core::ErrorCode::ScreenUnavailable,
                                GyanisScribe::Core::uiFormat(
                                        GyanisScribe::Core::uiTextUtf8("抓「{}」这块屏拿到了空图：本平台可能不允许直接抓取屏幕。请检查会话类型（远程会话、Wayland 沙箱）后重试"),
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
                    return std::unexpected(
                            Core::Error{Core::ErrorCode::PlatformUnsupported,
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
                                GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("抓不到句柄 {} 对应的窗口：本平台不允许抓取他人窗口，或窗口已关闭。请改用区域截图"),
                                                             windowHandle)});
        }

        const auto  dpr = target->devicePixelRatio();
        const QSize logicalSize(qRound(static_cast<double>(grabbed.width()) / dpr), qRound(static_cast<double>(grabbed.height()) / dpr));
        Core::Logger::info(std::format("按外部窗口抓取：句柄 {}，像素 {}×{}，按 dpr {} 回算逻辑尺寸 {}×{}", windowHandle, grabbed.width(), grabbed.height(), dpr,
                                       logicalSize.width(), logicalSize.height()));
        return assembleImage(grabbed, target, logicalSize);
    }

    std::optional<CursorSprite> QScreenCaptureSource::grabCursor(const QString &screenName) const
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
        // QCursor::pos() 给的是全局逻辑坐标，减掉该屏的逻辑原点才是 CursorSprite 要的那一格
        sprite->position = QCursor::pos() - screen->geometry().topLeft();
        return sprite;
#else
        // 其余平台没有公开入口能取到系统正在画的那枚指针：交回空值，
        // 由编排层在日志里说明「这次不含指针」，不自己画一枚凭空的箭头
        Q_UNUSED(screenName);
        return std::nullopt;
#endif
    }

} // namespace GyanisScribe::Capture
