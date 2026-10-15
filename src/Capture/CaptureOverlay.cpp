#include "Capture/CaptureOverlay.h"

#include "Capture/RegionHistory.h"
#include "Capture/ScreenUtils.h"
#include "Base/SizeText.h"
#include "Base/Logger.h"
#include "Base/Text.h"
#include "Pick/ColorFormat.h"
#include "Pick/PickGeometry.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Gui/Controls/AppShortcuts.h"
#include "Gui/Theme/DesignTokens.h"
#include "Gui/Controls/KeyStep.h"

#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>

#include <QClipboard>
#include <QGuiApplication>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <string_view>

namespace
{
    /// 放大镜边框线宽，逻辑像素：与取色遮罩同一档，两处放大镜不能看起来是两个东西
    constexpr int kMagnifierPenWidth = 2;

    /**
     * @brief 遮罩上可换键的那一档
     * @details 标识取自 Gui/Controls/AppShortcuts.h 那份清册：那颗键现在认给谁，界面上与按键上都由清册说了算。
     *          表里的顺序不参与判定——同一族的默认那颗互不相同（这一判由 TestAppShortcuts 钉住）。
     */
    struct RebindableRow
    {
        std::string_view                           token;                     ///< 清册里的标识
        GyanisScribe::Capture::RebindableKeyAction action;                    ///< 命中之后要走哪个动作
        bool                                       allowShiftVariant = false; ///< 认不认「同一颗键多按一个 Shift」那一档
    };

    constexpr std::array<RebindableRow, 6> kRebindableRows = {
            RebindableRow{"Mask/AspectRatio", GyanisScribe::Capture::RebindableKeyAction::ToggleAspectRatio, false},
            RebindableRow{"Mask/SizeEditor", GyanisScribe::Capture::RebindableKeyAction::OpenSizeEditor, false},
            RebindableRow{"Mask/RegionCycle", GyanisScribe::Capture::RebindableKeyAction::CycleRegionHistory, true},
            RebindableRow{"Mask/CopySize", GyanisScribe::Capture::RebindableKeyAction::CopySelectedSize, false},
            RebindableRow{"Mask/ColorFormat", GyanisScribe::Capture::RebindableKeyAction::CycleColorFormat, false},
            RebindableRow{"Mask/PointerToggle", GyanisScribe::Capture::RebindableKeyAction::ToggleCursor, false},
    };

    /// 放大镜下面那条读数条的高度，逻辑像素
    constexpr int kMagnifierStripHeight = 24;

    /// 「已复制尺寸」提示与读数框之间的间距，逻辑像素
    constexpr int kCopiedHintGap = 4;

    /// 尺寸输入框的宽度，逻辑像素：装得下「1920x1080」这样的读数
    constexpr int kSizeEditorWidth = 148;

    /// 尺寸输入框摆在光标右下时的偏移，逻辑像素
    constexpr int kSizeEditorCursorGap = 12;
} // namespace

namespace GyanisScribe::Capture
{

    CaptureOverlay::CaptureOverlay(const QImage &background, const QRect &screenGeometry, qreal devicePixelRatio, Core::ISettingsStore &settings, QWidget *parent) :
        QWidget(parent), m_settings(settings), m_screenGeometry(screenGeometry), m_devicePixelRatio(devicePixelRatio)
    {
        // 物理像素图按该屏缩放因子标记一次，之后的绘制一律用逻辑坐标，读数与选区才对得上同一个点
        m_background = background;
        m_background.setDevicePixelRatio(devicePixelRatio);

        // 视野档在这一轮开始时读一次：读坏了只记一条日志，而不是每帧绘制都刷一句同样的话
        std::string complaint;
        m_magnifierView = Pick::magnifierViewOrDefault(
                m_settings.stringValue(GyanisScribe::Core::kCaptureMagnifierView, QString::fromUtf8(GyanisScribe::Pick::magnifierViewToken(Pick::MagnifierView::Narrow)))
                        .toStdString(),
                complaint);
        if (!complaint.empty())
        {
            Base::Logger::warning(std::format("放大镜视野读不出：{}，这一轮按窄档（9×9）走", complaint));
        }

        // 尺寸单位也是这一轮开始时读一次：读数、复制与尺寸框三处共用它，中途换档会让刚看到的数敲回去变成另一块选区
        m_sizeUnit =
                sizeUnitOrDefault(m_settings.stringValue(GyanisScribe::Core::kCaptureSizeUnit, QString::fromUtf8(sizeUnitToken(SizeUnit::Logical))).toStdString(), complaint);
        if (!complaint.empty())
        {
            Base::Logger::warning(std::format("尺寸单位读不出：{}", complaint));
        }

        // 无边框 + 置顶 + Tool：Tool 让它不进任务栏，截图时不该多出一个窗口条目
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        setGeometry(m_screenGeometry);
        setMouseTracking(true); // 十字辅助线要在不按鼠标时也跟着走
        setCursor(Qt::CrossCursor);
        setFocusPolicy(Qt::StrongFocus);
    }

    CaptureOverlay::~CaptureOverlay() = default;

