#include "Gui/Capture/CaptureOverlay.h"

#include "Core/Capture/RegionHistory.h"
#include "Core/Capture/ScreenUtils.h"
#include "Base/SizeText.h"
#include "Base/Logger.h"
#include "Base/Text.h"
#include "Core/Pick/ColorFormat.h"
#include "Core/Pick/PickGeometry.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Gui/Controls/AppShortcuts.h"
#include "Gui/Theme/DesignTokens.h"
#include "Gui/Controls/KeyStep.h"
#include "Gui/Controls/LineEdit.h"

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
        std::string_view                       token;                     ///< 清册里的标识
        GyanisScribe::Gui::RebindableKeyAction action;                    ///< 命中之后要走哪个动作
        bool                                   allowShiftVariant = false; ///< 认不认「同一颗键多按一个 Shift」那一档
    };

    constexpr std::array<RebindableRow, 6> kRebindableRows = {
            RebindableRow{"Mask/AspectRatio", GyanisScribe::Gui::RebindableKeyAction::ToggleAspectRatio, false},
            RebindableRow{"Mask/SizeEditor", GyanisScribe::Gui::RebindableKeyAction::OpenSizeEditor, false},
            RebindableRow{"Mask/RegionCycle", GyanisScribe::Gui::RebindableKeyAction::CycleRegionHistory, true},
            RebindableRow{"Mask/CopySize", GyanisScribe::Gui::RebindableKeyAction::CopySelectedSize, false},
            RebindableRow{"Mask/ColorFormat", GyanisScribe::Gui::RebindableKeyAction::CycleColorFormat, false},
            RebindableRow{"Mask/PointerToggle", GyanisScribe::Gui::RebindableKeyAction::ToggleCursor, false},
    };

    /// 放大镜下面那条读数条的高度，逻辑像素
    constexpr int kMagnifierStripHeight = 24;

    /// 「已复制尺寸」提示与读数框之间的间距，逻辑像素
    constexpr int kCopiedHintGap = 4;

    /// 尺寸输入框的宽度，逻辑像素：装得下「1920x1080」这样的读数
    constexpr int kSizeEditorWidth = 148;

    /// 尺寸输入框摆在光标右下时的偏移，逻辑像素
    constexpr int kSizeEditorCursorGap = 12;

    /// 松手时框出的宽与高都不超过这一格（逻辑像素）时按「原地点击」处理：手抖不该把整窗选择变成碎片
    constexpr int kHoverClickSlop = 4;

    /// 局部重绘时选区/悬停框外扩的格数，逻辑像素：盖住边框与抓点
    constexpr int kRepaintMargin = 4;

    /// 放大镜边框外让的格数，逻辑像素：边框线宽有一半压在矩形外，脏区少了这一格就留半条残影
    constexpr int kMagnifierBorderOutset = 1;

    /// 选择中十字线的带宽，逻辑像素：虚线本身只有一格，两侧各留一格给抗锯齿
    constexpr int kCrosshairBandWidth = 3;

    /// 模式横幅距遮罩左上角的边距，逻辑像素
    constexpr int kModeBannerMargin = 12;

    /// 模式横幅底板在文字两侧各让的留白，逻辑像素
    constexpr int kModeBannerPaddingX = 8;

    /// 模式横幅底板在文字上下各让的留白，逻辑像素
    constexpr int kModeBannerPaddingY = 4;
} // namespace

namespace GyanisScribe::Gui
{

