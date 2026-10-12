#include "Capture/CaptureService.h"

#include "Capture/CaptureOverlay.h"
#include "Capture/FrameStitch.h"
#include "Capture/IScreenCaptureSource.h"
#include "Capture/LongCaptureHud.h"
#include "Capture/RegionHistory.h"
#include "Capture/ScreenUtils.h"
#include "Core/Logger.h"
#include "Core/Text.h"
#include "Platform/IPlatformIntegration.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"

#include <QGuiApplication>
#include <QScreen>

#include <cmath>
#include <format>

namespace
{
    /// 滚动长截图的取帧节拍，毫秒：比人手滚一格快，又不至于每帧都在同一位置上
    constexpr int kLongCaptureIntervalMs = 250;

    /// 长图的逻辑高度上限：接到这里就地收口，免得在一条没有底的页面上把内存吃光
    constexpr int kLongCaptureMaximumLogicalHeight = 40000;
} // namespace

namespace GyanisScribe::Capture
{

    CaptureService::CaptureService(const IScreenCaptureSource &source, Settings::ISettingsStore &settings, const Platform::IPlatformIntegration &integration, QObject *parent) :
        ICaptureService(parent), m_source(source), m_settings(settings), m_integration(integration)
    {
    }

    CaptureService::~CaptureService()
    {
        closeOverlays();
        // 析构时还在接：计时器与控制条一起收，别留下一个每 250ms 抓一次屏的孤儿
        m_longTimer.stop();
        closeLongHud();
    }

    void CaptureService::captureFullScreen(const QString &screenName)
    {
        deliver(withCursorIfWanted(m_source.grabScreen(screenName)));
    }

    void CaptureService::captureRegion(const QRect &logicalRegion, const QString &screenName)
    {
        const auto frame = grabRegionNow(logicalRegion, screenName);
        if (!frame)
        {
            // 抓帧或裁剪失败：原因原样交回，别在这里补一个「按 0 处理」的形状
            deliver(frame);
            return;
        }

        // 选区记账排在成功之后：失败也写就会把用户上次的好选区顶掉
        rememberRegion(m_settings, RegionHistoryEntry{logicalRegion, frame->screenName});

        deliver(frame);
    }

    Core::Expected<Core::CapturedImage> CaptureService::grabRegionNow(const QRect &logicalRegion, const QString &screenName)
    {
        // 指针先画进整帧再裁：选区里带不带指针由选区自己决定，不必裁完再来一次坐标换算
        const auto frame = withCursorIfWanted(m_source.grabScreen(screenName));
        if (!frame)
        {
            // 整帧都没抓到就谈不上裁剪：原因交回
            return std::unexpected(frame.error());
        }

        return cropToLogicalRect(*frame, logicalRegion);
    }

    void CaptureService::captureWindow(quintptr windowHandle)
    {
        // 这一路刻意不合成指针：形状要按窗口左上角换算落点，而抓别人的窗口时那份逻辑几何拿不齐
        deliver(m_source.grabWindow(windowHandle));
    }

    void CaptureService::captureLastRegion()
    {
        const auto history = loadRegionHistory(m_settings);
        if (!history)
        {
            deliver(Core::Expected<Core::CapturedImage>(std::unexpected(history.error())));
            return;
        }
        if (history->empty())
        {
            deliver(Core::Expected<Core::CapturedImage>(
                    std::unexpected(Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("还没有可复用的上次区域：先框选截图一次，或改用区域截图")})));
            return;
        }