    QRect CaptureOverlay::selectedRegion() const
    {
        if (!m_isSelecting && m_anchorPoint == m_currentPoint)
        {
            return {};
        }
        return normalizeRect(m_anchorPoint, m_currentPoint);
    }

    void CaptureOverlay::confirmSelection()
    {
        const auto region = selectedRegion();
        if (region.width() <= 0 || region.height() <= 0)
        {
            // 没框到东西就不产出：0×0 的图一路传下去会变成一个打不开的产物
            return;
        }
        emit regionSelected(region);
    }

    void CaptureOverlay::setWindowCandidates(const std::vector<QRect> &globalRects)
    {
        // 换成局部坐标：遮罩上的鼠标读数与选区都是这块屏内的局部值，两套单位混在一起就会错一格
        const auto origin = m_screenGeometry.topLeft();
        m_windowCandidates.clear();
        m_windowCandidates.reserve(globalRects.size());
        for (const auto &rect: globalRects)
        {
            m_windowCandidates.push_back(rect.translated(-origin.x(), -origin.y()));
        }
        refreshHoveredRegion();
    }

    void CaptureOverlay::setElementChain(const std::vector<QRect> &globalRects)
    {
        // 同窗口候选：换算到这块屏的局部坐标再用
        const auto origin = m_screenGeometry.topLeft();
        m_elementChain.clear();
        m_elementChain.reserve(globalRects.size());
        for (const auto &rect: globalRects)
        {
            const auto local = rect.translated(-origin.x(), -origin.y());
            // 相邻同矩形并档：惰性树里一层包装层套一层同尺寸的壳，`,`、`.` 按的是「块」不是「层」，
            // 不并档就会出现按几下尺寸都不动的死档。只在相邻之间并——链是按深度排的，隔着不同尺寸的重复才是真的退了回去
            if (!m_elementChain.empty() && m_elementChain.back() == local)
            {
                continue;
            }
            m_elementChain.push_back(local);
        }
        refreshHoveredRegion();
        update();
    }

    void CaptureOverlay::setRegionHistory(const std::vector<QRect> &localRects)
    {
        // 进来就是这块屏的局部坐标（条目在记的时候就按屏存的），只做一次上限裁剪：
        // 与 RegionHistory 那条上限同解，别在两处各写一个数字
        m_regionHistory = localRects;
        if (m_regionHistory.size() > kRegionHistoryLimit)
        {
            m_regionHistory.resize(kRegionHistoryLimit);
        }
        m_historyIndex = -1;
    }

    std::optional<QRect> CaptureOverlay::hoveredRegion() const
    {
        return m_hoveredRegion;
    }

    QColor CaptureOverlay::colorUnderCursor() const
    {
        if (!m_hasCursorPoint || m_background.isNull())
        {
            // 没收到鼠标移动就把 (0,0) 当成光标，报出来的是这块屏左上角的颜色
            return {};
        }
        const auto pixel = Pick::framePixelFor(globalCursorPoint(), m_screenGeometry, m_devicePixelRatio, m_background.size());
        return m_background.pixelColor(pixel);
    }

    Pick::MagnifierLayout CaptureOverlay::magnifierLayout() const
    {
        if (!m_hasCursorPoint)
        {
            return {};
        }
        // 「不显示」也按窄档摆一次：读数条要留在原位，插图由 isMagnifierVisible() 挡住
        const auto edge = Pick::magnifierSampleEdgeFor(m_magnifierView);
        return Pick::magnifierLayoutFor(globalCursorPoint(), m_screenGeometry, m_screenGeometry, edge > 0 ? edge : Pick::kMagnifierSampleEdge, Pick::kMagnifierViewEdge);
    }

    Pick::MagnifierView CaptureOverlay::magnifierView() const noexcept
    {
        return m_magnifierView;
    }

    QRect CaptureOverlay::magnifierRect() const
    {
        if (!isMagnifierVisible())
        {
            return {};
        }
        return magnifierLayout().viewRect.translated(-m_screenGeometry.topLeft());
    }

    QPoint CaptureOverlay::globalCursorPoint() const
    {
        // 放大镜几何按全局坐标算（副屏的原点不是 (0,0)），画回遮罩时才减掉这块屏的原点
        return m_cursorPoint + m_screenGeometry.topLeft();
    }

    bool CaptureOverlay::isMagnifierVisible() const noexcept
    {
        // 光标亮过、这块屏的冻帧真的抓到了，且这一档不是「不显示」
        return m_hasCursorPoint && !m_background.isNull() && m_magnifierView != Pick::MagnifierView::Off;
    }

    bool CaptureOverlay::isReadoutVisible() const noexcept
    {
        return m_hasCursorPoint && !m_background.isNull();
    }

    QRect CaptureOverlay::readoutStripRect() const
    {
        if (!isReadoutVisible())
        {
            return {};
        }
        const auto viewRect = magnifierLayout().viewRect.translated(-m_screenGeometry.topLeft());
        return QRect{viewRect.left(), viewRect.bottom() + kMagnifierPenWidth, viewRect.width(), kMagnifierStripHeight};
    }