    CaptureOverlay::CaptureOverlay(const QImage &background, const QRect &screenGeometry, qreal devicePixelRatio, Core::ISettingsStore &settings, QWidget *parent) :
        QWidget(parent), m_settings(settings), m_screenGeometry(screenGeometry), m_devicePixelRatio(devicePixelRatio)
    {
        // 物理像素图按该屏缩放因子标记一次，之后的绘制一律用逻辑坐标，读数与选区才对得上同一个点
        m_background = background;
        m_background.setDevicePixelRatio(devicePixelRatio);

        // 视野档在这一轮开始时读一次：读坏了只记一条日志，而不是每帧绘制都刷一句同样的话
        std::string complaint;
        m_magnifierView = Core::magnifierViewOrDefault(
                m_settings.stringValue(GyanisScribe::Core::kCaptureMagnifierView, QString::fromUtf8(GyanisScribe::Core::magnifierViewToken(Core::MagnifierView::Narrow)))
                        .toStdString(),
                complaint);
        if (!complaint.empty())
        {
            Base::Logger::warning(std::format("放大镜视野读不出：{}，这一轮按窄档（9×9）走", complaint));
        }

        // 尺寸单位也是这一轮开始时读一次：读数、复制与尺寸框三处共用它，中途换档会让刚看到的数敲回去变成另一块选区
        m_sizeUnit = Core::sizeUnitOrDefault(
                m_settings.stringValue(GyanisScribe::Core::kCaptureSizeUnit, QString::fromUtf8(Core::sizeUnitToken(Core::SizeUnit::Logical))).toStdString(), complaint);
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

    QRegion CaptureOverlay::lastPaintRegionForTesting() const noexcept
    {
        return m_lastPaintRegion;
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
        // 先记改动前的那一片：悬停框换位置时要连旧位置一起刷，否则原地留下一个框
        const auto before = visualRegionForCurrentState();

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
        update(before | visualRegionForCurrentState());
    }

    void CaptureOverlay::setRegionHistory(const std::vector<QRect> &localRects)
    {
        // 进来就是这块屏的局部坐标（条目在记的时候就按屏存的），只做一次上限裁剪：
        // 与 RegionHistory 那条上限同解，别在两处各写一个数字
        m_regionHistory = localRects;
        if (m_regionHistory.size() > Core::kRegionHistoryLimit)
        {
            m_regionHistory.resize(Core::kRegionHistoryLimit);
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
        const auto pixel = Core::framePixelFor(globalCursorPoint(), m_screenGeometry, m_devicePixelRatio, m_background.size());
        return m_background.pixelColor(pixel);
    }

    Core::MagnifierLayout CaptureOverlay::magnifierLayout() const
    {
        if (!m_hasCursorPoint)
        {
            return {};
        }
        // 「不显示」也按窄档摆一次：读数条要留在原位，插图由 isMagnifierVisible() 挡住
        const auto edge = Core::magnifierSampleEdgeFor(m_magnifierView);
        return Core::magnifierLayoutFor(globalCursorPoint(), m_screenGeometry, m_screenGeometry, edge > 0 ? edge : Core::kMagnifierSampleEdge, Core::kMagnifierViewEdge);
    }

    Core::MagnifierView CaptureOverlay::magnifierView() const noexcept
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
        return m_hasCursorPoint && !m_background.isNull() && m_magnifierView != Core::MagnifierView::Off;
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

    QRegion CaptureOverlay::visualRegionForCurrentState() const
    {
        const auto selection = selectedRegion();
        // 与绘制同一处取舍：还没框出选区时，悬停认出的那一块就是画面上亮着的那一块
        const auto shown = selection.isNull() ? m_hoveredRegion.value_or(QRect{}) : selection;

        QRegion region;
        // 模式横幅与选区无关，任何状态下都得算进来
        if (!m_modeBannerRect.isEmpty())
        {
            region += QRegion{m_modeBannerRect};
        }
        if (!shown.isNull())
        {
            // 外扩几格把边框、抓点与三分线都盖住：它们都贴在选区边上
            region += QRegion{shown.adjusted(-kRepaintMargin, -kRepaintMargin, kRepaintMargin, kRepaintMargin)};

            // 提示区按画上去的那三层文字逐块取：读数与确认行的宽高随字号、文案变，
            // 按一块固定宽高估就会把长的那一行裁出脏区
            const auto widgetRect = QRect{QPoint{0, 0}, size()};
            const auto hint       = hintLayoutFor(shown);
            for (const auto &rect: {hint.hintRect, hint.confirmRect, hint.copiedRect})
            {
                if (const auto visible = rect.intersected(widgetRect); !visible.isEmpty())
                {
                    region += QRegion{visible};
                }
            }
        }

        if (m_isSelecting && !shown.isNull())
        {
            // 十字虚线横贯整块遮罩，两条各按线宽取一条窄带
            const auto halfBand = kCrosshairBandWidth / 2;
            region += QRegion{QRect{0, m_currentPoint.y() - halfBand, size().width(), kCrosshairBandWidth}};
            region += QRegion{QRect{m_currentPoint.x() - halfBand, 0, kCrosshairBandWidth, size().height()}};
        }

        if (const auto magnifier = magnifierRect(); !magnifier.isEmpty())
        {
            region += QRegion{magnifier.adjusted(-kMagnifierBorderOutset, -kMagnifierBorderOutset, kMagnifierBorderOutset, kMagnifierBorderOutset)};
        }
        if (const auto readout = readoutStripRect(); !readout.isEmpty())
        {
            region += QRegion{readout};
        }
        return region;
    }

    Core::ColorFormat CaptureOverlay::readoutFormat() const noexcept
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
            const auto cropPixel  = Core::framePixelFor(layout.sourceRect.topLeft(), m_screenGeometry, m_devicePixelRatio, m_background.size());
            const auto sampleEdge = Core::magnifierSampleEdgeFor(m_magnifierView);

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
        // 坐标与颜色是读数：锁等宽后扫读不抖；同一行含中文，不换字族与字重
        stripFont = tabularFont(stripFont, TextScript::Mixed);
        painter.setFont(stripFont);
        painter.setPen(GyanisScribe::Gui::kCaptureHintTextColor);
        painter.drawText(stripRect.adjusted(GyanisScribe::Gui::kHintPadding, 0, -GyanisScribe::Gui::kHintPadding, 0), Qt::AlignVCenter | Qt::AlignLeft, readout);
    }

    QString CaptureOverlay::readoutColorText() const
    {
        // 字母档现读配置：这一格只管 HEX 写法，其它写法读出来不受它影响
        const auto casing = Core::hexCasingOf(m_settings.boolValue(GyanisScribe::Core::kPickHexUppercase, true));
        return Core::formatColor(colorUnderCursor(), m_readoutFormat, casing);
    }

    Core::SizeUnit CaptureOverlay::sizeUnit() const noexcept
    {
        return m_sizeUnit;
    }

    QRect CaptureOverlay::measuredRect(const QRect &logicalRegion) const
    {
        return Core::rectInUnit(logicalRegion, m_devicePixelRatio, m_sizeUnit);
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
        m_hoveredRegion = Core::regionAtPoint(m_cursorPoint, m_windowCandidates);
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
            return {region.left(), region.top() - hintOffset};
        }
        return {region.left(), region.bottom() + 4};
    }