        // 「上次区域」取最近那一条：历史里其余几条留给框选态的 R/Shift+R 循环挑
        captureRegion(history->front().region, history->front().screenName);
    }

    void CaptureService::startInteractive()
    {
        beginInteractive(false);
    }

    void CaptureService::startLongCapture()
    {
        beginInteractive(true);
    }

    void CaptureService::beginInteractive(const bool longCapture)
    {
        if (m_longActive)
        {
            // 上一轮长截图还在接：再铺一层遮罩会让手里那张接了一半的图既没交也没作废。
            // 这一句把选择权交回给用户——完成或取消，再开始下一次
            deliver(Core::Expected<Core::CapturedImage>(std::unexpected(
                    Core::Error{Core::ErrorCode::InvalidArgument, GyanisScribe::Core::uiTextUtf8("滚动长截图还在接：先在控制条上按完成或取消，再开始下一次截图")})));
            return;
        }

        closeOverlays();

        // 这一轮的悬停记账到期重来：上一轮问过的点与报过的失败理由都不该带进新一轮
        m_lastElementQuery.reset();
        m_elementFailureReported = false;

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

        // 历史选区按屏分：遮罩铺在哪块屏，就只把那块屏的几条交给它——
        // R/Shift+R 循环挑的是同屏的几块，别屏的矩形放上来会露出一块越界的框
        auto regionHistory = loadRegionHistory(m_settings);
        if (!regionHistory)
        {
            Core::Logger::warning(std::format("选区历史读不出，这一轮的 R/Shift+R 没有可挑的：{}", regionHistory.error().message));
            regionHistory = std::vector<RegionHistoryEntry>{};
        }

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
            // 只给同屏的几条：R/Shift+R 循环挑的是这块屏上的几块，别屏的矩形放上来会露出一块越界的框
            overlay->setRegionHistory(regionsOnScreen(*regionHistory, screen->name()));
            QObject::connect(overlay, &CaptureOverlay::regionSelected, this,
                             [this, screen, overlay, longCapture](const QRect &region)
                             {
                                 closeOverlays();
                                 // 同一层遮罩两条去处：长截图先把这一块当成开头接住，普通截图当场交成品
                                 if (longCapture)
                                 {
                                     captureLongRegion(region, screen->name());
                                     return;
                                 }
                                 captureRegion(region, screen->name());
                             });
            QObject::connect(overlay, &CaptureOverlay::dismissed, this, &CaptureService::cancelInteractive);
            // 悬停细分到界面元素：平台没这能力就不接这条线，遮罩照旧只认窗口候选
            if (m_integration.capabilities().uiElements)
            {
                QObject::connect(overlay, &CaptureOverlay::hoverPointChanged, this, [this, overlay](const QPoint &globalPoint) { queryElementChain(overlay, globalPoint); });
            }
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

    Core::Expected<Core::CapturedImage> CaptureService::withCursorIfWanted(const Core::Expected<Core::CapturedImage> &frame) const
    {
        if (!frame || !m_settings.boolValue(Settings::kCaptureIncludeCursor, Settings::kDefaultCaptureIncludeCursor))
        {
            return frame;
        }

        // 屏名沿用这一帧自己报回来的那一个：来源按光标选屏时也会把真名写回帧里，两边认的是同一块屏
        const auto sprite = m_source.grabCursor(frame->screenName);
        if (!sprite || sprite->image.isNull())
        {
            // 平台给不出形状（指针隐藏、非 Windows 后端）：说明白这次不含指针，别按「含了」交下去
            Core::Logger::warning(std::format("设置里要含鼠标指针，但这次取不到指针的形状，交出去的图里没有指针"));
            return frame;
        }

        auto result = *frame;
        drawCursorInto(result, *sprite);
        return result;
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

    void CaptureService::captureLongRegion(const QRect &logicalRegion, const QString &screenName)
    {
        if (m_longActive)
        {
            // 上一轮还开着：先按「用户作废」收掉，让那一次请求也有个可观察的结果，再开这一轮
            cancelLongCapture();
        }

        const auto frame = m_source.grabScreen(screenName);
        if (!frame)
        {
            deliver(frame);
            return;
        }

        const auto first = cropToLogicalRect(*frame, logicalRegion);
        if (!first)
        {
            deliver(first);
            return;
        }

        m_longImage      = *first;
        m_longRegion     = logicalRegion;
        m_longScreenName = frame->screenName;
        m_longActive     = true;

        // 选区同样记进「上次区域」：长截图框出来的那条竖块往往正是下次要复用的那一块
        rememberRegion(m_settings, RegionHistoryEntry{logicalRegion, m_longScreenName});

        auto *hud = new LongCaptureHud{};
        QObject::connect(hud, &LongCaptureHud::finished, this, &CaptureService::finishLongCapture);
        QObject::connect(hud, &LongCaptureHud::cancelled, this, &CaptureService::cancelLongCapture);
        m_longHud = QPointer<LongCaptureHud>(hud);
        hud->show();
        placeLongHud();
        hud->setReadout(longReadout(true));

        QObject::connect(&m_longTimer, &QTimer::timeout, this, &CaptureService::grabNextLongFrame);
        m_longTimer.start(kLongCaptureIntervalMs);
    }

    void CaptureService::grabNextLongFrame()
    {
        if (!m_longActive)
        {
            return;
        }

        const auto frame = m_source.grabScreen(m_longScreenName);
        if (!frame)
        {
            stopLongRound();
            deliver(frame);
            return;
        }

        const auto cropped = cropToLogicalRect(*frame, m_longRegion);
        if (!cropped)
        {
            stopLongRound();
            deliver(cropped);
            return;
        }

        const auto appended = appendFrame(m_longImage.image, cropped->image);
        if (!appended)
        {
            stopLongRound();
            deliver(Core::Expected<Core::CapturedImage>(std::unexpected(appended.error())));
            return;
        }

        const auto grew   = appended->height() > m_longImage.image.height();
        m_longImage.image = *appended;
        // 逻辑高度按缩放因子反算：宽没变，高是像素除以比例；向下取整会让读数比实际少一格
        m_longImage.logicalSize = QSize{m_longImage.logicalSize.width(), static_cast<int>(std::lround(m_longImage.image.height() / m_longImage.devicePixelRatio))};

        if (m_longHud)
        {
            m_longHud->setReadout(longReadout(grew));
        }
        if (m_longImage.logicalSize.height() >= kLongCaptureMaximumLogicalHeight)
        {
            Core::Logger::warning(std::format("长截图接到逻辑高度 {} 的上限，就地收口", m_longImage.logicalSize.height()));
            finishLongCapture();
        }
    }

    void CaptureService::finishLongCapture()
    {
        if (!m_longActive)
        {
            return;
        }
        m_longActive = false;
        stopLongRound();

        // 交出去的形状与一次普通截图完全一样：后续去向不需要知道这是接出来的
        deliver(Core::Expected<Core::CapturedImage>(m_longImage));
        m_longImage = {};
    }

    void CaptureService::cancelLongCapture()
    {
        if (!m_longActive)
        {
            return;
        }
        m_longActive = false;
        stopLongRound();
        m_longImage = {};
        emit captureCancelled();
    }

    void CaptureService::stopLongRound()
    {
        m_longTimer.stop();
        closeLongHud();
        // 三条出口（完成、取消、接不上）都收在这里：这一位不翻，之后的每一次调用还会去抓一帧
        m_longActive = false;
    }

    void CaptureService::closeLongHud()
    {
        if (m_longHud)
        {
            m_longHud->close();
            m_longHud->deleteLater();
        }
    }

    void CaptureService::placeLongHud()
    {
        if (m_longHud == nullptr)
        {
            return;
        }

        const QScreen *screen = nullptr;
        for (auto *candidate: QGuiApplication::screens())
        {
            if (candidate->name() == m_longScreenName)
            {
                screen = candidate;
                break;
            }
        }
        if (screen == nullptr)
        {
            // 名字对不上（多半是这一轮之间拔了屏）：摆不了位也要能收口，控制条留在原地由用户关掉
            Core::Logger::warning(std::format("长截图没找到那块屏 {}，控制条摆在原处", m_longScreenName.toStdString()));
            return;
        }

        const auto regionGlobal = m_longRegion.translated(screen->geometry().topLeft());
        m_longHud->setGeometry(LongCaptureHud::positionFor(regionGlobal, screen->availableGeometry(), m_longHud->preferredHudSize()));
    }

    QString CaptureService::longReadout(const bool grew) const
    {
        const auto height = m_longImage.logicalSize.height();
        if (!grew)
        {
            return QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("已接 {} 像素高：这一段和上一段一样，往下滚一点再接"), height).c_str());
        }
        return QString::fromUtf8(GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("已接 {} 像素高"), height).c_str());
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

    void CaptureService::queryElementChain(CaptureOverlay *overlay, const QPoint &globalLogicalPoint)
    {
        // 光标可以连着两次落在同一格上（进遮罩的那一下、按下前的微动），同一格不重复问：
        // 每次问都是一串跨进程调用，答案还一模一样
        if (m_lastElementQuery && *m_lastElementQuery == globalLogicalPoint)
        {
            return;
        }
        m_lastElementQuery = globalLogicalPoint;

        const auto chain = m_integration.elementChainAtPoint(globalLogicalPoint, overlayHandles());
        if (chain)
        {
            overlay->setElementChain(*chain);
            return;
        }

        // 问不出来就退回只认窗口，别把上一次的链留在屏幕上装作还认得：那个框已经不对应光标下的东西了
        overlay->setElementChain({});
        if (m_elementFailureReported)
        {
            return;
        }
        m_elementFailureReported = true;
        Core::Logger::warning(std::format("认不出光标下的界面元素，这一轮退回只认整扇窗口：{}", chain.error().message));
    }

    std::vector<quintptr> CaptureService::overlayHandles() const
    {
        std::vector<quintptr> handles;
        handles.reserve(m_overlays.size());
        for (const auto &overlay: m_overlays)
        {
            if (overlay)
            {
                // winId() 对已 show 的窗口不会新建原生窗口，只是把句柄取出来
                handles.push_back(static_cast<quintptr>(overlay->winId()));
            }
        }
        return handles;
    }

} // namespace GyanisScribe::Capture