    Pick::ColorFormat CaptureOverlay::readoutFormat() const noexcept
    {
        return m_readoutFormat;
    }

    void CaptureOverlay::paintGrips(QPainter &painter, const QRect &region)
    {
        const auto edge = GyanisScribe::Gui::kSelectionGripEdge;
        painter.setPen(QPen{GyanisScribe::Gui::kAccentColor, 1});
        for (const auto &center: gripCentersFor(region))
        {
            const auto rect = QRect{center - QPoint{edge / 2, edge / 2}, QSize{edge, edge}};
            // 方块里填底色而不是纯填充：压在深色画面上时实心同色块会看不出来
            painter.setBrush(GyanisScribe::Gui::kCaptureHintBackgroundColor);
            painter.drawRect(rect);
        }
    }

    std::array<QPoint, 8> CaptureOverlay::gripCentersFor(const QRect &region)
    {
        const auto horizontalMiddle = (region.left() + region.right()) / 2;
        const auto verticalMiddle   = (region.top() + region.bottom()) / 2;
        return {QPoint{region.left(), region.top()},     QPoint{horizontalMiddle, region.top()},    QPoint{region.right(), region.top()},   QPoint{region.right(), verticalMiddle},
                QPoint{region.right(), region.bottom()}, QPoint{horizontalMiddle, region.bottom()}, QPoint{region.left(), region.bottom()}, QPoint{region.left(), verticalMiddle}};
    }

    void CaptureOverlay::paintMagnifier(QPainter &painter)
    {
        if (!isReadoutVisible())
        {
            return;
        }

        const auto layout   = magnifierLayout();
        const auto viewRect = layout.viewRect.translated(-m_screenGeometry.topLeft());
        if (!viewRect.isValid())
        {
            return;
        }

        // 视野档是「不显示」时只跳过这枚插图，下面那条读数条照旧画在原位
        if (isMagnifierVisible())
        {
            const auto cropPixel  = Pick::framePixelFor(layout.sourceRect.topLeft(), m_screenGeometry, m_devicePixelRatio, m_background.size());
            const auto sampleEdge = Pick::magnifierSampleEdgeFor(m_magnifierView);

            // 取样范围是逻辑像素，抓帧是设备像素：边长要乘缩放比，夹到帧内而不是让它越界画出一片空白
            const auto cropSpan = static_cast<int>(std::lround(sampleEdge * m_devicePixelRatio));
            const auto cropSize = QSize{std::clamp(cropSpan, 1, m_background.width() - cropPixel.x()), std::clamp(cropSpan, 1, m_background.height() - cropPixel.y())};

            // 最近邻放大：Smooth 会把格子糊成一团，放大镜就没有存在的意义了
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::NoBrush);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawImage(viewRect, m_background.copy(QRect{cropPixel, cropSize}));

            auto borderPen = QPen(GyanisScribe::Gui::kAccentColor);
            borderPen.setWidth(kMagnifierPenWidth);
            painter.setPen(borderPen);
            painter.drawRect(viewRect);

            painter.setPen(QPen{GyanisScribe::Gui::kCrosshairColor, 1});
            for (auto step = 1; step < sampleEdge; ++step)
            {
                const auto offset = step * layout.cellSize;
                painter.drawLine(viewRect.left() + offset, viewRect.top(), viewRect.left() + offset, viewRect.bottom());
                painter.drawLine(viewRect.left(), viewRect.top() + offset, viewRect.right(), viewRect.top() + offset);
            }
        }