    CaptureOverlay::HintLayout CaptureOverlay::hintLayoutFor(const QRect &shown) const
    {
        HintLayout layout;

        // 读数按这一轮那一档量：拖框时看到的宽高要与复制出去、敲回尺寸框的那一组数是同一个口径
        layout.hint = Core::formatSelectionHint(measuredRect(shown));
        if (m_aspectLocked)
        {
            // 锁着比例时在读数后面标出来：不标的话「拖不出想要的形状」会被当成手抖
            layout.hint += GyanisScribe::Base::uiText("（锁定 %1）").arg(Core::aspectRatioToText(m_aspectRatio));
        }
        if (m_cursorTouched && m_cursorIncluded)
        {
            // 翻过指针档就把当下那一档写进读数：带不带指针看不出来，比一律不带更糟
            layout.hint += *m_cursorIncluded ? GyanisScribe::Base::uiText("（含指针）") : GyanisScribe::Base::uiText("（不含指针）");
        }

        layout.font = font();
        // 字号以 12pt 打底；控件字体调大时跟着放大，否则提示会比旁边的正文还小
        layout.font.setPointSizeF(std::max<qreal>(GyanisScribe::Gui::kHintFontSize, font().pointSizeF()));
        // 尺寸提示里中文与数字同处一行：只上 tnum，避免中文回退被降重推到 Bold
        layout.font = tabularFont(layout.font, TextScript::Mixed);
        const QFontMetrics metrics(layout.font);

        const auto textSize    = metrics.size(Qt::TextSingleLine, layout.hint);
        const auto hintTopLeft = hintPositionFor(shown);
        layout.hintRect        = QRect{hintTopLeft, textSize + QSize(GyanisScribe::Gui::kHintPadding * 2, GyanisScribe::Gui::kHintPadding)};

        // 确认/取消怎么写常驻读数下一行：新用户拖完框最容易停在「然后呢」上
        layout.confirm     = m_isSelecting ? GyanisScribe::Base::uiText("松手即完成 · Esc 取消") : GyanisScribe::Base::uiText("拖四边调整 · Enter/双击确认 · Esc 取消");
        const auto confirmSize = metrics.size(Qt::TextSingleLine, layout.confirm);
        layout.confirmRect     = QRect{hintTopLeft + QPoint(0, textSize.height() + kCopiedHintGap), confirmSize + QSize(GyanisScribe::Gui::kHintPadding * 2, GyanisScribe::Gui::kHintPadding)};

        // 「已复制尺寸 …」贴在确认行下面一格；指针一动就清（见 mouseMoveEvent），
        // 不为它引一个定时器——那种反馈本来就不必跟秒表绑定
        if (!m_copiedSizeHint.isEmpty())
        {
            layout.copied         = m_copiedSizeHint;
            const auto copiedSize = metrics.size(Qt::TextSingleLine, m_copiedSizeHint);
            layout.copiedRect     = QRect{hintTopLeft + QPoint(0, textSize.height() + confirmSize.height() + 2 * kCopiedHintGap),
                                          copiedSize + QSize(GyanisScribe::Gui::kHintPadding * 2, GyanisScribe::Gui::kHintPadding)};
        }
        return layout;
    }