        // 读数条压在放大镜下沿：光标那一点的坐标与颜色，不必松手就能核对框到的是哪一格
        const auto stripRect = readoutStripRect();
        const auto readout   = QStringLiteral("%1  %2").arg(formatCursorReadout(), readoutColorText());
        painter.setPen(Qt::NoPen);
        painter.setBrush(GyanisScribe::Gui::kCaptureHintBackgroundColor);
        painter.drawRect(stripRect);
        QFont stripFont = font();
        stripFont.setPointSize(GyanisScribe::Gui::kHintFontSize);
        painter.setFont(stripFont);
        painter.setPen(GyanisScribe::Gui::kCaptureHintTextColor);
        painter.drawText(stripRect.adjusted(GyanisScribe::Gui::kHintPadding, 0, -GyanisScribe::Gui::kHintPadding, 0), Qt::AlignVCenter | Qt::AlignLeft, readout);
    }

    QString CaptureOverlay::readoutColorText() const
    {
        // 字母档现读配置：这一格只管 HEX 写法，其它写法读出来不受它影响
        const auto casing = Pick::hexCasingOf(m_settings.boolValue(GyanisScribe::Core::kPickHexUppercase, true));
        return Pick::formatColor(colorUnderCursor(), m_readoutFormat, casing);
    }

    SizeUnit CaptureOverlay::sizeUnit() const noexcept
    {
        return m_sizeUnit;
    }

    QRect CaptureOverlay::measuredRect(const QRect &logicalRegion) const
    {
        return rectInUnit(logicalRegion, m_devicePixelRatio, m_sizeUnit);
    }

    QString CaptureOverlay::selectionSizeText() const
    {
        const auto measured = measuredRect(selectedRegion());
        return QStringLiteral("%1×%2").arg(measured.width()).arg(measured.height());
    }

    QString CaptureOverlay::formatCursorReadout() const
    {
        const auto global = globalCursorPoint();
        return QStringLiteral("(%1, %2)").arg(global.x()).arg(global.y());
    }

    void CaptureOverlay::refreshHoveredRegion()
    {
        if (m_isSelecting || !selectedRegion().isNull())
        {
            m_hoveredRegion.reset();
            return;
        }

        if (!m_elementChain.empty())
        {
            // 链按「最深在前」存，位次就是下标：链换了长度也还指得着同一档，用户按到哪一档就一直停在那一档
            const auto count = m_elementChain.size();
            if (m_elementAscent < count)
            {
                m_hoveredRegion = m_elementChain[m_elementAscent];
                return;
            }
            // 升到最深一级元素还往外一层：把整扇窗口当成链的更外一层，正好接上窗口候选
        }
        m_hoveredRegion = regionAtPoint(m_cursorPoint, m_windowCandidates);
    }

    QRect CaptureOverlay::normalizeRect(const QPoint &anchor, const QPoint &current)
    {
        return QRect(anchor, current).normalized();
    }

    QPoint CaptureOverlay::hintPositionFor(const QRect &region) const
    {
        // 读数默认画在选区上方一点；贴到屏顶时改画到选区下方，否则读数会被屏边切掉
        const auto hintOffset = region.height() + GyanisScribe::Gui::kSelectionBorderWidth + GyanisScribe::Gui::kHintPadding;
        if (region.top() - hintOffset - 4 >= 0)
        {
            return QPoint(region.left(), region.top() - hintOffset);
        }
        return QPoint(region.left(), region.bottom() + 4);
    }

    void CaptureOverlay::paintEvent(QPaintEvent *event)
    {
        Q_UNUSED(event);

        QPainter   painter(this);
        const auto widgetRect = QRect(QPoint(0, 0), size());
        const auto region     = selectedRegion();
        // 还没框的时候把悬停认出的那一块当预览画出来：不亮这块，用户就只能靠猜知道点一下会不会中
        const auto shown = region.isNull() ? m_hoveredRegion.value_or(QRect{}) : region;

        painter.drawImage(0, 0, m_background);

        // 压暗层用四块矩形围出选区，而不是建偶奇路径：四块矩形是这里最省事也最不容易画错形状的做法
        painter.setPen(Qt::NoPen);
        painter.setBrush(GyanisScribe::Gui::kCaptureMaskColor);
        if (shown.isNull())
        {
            painter.drawRect(widgetRect);
        } else
        {
            painter.setClipRegion(QRegion(widgetRect) - QRegion(shown));
            painter.drawRect(widgetRect);
            painter.setClipping(false);
        }

        if (!shown.isNull())
        {
            auto borderPen = QPen(GyanisScribe::Gui::kAccentColor);
            borderPen.setWidth(GyanisScribe::Gui::kSelectionBorderWidth);
            painter.setPen(borderPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(shown);

            if (m_isSelecting)
            {
                // 拖动中给十字虚线定位，松手后不留在画面上干扰读数
                auto crossPen = QPen(GyanisScribe::Gui::kCrosshairColor);
                crossPen.setStyle(Qt::DashLine);
                painter.setPen(crossPen);
                painter.drawLine(QPoint(m_currentPoint.x(), 0), QPoint(m_currentPoint.x(), widgetRect.height()));
                painter.drawLine(QPoint(0, m_currentPoint.y()), QPoint(widgetRect.width(), m_currentPoint.y()));

                // 三分网格：两条竖两条横的构图参考线，与十字线同一档（只在拖动途中露面）
                painter.setPen(QPen(GyanisScribe::Gui::kCrosshairColor));
                for (const auto &line: thirdsGridLinesFor(shown))
                {
                    painter.drawLine(line);
                }
            }

            // 读数按这一轮那一档量：拖框时看到的宽高要与复制出去、敲回尺寸框的那一组数是同一个口径
            auto hint = formatSelectionHint(measuredRect(shown));
            if (m_aspectLocked)
            {
                // 锁着比例时在读数后面标出来：不标的话「拖不出想要的形状」会被当成手抖
                hint += GyanisScribe::Base::uiText("（锁定 %1）").arg(aspectRatioToText(m_aspectRatio));
            }
            if (m_cursorTouched && m_cursorIncluded)
            {
                // 翻过指针档就把当下那一档写进读数：带不带指针看不出来，比一律不带更糟
                hint += *m_cursorIncluded ? GyanisScribe::Base::uiText("（含指针）") : GyanisScribe::Base::uiText("（不含指针）");
            }
            QFont hintFont = font();
            hintFont.setPointSize(GyanisScribe::Gui::kHintFontSize);
            painter.setFont(hintFont);
            const QFontMetrics metrics(hintFont);
            const auto         textSize    = metrics.size(Qt::TextSingleLine, hint);
            const auto         hintTopLeft = hintPositionFor(shown);
            const QRect        hintRect(hintTopLeft, textSize + QSize(GyanisScribe::Gui::kHintPadding * 2, GyanisScribe::Gui::kHintPadding));

            painter.setPen(Qt::NoPen);
            painter.setBrush(GyanisScribe::Gui::kCaptureHintBackgroundColor);
            painter.drawRect(hintRect);
            painter.setPen(GyanisScribe::Gui::kCaptureHintTextColor);
            painter.drawText(hintRect, Qt::AlignCenter, hint);

            // 「已复制尺寸 …」贴在读数框下面一格；指针一动就清（见 mouseMoveEvent），
            // 不为它引一个定时器——那种反馈本来就不必跟秒表绑定
            if (!m_copiedSizeHint.isEmpty())
            {
                const auto  copyTextSize = metrics.size(Qt::TextSingleLine, m_copiedSizeHint);
                const QRect copyRect(hintTopLeft + QPoint(0, textSize.height() + kCopiedHintGap),
                                     copyTextSize + QSize(GyanisScribe::Gui::kHintPadding * 2, GyanisScribe::Gui::kHintPadding));
                painter.setPen(Qt::NoPen);
                painter.setBrush(GyanisScribe::Gui::kCaptureHintBackgroundColor);
                painter.drawRect(copyRect);
                painter.setPen(GyanisScribe::Gui::kCaptureHintTextColor);
                painter.drawText(copyRect, Qt::AlignCenter, m_copiedSizeHint);
            }
        }

        // 选区落定后在八个抓点上画小方块：不画就没人知道这块还能拖
        if (!shown.isNull() && !m_isSelecting)
        {
            paintGrips(painter, shown);
        }

        // 放大镜排在最后画：它会挡住底下那片，但用户正要看的就是光标那一点，优先级比背景高。
        // 只要光标亮过就显示，不限于拖框途中——挑起始角与拉手柄这两处同样要像素级读数
        if (isReadoutVisible())
        {
            paintMagnifier(painter);
        }
    }

    void CaptureOverlay::mousePressEvent(QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton)
        {
            return;
        }

        // 尺寸输入框开着时点画布＝放弃这次输入（回车才算应用）：留着框会让人以为还没提交
        if (m_sizeEditor != nullptr)
        {
            closeSizeEditor();
        }

        const auto point  = event->position().toPoint();
        const auto region = selectedRegion();
        // 已经框好的一块先认抓点：认到了就改这一块，而不是重新框一遍。
        // 差几像素要重框是框选里最常见的一次火，八个抓点就是为了不必重框
        if (!region.isNull())
        {
            if (const auto grip = gripAt(region, point); grip != SelectionGrip::None)
            {
                m_isAdjusting = true;
                m_draggedGrip = grip;
                m_adjustPoint = point;
                update();
                return;
            }
        }

        m_isSelecting    = true;
        m_anchorPoint    = point;
        m_currentPoint   = m_anchorPoint;
        m_cursorPoint    = m_anchorPoint;
        m_hasCursorPoint = true;
        // 重新框一块就不是「取自历史」了：下一次 R 该从最近一条重新起，而不是接着上一轮的位次
        m_historyIndex = -1;
        // 预览留着不熄：按下还没拖的那一瞬间，「点一下要哪扇」的答案就是它。
        // 真拖起来了再由 refreshHoveredRegion 关掉，同一时刻不会出现两个矩形
        update();
    }

    void CaptureOverlay::mouseMoveEvent(QMouseEvent *event)
    {
        // 光标位置与选区端点分开记：没按下时也要重画（辅助线与悬停跟着光标走），
        // 但把它写进 m_currentPoint 会让 selectedRegion() 从原点编出一块根本没框过的选区
        m_cursorPoint    = event->position().toPoint();
        m_hasCursorPoint = true;
        m_copiedSizeHint.clear();

        // 悬停点报给服务去问界面元素：只在「没在框选、也还没有选区」时报，
        // 这两种状态下悬停预览本来就不显示，问了也是白花一次跨进程的费用。
        // 先报再重算：服务把新链送回来之后，下面的 refreshHoveredRegion 用的就是这一帧的链
        if (!m_isSelecting && !m_isAdjusting && selectedRegion().isNull())
        {
            emit hoverPointChanged(globalCursorPoint());
        }

        if (m_isAdjusting && m_draggedGrip)
        {
            const auto delta = m_cursorPoint - m_adjustPoint;
            m_adjustPoint    = m_cursorPoint;
            // 逐帧增量交给我这儿的几何函数夹：一次挪多长都不许越过对边，也不许出这块屏
            const auto moved = dragRegionByGrip(selectedRegion(), *m_draggedGrip, delta, QRect{QPoint{0, 0}, size()});
            m_anchorPoint    = moved.topLeft();
            m_currentPoint   = moved.bottomRight();
            update();
            return;
        }

        if (m_isSelecting)
        {
            const auto limit = QRect{QPoint{0, 0}, size()};
            // Shift 临时锁正方形；没按 Shift 时看长宽比锁。两处收口都在纯函数里（见 ScreenUtils）：
            // 比例锁关着时 ratioLockedPoint 原样交回光标点，与老行为逐像素同解
            m_currentPoint = event->modifiers().testFlag(Qt::ShiftModifier) ? squareLockedPoint(m_anchorPoint, m_cursorPoint, true, limit)
                                                                            : ratioLockedPoint(m_anchorPoint, m_cursorPoint, m_aspectLocked ? m_aspectRatio : QSize{}, limit);
        } else
        {
            // 没在拖的时候把光标形状交给抓点：形状变了就是「这里能抓」，不用试
            setCursor(cursorForGrip(gripAt(selectedRegion(), m_cursorPoint)));
        }
        refreshHoveredRegion();
        update();
    }

    void CaptureOverlay::mouseReleaseEvent(QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton)
        {
            return;
        }

        if (m_isAdjusting)
        {
            // 拖抓点改的大小已经写进两个端点了，松手只收状态：
            // 再走一遍「原地放开采纳悬停」会把刚改好的那块换成别的
            m_isAdjusting = false;
            m_draggedGrip.reset();
            update();
            return;
        }

        if (m_isSelecting)
        {
            m_isSelecting = false;
            // 松手的那一刻也走一遍锁：直接写原始光标点的话，Move 里锁好的端点会被覆盖掉，
            // 拖出来的正方形/按比例框会在松手瞬间弹回自由形状（Shift 锁正方形以前就有这个毛病）
            const auto releasePoint = event->position().toPoint();
            const auto limit        = QRect{QPoint{0, 0}, size()};
            m_currentPoint = event->modifiers().testFlag(Qt::ShiftModifier) ? squareLockedPoint(m_anchorPoint, releasePoint, true, limit)
                                                                            : ratioLockedPoint(m_anchorPoint, releasePoint, m_aspectLocked ? m_aspectRatio : QSize{}, limit);

            // 按下又原地放开（没拖出任何一块）时，认此刻悬停亮着的那个候选：
            // 「点一下就要这一扇窗口」是框选最常用的走法，而原地放开过去什么也不产出
            if (m_hoveredRegion && selectedRegion().isNull())
            {
                m_anchorPoint  = m_hoveredRegion->topLeft();
                m_currentPoint = m_hoveredRegion->bottomRight();
                m_hoveredRegion.reset();
                m_historyIndex = -1; // 采纳的是悬停候选，不是历史里那一条
            }
            update();
        }
    }

    void CaptureOverlay::mouseDoubleClickEvent(QMouseEvent *event)
    {
        // 双击即确认当前选区，不必再按 Enter
        Q_UNUSED(event);
        confirmSelection();
    }

    void CaptureOverlay::keyPressEvent(QKeyEvent *event)
    {
        // 方向键微调：裸按整块挪、`Shift` 挪右下角、`Ctrl` 把这一侧的边外扩一格、
        // `Ctrl+Shift` 把这一侧的边往里收一格。已经点中窗口或拖出一块之后，
        // 差一像素就得重框一遍，这几条键就是为了不必重框
        if (const auto step = GyanisScribe::Gui::arrowStepFor(event->key()))
        {
            const auto region = selectedRegion();
            if (!region.isNull())
            {
                const auto limit   = QRect{QPoint{0, 0}, size()};
                const auto control = event->modifiers().testFlag(Qt::ControlModifier);
                const auto shift   = event->modifiers().testFlag(Qt::ShiftModifier);
                const auto changed = control ? stretchRegionByEdge(region, *step, shift, limit) : (shift ? resizeRegion(region, *step, limit) : nudgeRegion(region, *step, limit));
                m_anchorPoint      = changed.topLeft();
                m_currentPoint     = changed.bottomRight();
                update();
            }
            return;
        }

        // 可换键的那几档排在按颗键判定的 switch 之前：它们认的是清册里现在那颗键，整颗组合相等才算命中。
        // 换上去的键与既有组合（如 Ctrl+A）撞上时这一层先赢——用户把它换上来就是要这颗键归这一档
        switch (const auto action = rebindableActionFor(event); action)
        {
            case RebindableKeyAction::None:
                break;
            case RebindableKeyAction::ToggleAspectRatio:
                toggleAspectRatioLock();
                return;
            case RebindableKeyAction::OpenSizeEditor:
                openSizeEditor();
                return;
            case RebindableKeyAction::CycleRegionHistory:
            {
                // 没有历史时什么都不做，不留一个按了没反应的哑键
                if (m_regionHistory.empty())
                {
                    QWidget::keyPressEvent(event);
                    return;
                }
                const auto count = static_cast<int>(m_regionHistory.size());
                // 多按了一个 Shift 就是反向：转一圈回到起点。方向键那一档是「挪一格」，这一档是「换一块」
                const auto step = event->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1;
                if (m_historyIndex < 0)
                {
                    // 还没从历史里挑过：正向从最近一条起、反向从最久一条起——两头各自从各自那一端起步
                    m_historyIndex = step > 0 ? 0 : count - 1;
                } else
                {
                    m_historyIndex = (m_historyIndex + step + count) % count;
                }

                const auto &picked = m_regionHistory[static_cast<std::size_t>(m_historyIndex)];
                m_isSelecting      = false;
                m_isAdjusting      = false;
                m_draggedGrip.reset();
                m_hoveredRegion.reset();
                m_anchorPoint  = picked.topLeft();
                m_currentPoint = picked.bottomRight();
                update();
                return;
            }
            case RebindableKeyAction::CopySelectedSize:
                if (const auto region = selectedRegion(); !region.isNull())
                {
                    // 复制的那一串跟着读数那一档走：照着屏幕上看到的数复制却得到另一组数，比哪种都难查
                    const auto text = selectionSizeText();
                    QGuiApplication::clipboard()->setText(text);
                    m_copiedSizeHint = GyanisScribe::Base::uiText("已复制尺寸 %1").arg(text);
                    update();
                }
                return;
            case RebindableKeyAction::CycleColorFormat:
                // 读数里颜色那一格换一种写法（词表见 Pick/ColorFormat，六个轮一圈）：对着目标程序核对配色时
                // 常常要换一种写法看，不必为这个先离开截图去设置里改一档
                m_readoutFormat = *Pick::colorFormatAt((Pick::indexOfColorFormat(m_readoutFormat) + 1) % static_cast<int>(Pick::kColorFormatCount));
                update();
                return;
            case RebindableKeyAction::ToggleCursor:
                // 只改这一轮，配置那一格不动。滚动长截图那一轮根本没有这一档（按帧接图会把一枚指针复制成一串），
                // 这时交回 Qt
                if (!m_cursorIncluded.has_value())
                {
                    QWidget::keyPressEvent(event);
                    return;
                }
                m_cursorIncluded = !*m_cursorIncluded;
                m_cursorTouched  = true;
                emit cursorIncludeChanged(*m_cursorIncluded);
                update();
                return;
        }

        switch (event->key())
        {
            case Qt::Key_Escape:
                emit dismissed();
                return;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                confirmSelection();
                return;
            case Qt::Key_A:
                // Ctrl+A 选整块屏：多屏上省掉从一角拖到另一角这一趟。裸按那一档归清册，在上面已经判过
                if (event->modifiers() != Qt::ControlModifier)
                {
                    QWidget::keyPressEvent(event);
                    return;
                }
                m_anchorPoint  = QPoint{0, 0};
                m_currentPoint = QPoint{size().width() - 1, size().height() - 1};
                m_historyIndex = -1; // 整屏不是取自历史：下一次挑历史选区该从最近一条重新起
                update();
                return;
            case Qt::Key_Comma:
            case Qt::Key_Period:
            {
                // `,` 往上一层、`.` 往回一层：悬停认出的界面元素从最深一级起，按这两个键在层级间走，
                // 走到最外层再往上就是整扇窗口，与「悬停认窗」正好接上。没有元素链时什么都不做，别留哑键。
                // 这两颗不给换：符号键不在允许集合里（不同布局上落点不同），而且它们是一对，改一颗另一颗就配不成对
                if (event->modifiers() != Qt::NoModifier || m_elementChain.empty())
                {
                    QWidget::keyPressEvent(event);
                    return;
                }
                if (event->key() == Qt::Key_Comma)
                {
                    // 上限是链长：等于链长那一档看的就是整扇窗口
                    m_elementAscent = std::min(m_elementAscent + 1, m_elementChain.size());
                } else if (m_elementAscent > 0)
                {
                    --m_elementAscent;
                }
                refreshHoveredRegion();
                update();
                return;
            }
            default:
                QWidget::keyPressEvent(event);
                return;
        }
    }

    RebindableKeyAction CaptureOverlay::rebindableActionFor(const QKeyEvent *event) const
    {
        for (const auto &row: kRebindableRows)
        {
            const auto *entry = GyanisScribe::Gui::appShortcutEntry(row.token);
            if (entry != nullptr && GyanisScribe::Gui::isAppShortcutHit(m_settings, *entry, event->key(), event->modifiers(), row.allowShiftVariant))
            {
                return row.action;
            }
        }
        return RebindableKeyAction::None;
    }

    void CaptureOverlay::setLockedAspectRatio(const QSize &ratio)
    {
        if (ratio.width() <= 0 || ratio.height() <= 0)
        {
            return;
        }
        m_aspectRatio  = ratio;
        m_aspectLocked = true;
        update();
    }

    void CaptureOverlay::setCursorIncluded(const std::optional<bool> included)
    {
        // 换档时把「翻过没有」一并清掉：新一轮的初值来自配置，读数不该带着上一轮的痕迹
        m_cursorIncluded = included;
        m_cursorTouched  = false;
    }

    std::optional<bool> CaptureOverlay::cursorIncluded() const noexcept
    {
        return m_cursorIncluded;
    }

    QSize CaptureOverlay::lockedAspectRatio() const
    {
        return m_aspectLocked ? m_aspectRatio : QSize{};
    }

    void CaptureOverlay::toggleAspectRatioLock()
    {
        if (m_aspectLocked)
        {
            m_aspectLocked = false;
            // 持久层那一档跟着清：用户明确松开的档不该在下一轮里复活。
            // 本轮里的「上一档」留着——再按 A 还能用同一个比例锁上
            emit aspectRatioLockChanged(QSize{});
            update();
            return;
        }

        // 锁上时优先取当前选区的比例（先框一个相近的再按 A 就是「锁这一档」）；
        // 没有选区就用本轮上次锁过的。两样都没有时什么都不做——凭空锁一个比例只会让下一拖出意外
        if (const auto region = selectedRegion(); !region.isNull() && region.width() > 0 && region.height() > 0)
        {
            const int divisor = std::gcd(region.width(), region.height());
            m_aspectRatio     = QSize{region.width() / divisor, region.height() / divisor};
        }
        if (m_aspectRatio.isEmpty())
        {
            return;
        }
        m_aspectLocked = true;
        emit aspectRatioLockChanged(m_aspectRatio);
        update();
    }

    void CaptureOverlay::openSizeEditor()
    {
        if (m_sizeEditor == nullptr)
        {
            m_sizeEditor = new QLineEdit{this};
            m_sizeEditor->setObjectName(QStringLiteral("overlaySizeEditor"));
            m_sizeEditor->setAlignment(Qt::AlignCenter);
            m_sizeEditor->setFixedWidth(kSizeEditorWidth);
            m_sizeEditor->setAccessibleName(GyanisScribe::Base::uiText("选区尺寸"));
            m_sizeEditor->installEventFilter(this);
        }

        const auto region = selectedRegion();
        if (!region.isNull())
        {
            // 框里预填的与读数是同一档：看到 1500 却要在框里敲 1000 才是同一块，这种换算没人记得住
            // 框里预填的与读数是同一档：看到 1500 却要在框里敲 1000 才是同一块，这种换算没人记得住
            const auto measured = measuredRect(region);
            m_sizeEditor->setText(QStringLiteral("%1x%2").arg(measured.width()).arg(measured.height()));
        } else
        {
            m_sizeEditor->setText({});
        }
        m_sizeEditor->setToolTip(QString::fromUtf8(
                GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("按{}填；这一档跟着设置里的「尺寸单位」走"), sizeUnitLabel(m_sizeUnit).toStdString()).c_str()));

        // 摆在读数框那一格（贴顶时会翻到选区下方）；还没有选区就摆光标右下
        const auto base = region.isNull()
                                  ? (m_hasCursorPoint ? m_cursorPoint + QPoint{kSizeEditorCursorGap, kSizeEditorCursorGap} : QPoint{kSizeEditorCursorGap, kSizeEditorCursorGap})
                                  : hintPositionFor(region);
        const auto editorSize = QSize{m_sizeEditor->width(), m_sizeEditor->sizeHint().height()};
        m_sizeEditor->move(
                QPoint{std::clamp(base.x(), 0, std::max(0, size().width() - editorSize.width())), std::clamp(base.y(), 0, std::max(0, size().height() - editorSize.height()))});
        m_sizeEditor->show();
        m_sizeEditor->setFocus();
        m_sizeEditor->selectAll();
    }

    void CaptureOverlay::commitSizeEditor()
    {
        if (m_sizeEditor == nullptr)
        {
            return;
        }

        const auto region = selectedRegion();
        const auto anchor = region.isNull() ? QPoint{0, 0} : region.topLeft();
        const auto limit  = QRect{QPoint{0, 0}, size()};
        // 两种写法都认：「宽x高」保住左上角，「x,y,宽,高」连位置一起指定（与配置里那条选区记录同一套写法）；
        // 越界的那一边由 regionFromSizeText 夹回屏内——输错一个大数之后画面里只剩一小角，比夹住更让人摸不着头脑
        const auto next = regionFromSizeText(m_sizeEditor->text(), anchor, limit, m_devicePixelRatio, m_sizeUnit);
        if (!next)
        {
            // 看不懂就留在框里继续改：把「看不懂」变成「框没了」是最坏的一种反馈
            return;
        }

        m_anchorPoint  = next->topLeft();
        m_currentPoint = next->bottomRight();
        m_historyIndex = -1;
        closeSizeEditor();
        update();
    }

    void CaptureOverlay::cancelSizeEditor()
    {
        closeSizeEditor();
    }

    void CaptureOverlay::closeSizeEditor()
    {
        if (m_sizeEditor == nullptr)
        {
            return;
        }
        auto *editor = m_sizeEditor;
        m_sizeEditor = nullptr;
        editor->hide();
        editor->deleteLater();
    }

    bool CaptureOverlay::eventFilter(QObject *watched, QEvent *event)
    {
        if (watched == m_sizeEditor && event->type() == QEvent::KeyPress)
        {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape)
            {
                cancelSizeEditor();
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            {
                commitSizeEditor();
                return true;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

} // namespace GyanisScribe::Capture