    void CaptureOverlay::paintEvent(QPaintEvent *event)
    {
        m_lastPaintRegion = event->region();

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
                for (const auto &line: Core::thirdsGridLinesFor(shown))
                {
                    painter.drawLine(line);
                }
            }

            // 提示区三层文字与位置跟局部重绘共用一次测量：两边各量一遍，字号或文案一变就会有一边算错
            const auto hint     = hintLayoutFor(shown);
            // 量尺寸时已锁过表格数字，绘制前再显式锁一次：同一属性重复应用不改变字体，绘制处也不依赖量尺寸那一侧
            QFont hintFont = hint.font;
            hintFont       = GyanisScribe::Gui::tabularFont(hintFont, TextScript::Mixed);
            painter.setFont(hintFont);
            painter.setPen(Qt::NoPen);
            painter.setBrush(GyanisScribe::Gui::kCaptureHintBackgroundColor);
            painter.drawRect(hint.hintRect);
            painter.setPen(GyanisScribe::Gui::kCaptureHintTextColor);
            painter.drawText(hint.hintRect, Qt::AlignCenter, hint.hint);

            painter.setPen(Qt::NoPen);
            painter.setBrush(GyanisScribe::Gui::kCaptureHintBackgroundColor);
            painter.drawRect(hint.confirmRect);
            painter.setPen(GyanisScribe::Gui::kCaptureHintTextColor);
            painter.drawText(hint.confirmRect, Qt::AlignCenter, hint.confirm);

            if (!hint.copiedRect.isEmpty())
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(GyanisScribe::Gui::kCaptureHintBackgroundColor);
                painter.drawRect(hint.copiedRect);
                painter.setPen(GyanisScribe::Gui::kCaptureHintTextColor);
                painter.drawText(hint.copiedRect, Qt::AlignCenter, hint.copied);
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

        // 模式横幅压在所有东西上面：它要说明「这块画面不是此刻的」，被选区或放大镜盖住就失去了用处
        if (!m_modeBannerRect.isEmpty() && !m_modeBanner.isEmpty())
        {
            QFont bannerFont = font();
            bannerFont.setPointSizeF(std::max<qreal>(GyanisScribe::Gui::kHintFontSize, font().pointSizeF()));
            bannerFont = GyanisScribe::Gui::tabularFont(bannerFont, TextScript::Mixed);
            painter.setFont(bannerFont);
            painter.setPen(Qt::NoPen);
            painter.setBrush(GyanisScribe::Gui::kCaptureHintBackgroundColor);
            painter.drawRect(m_modeBannerRect);
            painter.setPen(GyanisScribe::Gui::kCaptureHintTextColor);
            painter.drawText(m_modeBannerRect, Qt::AlignCenter, m_modeBanner);
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

        // 上一次按下的悬停候选到这里已经过期：这一次落在抓点上时松手不采纳它
        m_pressedHoveredRegion.reset();

        const auto point  = event->position().toPoint();
        const auto region = selectedRegion();
        // 已经框好的一块先认抓点：认到了就改这一块，而不是重新框一遍。
        // 差几像素要重框是框选里最常见的一次火，八个抓点就是为了不必重框
        if (!region.isNull())
        {
            if (const auto grip = Core::gripAt(region, point); grip != Core::SelectionGrip::None)
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
        // 按下那一刻亮着的候选留一份：拖动会把它从预览里清掉，小位移松手时还要按它采纳
        m_pressedHoveredRegion = m_hoveredRegion;
        // 重新框一块就不是「取自历史」了：下一次 R 该从最近一条重新起，而不是接着上一轮的位次
        m_historyIndex = -1;
        // 预览留着不熄：按下还没拖的那一瞬间，「点一下要哪扇」的答案就是它。
        // 真拖起来了再由 refreshHoveredRegion 关掉，同一时刻不会出现两个矩形
        update();
    }

    void CaptureOverlay::mouseMoveEvent(QMouseEvent *event)
    {
        // 先取改动前会变的那一片：移动会挪走放大镜与读数条，旧位置也必须一起刷
        const auto before = visualRegionForCurrentState();

        // 光标位置与选区端点分开记：没按下时也要重画（辅助线与悬停跟着光标走），
        // 但把它写进 m_currentPoint 会让 selectedRegion() 从原点编出一块根本没框过的选区
        m_cursorPoint    = event->position().toPoint();
        m_hasCursorPoint = true;
        m_copiedSizeHint.clear();

        // 悬停点报给服务去问界面元素：只在「没在框选、也还没有选区」时报，
        // 这两种状态下悬停预览本来就不显示，问了也是白花一次跨进程的费用。
        // 服务把连续几个点合并进同一个 40ms 窗口，新链不会在这一帧就回来：本次仍按手上的链重算，
        // 等 setElementChain 送到时再刷一次
        if (!m_isSelecting && !m_isAdjusting && selectedRegion().isNull())
        {
            emit hoverPointChanged(globalCursorPoint());
        }

        if (m_isAdjusting && m_draggedGrip)
        {
            const auto delta = m_cursorPoint - m_adjustPoint;
            m_adjustPoint    = m_cursorPoint;
            // 逐帧增量交给我这儿的几何函数夹：一次挪多长都不许越过对边，也不许出这块屏
            const auto moved = Core::dragRegionByGrip(selectedRegion(), *m_draggedGrip, delta, QRect{QPoint{0, 0}, size()});
            m_anchorPoint    = moved.topLeft();
            m_currentPoint   = moved.bottomRight();
            update(before | visualRegionForCurrentState());
            return;
        }

        if (m_isSelecting)
        {
            const auto limit = QRect{QPoint{0, 0}, size()};
            // Shift 临时锁正方形；没按 Shift 时看长宽比锁。两处收口都在纯函数里（见 ScreenUtils）：
            // 比例锁关着时 Core::ratioLockedPoint 原样交回光标点，与老行为逐像素同解
            m_currentPoint = event->modifiers().testFlag(Qt::ShiftModifier) ? Core::squareLockedPoint(m_anchorPoint, m_cursorPoint, true, limit)
                                                                            : Core::ratioLockedPoint(m_anchorPoint, m_cursorPoint, m_aspectLocked ? m_aspectRatio : QSize{}, limit);
        } else
        {
            // 没在拖的时候把光标形状交给抓点：形状变了就是「这里能抓」，不用试
            setCursor(Core::cursorForGrip(Core::gripAt(selectedRegion(), m_cursorPoint)));
        }
        refreshHoveredRegion();
        update(before | visualRegionForCurrentState());
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
            m_currentPoint = event->modifiers().testFlag(Qt::ShiftModifier) ? Core::squareLockedPoint(m_anchorPoint, releasePoint, true, limit)
                                                                            : Core::ratioLockedPoint(m_anchorPoint, releasePoint, m_aspectLocked ? m_aspectRatio : QSize{}, limit);

            // 按下又原地放开（没拖出任何一块）时，认此刻悬停亮着的那个候选：
            // 「点一下就要这一扇窗口」是框选最常用的走法，而原地放开过去什么也不产出。
            // 1-3 像素的手抖同样按原地点击算：宽高都在阈值内才是手抖，细长条是有意框的一条
            const auto drafted      = selectedRegion();
            const bool inPlaceClick = drafted.width() <= kHoverClickSlop && drafted.height() <= kHoverClickSlop;
            if (inPlaceClick && m_pressedHoveredRegion)
            {
                m_anchorPoint  = m_pressedHoveredRegion->topLeft();
                m_currentPoint = m_pressedHoveredRegion->bottomRight();
                m_hoveredRegion.reset();
                m_historyIndex = -1; // 采纳的是悬停候选，不是历史里那一条
            }
            m_pressedHoveredRegion.reset();
            update();
            // 松手即交出：选区范围在这一刻定下来，下一个界面就是标注窗；
            // 重裁边框在标注窗里用「选择」档拖四边完成
            confirmSelection();
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
                const auto changed = control ? Core::stretchRegionByEdge(region, *step, shift, limit)
                                             : (shift ? Core::resizeRegion(region, *step, limit) : Core::nudgeRegion(region, *step, limit));
                m_anchorPoint      = changed.topLeft();
                m_currentPoint     = changed.bottomRight();
                update();
            }
            return;
        }

        // 可换键的那几档排在按颗键判定的 switch 之前：它们认的是清册里现在那颗键，整颗组合相等才算命中。
        // 换上去的键与既有组合（如 Ctrl+A）撞上时这一层先赢——用户把它换上来就是要这颗键归这一档
        const auto action = rebindableActionFor(event);
        // 长按产生的自动重复不翻档：翻档是「按一下」的语义，键盘重复率会把它变成来回抽动
        if (event->isAutoRepeat() && (action == RebindableKeyAction::ToggleAspectRatio || action == RebindableKeyAction::ToggleCursor))
        {
            return;
        }
        switch (action)
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
                if (const auto next = Core::colorFormatAt((Core::indexOfColorFormat(m_readoutFormat) + 1) % static_cast<int>(Core::kColorFormatCount)))
                {
                    m_readoutFormat = *next;
                    update();
                }
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

    void CaptureOverlay::setModeBanner(const QString &text)
    {
        // 横幅上下一块也要重绘：换文案时旧底板不复用，只刷一次并集
        const auto before = visualRegionForCurrentState();
        m_modeBanner      = text;
        m_modeBannerRect  = modeBannerRectFor(text);
        update(before | visualRegionForCurrentState());
    }

    QString CaptureOverlay::modeBannerForTesting() const
    {
        return m_modeBanner;
    }

    QRect CaptureOverlay::modeBannerRectFor(const QString &text) const
    {
        if (text.isEmpty())
        {
            return {};
        }

        QFont bannerFont = font();
        // 与读数条同一档字号：横幅是给同一双眼睛看的，不该比旁边的数还小
        bannerFont.setPointSizeF(std::max<qreal>(GyanisScribe::Gui::kHintFontSize, font().pointSizeF()));
        bannerFont = tabularFont(bannerFont, TextScript::Mixed);
        const QFontMetrics metrics(bannerFont);
        const auto         textSize = metrics.size(Qt::TextSingleLine, text);
        return QRect{QPoint{kModeBannerMargin, kModeBannerMargin}, textSize + QSize(kModeBannerPaddingX * 2, kModeBannerPaddingY * 2)};
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
            m_sizeEditor = new Gui::LineEdit{this};
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
                GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("按{}填；这一档跟着设置里的「尺寸单位」走"), Core::sizeUnitLabel(m_sizeUnit).toStdString()).c_str()));

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
        // 越界的那一边由 Core::regionFromSizeText 夹回屏内——输错一个大数之后画面里只剩一小角，比夹住更让人摸不着头脑
        const auto next = Core::regionFromSizeText(m_sizeEditor->text(), anchor, limit, m_devicePixelRatio, m_sizeUnit);
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
            auto *key = dynamic_cast<QKeyEvent *>(event);
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

} // namespace GyanisScribe::Gui
