#include "Pin/PinWindow.h"

#include "Annotation/AnnotationEngine.h"
#include "Annotation/AnnotationToolbar.h"
#include "Annotation/AnnotationToolbarBridge.h"
#include "Annotation/AnnotationToolbarGeometry.h"
#include "Annotation/AnnotationView.h"
#include "Annotation/AnnotationWindow.h"
#include "Core/ImageFormat.h"
#include "Core/ClipboardFile.h"
#include "Core/Logger.h"
#include "Core/OutputDecoration.h"
#include "Core/Paths.h"
#include "Core/SizeText.h"
#include "Core/Text.h"
#include "Pin/PinLayout.h"
#include "Pin/PinViewFilter.h"
#include "Pick/ColorFormat.h"
#include "Ui/DesignTokens.h"
#include "Ui/IconProvider.h"
#include "Ui/KeyStep.h"
#include "Platform/IPlatformIntegration.h"
#include "Settings/ISettingsStore.h"
#include "Settings/NamingRules.h"
#include "Settings/OutputStyleRules.h"
#include "Settings/SettingsKeys.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFontMetrics>
#include <QMenu>
#include <QMimeData>
#include <QMovie>
#include <QImageReader>
#include <QInputDialog>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QKeyEvent>

#include <QGuiApplication>
#include <QScreen>
#include <QUrl>

#include <cmath>
#include <format>

namespace GyanisScribe::Pin
{
    void applyPinBatchAction(PinWindow &pin, const PinBatchAction action, const bool value)
    {
        switch (action)
        {
            case PinBatchAction::AlwaysOnTop:
                pin.setAlwaysOnTop(value);
                break;
            case PinBatchAction::MouseThrough:
                if (const auto applied = pin.setMouseThrough(value); !applied)
                {
                    // 没能力的平台上穿透这一项在菜单里根本不出现；走到这里仍是能力表与菜单不一致的那条兜底
                    Core::Logger::error(applied.error().message);
                }
                break;
            case PinBatchAction::Thumbnail:
                pin.setThumbnailMode(value);
                break;
            case PinBatchAction::Grayscale:
                pin.setGrayscale(value);
                break;
            case PinBatchAction::Inverted:
                pin.setInverted(value);
                break;
            case PinBatchAction::FlipHorizontal:
                pin.setMirroredHorizontally(value);
                break;
            case PinBatchAction::FlipVertical:
                pin.setMirroredVertically(value);
                break;
        }
    }

    namespace
    {
        /// 手柄边长，逻辑像素
        constexpr double kHandleSize = 10.0;

        /// 边缘改大小的起笔带，逻辑像素：离窗口四边这一格以内都算「按住边」，
        /// 不必正落在那枚小手柄上——那枚方块只有 10 格，对着小图几乎按不中
        constexpr double kEdgeResizeBand = 6.0;

        /// 滚轮一格的缩放倍率
        constexpr double kWheelScaleStep = 1.1;

        /// 滚轮一格转过的角度
        constexpr double kWheelRotationStep = 5.0;

        /// 滚轮一格改变的透明度
        constexpr double kWheelOpacityStep = 0.05;

        /// 键盘一挡转过的角度：滚轮那 5° 是细调，按一次 `R` 要的是「转正一首」
        constexpr double kKeyboardRotationStride = 90.0;

        /// 方向键按住 `Shift` 时一次挪多少格：与标注窗那档粗调同值（一格太慢，几十格会跳过目标）
        constexpr int kKeyboardNudgeStride = 10;

        /// 选中边框与悬停边框的描边宽度
        constexpr int kBorderWidth = 1;

        /// 选进多选集时的边框宽，逻辑像素：比悬停那一圈粗一档，一眼分得出这一组是哪几张
        constexpr int kSelectedBorderWidth = 2;

        /// 贴图的最小边长，逻辑像素：再小就找不到也拖不动
        constexpr double kMinimumEdge = 8.0;

        /// 手柄档对哪一个光标形状：八个档只有三种形状，两条对角各一枚
        Qt::CursorShape resizeCursorFor(const HandleKind handle) noexcept
        {
            switch (handle)
            {
                case HandleKind::TopLeft:
                case HandleKind::BottomRight:
                    return Qt::SizeFDiagCursor;
                case HandleKind::TopRight:
                case HandleKind::BottomLeft:
                    return Qt::SizeBDiagCursor;
                case HandleKind::Top:
                case HandleKind::Bottom:
                    return Qt::SizeVerCursor;
                case HandleKind::Left:
                case HandleKind::Right:
                    return Qt::SizeHorCursor;
            }
            return Qt::ArrowCursor; // 走到这里说明枚举加了新档没配形状
        }

        /// 改完大小之后窗口该摆在哪：抓哪条边就把对面那条钉住，没人抓的那一轴让中心原地不动
        QPoint anchoredTopLeft(const HandleKind handle, const QRect &before, const QRect &after)
        {
            const bool grabsLeft   = handle == HandleKind::Left || handle == HandleKind::TopLeft || handle == HandleKind::BottomLeft;
            const bool grabsRight  = handle == HandleKind::Right || handle == HandleKind::TopRight || handle == HandleKind::BottomRight;
            const bool grabsTop    = handle == HandleKind::Top || handle == HandleKind::TopLeft || handle == HandleKind::TopRight;
            const bool grabsBottom = handle == HandleKind::Bottom || handle == HandleKind::BottomLeft || handle == HandleKind::BottomRight;

            // 横向：抓的是左边就让那一条线跟着走（对面即右边界钉住），抓右边则左边界钉住；
            // QRect 的右边界含最后一格，所以对面那条线在 right()+1 上
            const int left = grabsLeft ? before.right() + 1 - after.width() : grabsRight ? before.left() : (before.left() + before.right() + 1 - after.width()) / 2;
            const int top  = grabsTop ? before.bottom() + 1 - after.height() : grabsBottom ? before.top() : (before.top() + before.bottom() + 1 - after.height()) / 2;
            return {left, top};
        }

        /// 取色读数框：色块边长、字号、内边距与离光标的偏移，都是逻辑像素
        constexpr double kColorReadoutSwatchEdge    = 16.0;
        constexpr int    kColorReadoutFontPointSize = 8;
        constexpr double kColorReadoutPadding       = 4.0;
        constexpr double kColorReadoutOffset        = 14.0;

        /// 按住 Shift 时标注微调一次挪多少格：与标注窗那一档同值
        constexpr int kAnnotateCoarseNudgeStride = 10;

        /// 裁剪框的最小边长，像素：比这还小的框多半是手抖点了一下，不当裁剪
        constexpr int kMinimumCropEdge = 2;

        /// 裁剪模式压暗框外的透明度（0-255）：压太黑看不清框外该不该留，太淡又看不出框在哪
        constexpr int kCropDimAlpha = 120;

        /// 拖放的粗判据：带文件、图像或文本就值得收下再看（细判在落下那一刻做）
        bool dragPayloadIsPinnable(const QMimeData *mime)
        {
            return mime != nullptr && (mime->hasUrls() || mime->hasImage() || mime->hasText());
        }

        /**
         * @brief 取保存贴图与截图的目录：配置里填了就用它，否则用系统图片目录下的那一个
         * @param settings 配置来源
         * @return 可直接写文件的目录
         */
        std::filesystem::path screenshotDirectory(const Settings::ISettingsStore &settings)
        {
            const auto configured = settings.stringValue(Settings::kPathScreenshot, QString());
            if (!configured.trimmed().isEmpty())
            {
                return Core::Paths::fromDisplayText(configured);
            }
            return Core::Paths::defaultScreenshotDirectory();
        }

        /**
         * @brief 读设置里那一档保存格式与质量
         * @param settings 配置来源
         * @return 成功返回「档位 + 质量」；配置里存了词表外的取值或越界的质量时返回那一格的回绝原因
         * @details 不替用户猜一档：猜错就是把成品按另一种压缩写出去，事后无从找回原样。
         *          PNG 是无损的，质量那一格对它不起作用，交给 Qt 自己忽略。
         *          交回的是档位而不是后缀：装饰那一步要知道这一档带不带透明通道（JPG 垫白）。
         */
        [[nodiscard]] Core::Expected<std::pair<Core::ImageFormat, int>> imageSaveTarget(const Settings::ISettingsStore &settings)
        {
            const auto stored = settings.stringValue(Settings::kSaveImageFormat, QStringLiteral("Png"));
            const auto format = Core::parseImageFormat(stored);
            if (!format)
            {
                return std::unexpected(format.error());
            }

            const auto quality = settings.intValue(Settings::kSaveImageQuality, Core::kDefaultImageQuality);
            if (const auto checked = Core::parseImageQuality(quality); !checked)
            {
                return std::unexpected(checked.error());
            }
            if (!Core::isImageFormatWritable(*format))
            {
                // 编码器没装这一档：说清是格式的问题，别让用户去查目录权限
                return std::unexpected(Core::Error{
                        Core::ErrorCode::PlatformUnsupported,
                        GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("这台机器的 Qt 写不出「{}」这一档保存格式。请在设置的截图页改选一档"), stored.toStdString())});
            }
            return std::pair{*format, quality};
        }
    } // namespace

    PinWindow::PinWindow(const QImage &image, Settings::ISettingsStore &settings, const Platform::IPlatformIntegration &integration, QWidget *parent) :
        QWidget(parent), m_settings(settings), m_integration(integration)
    {
        // 无边框 + 置顶 + Tool：Tool 让它不进任务栏，贴图是浮在屏幕上的东西，不该占一个窗口条目
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setMouseTracking(true); // 悬停要亮边框，不追踪就等不到进入事件
        setAttribute(Qt::WA_TranslucentBackground, false);
        // 收拖放：盘上的图片文件、链接与文本拖上来贴成新图（见 dragEnterEvent 与 dropEvent）
        setAcceptDrops(true);

        m_image       = image;
        m_logicalSize = QSizeF(image.width(), image.height());
        if (image.devicePixelRatio() > 1.0)
        {
            m_logicalSize = QSizeF(image.width() / image.devicePixelRatio(), image.height() / image.devicePixelRatio());
        }

        setWindowTitle(GyanisScribe::Core::uiText("智录贴图"));
        setAccessibleName(GyanisScribe::Core::uiText("贴图窗口"));

        // 起点不透明度取自配置。恢复布局那一条之后还会再覆盖一次，
        // 所以每张旧贴图各自的透明度不受这一条影响
        if (const auto parsed = parseOpacityPercentSetting(m_settings.intValue(Settings::kPinDefaultOpacity, kDefaultOpacityPercent)); parsed)
        {
            m_opacity = *parsed;
        } else
        {
            Core::Logger::warning(std::format("贴图默认不透明度读不出：{}，本次按完全不透明开始", parsed.error().message));
        }

        applyGeometry();
        setWindowOpacity(m_opacity);
    }

    PinWindow::~PinWindow() = default;

    void PinWindow::setScale(double scale)
    {
        m_scale = clampScale(scale);
        applyGeometry();
        update();
    }

    double PinWindow::scale() const noexcept
    {
        return m_scale;
    }

    void PinWindow::setExactSize(const QSize &size)
    {
        if (m_logicalSize.width() <= 0.0 || m_logicalSize.height() <= 0.0)
        {
            return;
        }

        // 保持长宽比、不拉伸：按宽算，宽没给才按高算。两维都超出缩放区间时由 setScale 夹住
        if (size.width() > 0)
        {
            setScale(size.width() / m_logicalSize.width());
        } else if (size.height() > 0)
        {
            setScale(size.height() / m_logicalSize.height());
        }
    }

    void PinWindow::setRotationDegrees(double degrees)
    {
        m_rotationDegrees = normalizeDegrees(degrees);
        applyGeometry();
        update();
    }

    double PinWindow::rotationDegrees() const noexcept
    {
        return m_rotationDegrees;
    }

    void PinWindow::setMirroredHorizontally(const bool mirrored)
    {
        m_mirroredHorizontally = mirrored;
        update();
    }

    bool PinWindow::mirroredHorizontally() const noexcept
    {
        return m_mirroredHorizontally;
    }

    void PinWindow::setMirroredVertically(const bool mirrored)
    {
        m_mirroredVertically = mirrored;
        update();
    }

    bool PinWindow::mirroredVertically() const noexcept
    {
        return m_mirroredVertically;
    }

    void PinWindow::setGrayscale(const bool grayscale)
    {
        m_grayscale    = grayscale;
        m_viewFiltered = {};
        update();
    }

    bool PinWindow::grayscale() const noexcept
    {
        return m_grayscale;
    }

    void PinWindow::setInverted(const bool inverted)
    {
        m_inverted     = inverted;
        m_viewFiltered = {};
        update();
    }

    bool PinWindow::inverted() const noexcept
    {
        return m_inverted;
    }

    bool PinWindow::isSelected() const noexcept
    {
        return m_selected;
    }

    void PinWindow::setSelected(const bool selected)
    {
        m_selected = selected;
        update();
    }

    void PinWindow::setBackgroundColor(const QColor &color)
    {
        m_backgroundColor = color;
        update();
    }

    QColor PinWindow::backgroundColor() const
    {
        return m_backgroundColor;
    }

    bool PinWindow::isSolo() const noexcept
    {
        return m_soloState;
    }

    void PinWindow::setSoloState(const bool solo)
    {
        m_soloState = solo;
    }

    int PinWindow::selectionCount() const noexcept
    {
        return m_selectionCount;
    }

    void PinWindow::setSelectionCount(const int count)
    {
        m_selectionCount = count;
    }

    const QImage &PinWindow::displayImage() const
    {
        if (!m_grayscale && !m_inverted)
        {
            return m_image;
        }

        // 缓存按「哪一版像素」认：动图每换一帧都会换掉 m_image，cacheKey 跟着变，这里自然重算
        if (m_viewFiltered.isNull() || m_viewFilteredSourceKey != m_image.cacheKey())
        {
            m_viewFiltered          = pinViewFiltered(m_image, m_grayscale, m_inverted);
            m_viewFilteredSourceKey = m_image.cacheKey();
        }
        return m_viewFiltered;
    }

    void PinWindow::setOpacityLevel(double opacity)
    {
        m_opacity = clampOpacity(opacity);
        setWindowOpacity(m_opacity);
    }

    double PinWindow::opacityLevel() const noexcept
    {
        return m_opacity;
    }

    Core::Expected<void> PinWindow::setMouseThrough(bool through)
    {
        if (through && !m_integration.capabilities().clickThrough)
        {
            return std::unexpected(Core::Error{
                    Core::ErrorCode::PlatformUnsupported,
                    GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("{}不支持鼠标穿透：这一项在本平台没有实现。请把贴图挪到不挡操作的位置，或降低透明度后从别处点击"),
                                                 m_integration.displayName().toStdString())});
        }

        // 穿透同时去掉焦点接受：吃不到鼠标却又抢走焦点，会让用户的键盘操作莫名失灵
        const auto opaque = windowFlags() & ~Qt::WindowTransparentForInput & ~Qt::NoDropShadowWindowHint;
        setWindowFlags(through ? (opaque | Qt::WindowTransparentForInput) : opaque);
        setFocusPolicy(through ? Qt::NoFocus : Qt::StrongFocus);
        m_mouseThrough = through;
        show(); // 改标志位后 Qt 要求重新映射窗口才生效
        return {};
    }

    bool PinWindow::isMouseThrough() const noexcept
    {
        return m_mouseThrough;
    }

    void PinWindow::setTransparentBackdrop(const TransparentBackdrop mode)
    {
        if (mode == m_transparentBackdrop)
        {
            // 重复设同一档不重映射窗口：那会看得见地闪一下，而什么都没变
            return;
        }
        const auto wasVisible = isVisible();
        // WA_TranslucentBackground 要在窗口未创建的状态下改才生效，与改标志位同一套做法
        if (wasVisible)
        {
            hide();
        }
        setAttribute(Qt::WA_TranslucentBackground, mode == TransparentBackdrop::SeeThrough);
        m_transparentBackdrop = mode;
        if (wasVisible)
        {
            show();
        }
        update();
    }

    TransparentBackdrop PinWindow::transparentBackdrop() const noexcept
    {
        return m_transparentBackdrop;
    }

    void PinWindow::setAlwaysOnTop(bool pinned)
    {
        m_pinned            = pinned;
        const auto unpinned = windowFlags() & ~Qt::WindowStaysOnTopHint;
        setWindowFlags(pinned ? (unpinned | Qt::WindowStaysOnTopHint) : unpinned);
        show(); // 同上：改标志要重新映射
    }

    bool PinWindow::isAlwaysOnTop() const noexcept
    {
        return m_pinned;
    }

    void PinWindow::setConcealed(const bool concealed)
    {
        if (concealed)
        {
            hide();
            return;
        }
        // 摆回时要 raise：不压回来的话它落在别的窗口后面，看起来就像没收回来
        show();
        raise();
    }

    bool PinWindow::isConcealed() const noexcept
    {
        return !isVisible();
    }

    ScreenEdge PinWindow::edgeDock() const noexcept
    {
        return m_edgeDock;
    }

    void PinWindow::setEdgeDocked(const bool docked)
    {
        if (!docked)
        {
            // 取消贴边 = 摆回贴上那条边、整张露出来；之后的拖动照旧，拖离屏边就自动脱开
            if (m_edgeDock != ScreenEdge::None)
            {
                m_dockCollapsed = false;
                applyDockGeometry();
            }
            m_edgeDock = ScreenEdge::None;
            update();
            return;
        }

        // 贴到离当前矩形最近的那条边：菜单那一项与拖着放上去走同一条落地路径
        const auto *screen = QGuiApplication::screenAt(QRectF(pos(), size()).center().toPoint());
        if (screen == nullptr)
        {
            return;
        }
        m_dockArea = screen->availableGeometry();

        const auto rect     = QRect(pos(), size());
        const auto toLeft   = std::abs(rect.left() - m_dockArea.left());
        const auto toRight  = std::abs(m_dockArea.right() - rect.right());
        const auto toTop    = std::abs(rect.top() - m_dockArea.top());
        const auto toBottom = std::abs(m_dockArea.bottom() - rect.bottom());
        auto       nearest  = ScreenEdge::Left;
        auto       best     = toLeft;
        if (toRight < best)
        {
            nearest = ScreenEdge::Right;
            best    = toRight;
        }
        if (toTop < best)
        {
            nearest = ScreenEdge::Top;
            best    = toTop;
        }
        if (toBottom < best)
        {
            nearest = ScreenEdge::Bottom;
        }

        m_edgeDock = nearest;
        // 菜单点「贴边隐藏」就当场收成一条：与拖着放上去的那一路不同——那一路松手时指针还在图上，先露着
        m_dockCollapsed = true;
        applyDockGeometry();
        update();
    }

    const QImage &PinWindow::image() const noexcept
    {
        return m_image;
    }

    void PinWindow::copyToClipboard()
    {
        // 剪贴板带得了透明通道：装饰按「不垫白」来（与标注窗那边同一条规矩）。
        // 这一路本来就静默：复制过没有从别处看得出来，不需要再补一句
        QApplication::clipboard()->setImage(decorateForOutput(m_image, false));
        Core::Logger::info(std::format("贴图已交剪贴板：{}×{} 像素", m_image.width(), m_image.height()));
    }

    void PinWindow::copyAsFileToClipboard()
    {
        // 落在缓存目录下一个专用子目录：这是给剪贴板用的中转文件，不往用户的图片目录里塞
        if (const auto written = Core::copyImageAsFile(m_image, Core::Paths::cacheDirectory() / "Clipboard"); !written)
        {
            Core::Logger::warning(std::format("复制为文件没成：{}", written.error().message));
        }
    }

    QImage PinWindow::decorateForOutput(const QImage &image, const bool fillBackground) const
    {
        const auto border = m_settings.boolValue(Settings::kOutputBorder, false);
        const auto shadow = m_settings.boolValue(Settings::kOutputShadow, false);
        // 宽与色从同一处读出来的一份参数：贴图保存这一路与标注窗那一头必须拿到同一档，
        // 否则同一张图从两处出去会带出两种边框
        return Core::decorateForOutput(image, border, shadow, fillBackground, Settings::readOutputDecorationStyle(m_settings));
    }

    Core::Expected<std::filesystem::path> PinWindow::saveWithDefaultName()
    {
        const auto target = imageSaveTarget(m_settings);
        if (!target)
        {
            // 格式或质量那一格存坏了：不写一个猜出来的文件，原因交回调用方
            return std::unexpected(target.error());
        }

        const auto directory = screenshotDirectory(m_settings);
        // 起名按设置里那一格的模板（留空即默认档），撞名时让一次位；
        // 管理器成批写缓存那一路不走这里，那一路按设计就是要覆盖
        const auto fileName = Core::Paths::imageFileName(QDateTime::currentDateTime(), Core::imageFormatExtension(target->first), Settings::nameTemplateOrDefault(m_settings));
        const auto unique   = Core::Paths::toDisplayText(Core::Paths::uniqueFilePath(directory, fileName).filename());
        // 右键「保存」是给用户拿走的成品：按外观设置装好饰再写。缓存与布局那一路走 saveToFile（原图），
        // 那些文件是给程序自己读回来的，装过饰下次恢复出来的贴图就多一圈边
        const auto decorated = decorateForOutput(m_image, !Core::imageFormatHasAlpha(target->first));
        if (const auto saved = writeFile(directory, unique, decorated, target->second); !saved)
        {
            return std::unexpected(saved.error());
        }
        return directory;
    }

    Core::Expected<QString> PinWindow::saveToFile(const std::filesystem::path &directory, const QString &fileName, const int quality)
    {
        return writeFile(directory, fileName, m_image, quality);
    }

    Core::Expected<QString> PinWindow::writeFile(const std::filesystem::path &directory, const QString &fileName, const QImage &contents, const int quality)
    {
        if (const auto ensured = Core::Paths::ensureDirectory(directory); !ensured)
        {
            return std::unexpected(ensured.error());
        }

        // 文件名归调用方：右键菜单那一路用 Core::Paths::imageFileName 起名（后缀按设置里那一档），
        // 布局那一路用序号。格式不单独传：Qt 按后缀挑编码器，再传一次格式名就是第二个出处
        const auto filePath = directory / Core::Paths::fromDisplayText(fileName);

        if (!contents.save(Core::Paths::toDisplayText(filePath), nullptr, quality))
        {
            return std::unexpected(Core::Error{Core::ErrorCode::FileWriteFailed,
                                               GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("贴图保存失败：{}。请检查目录是否可写，或先把这一张复制进剪贴板"),
                                                                            Core::Paths::toDisplayText(filePath).toStdString())});
        }

        Core::Logger::info(std::format("贴图已保存：{}", Core::Paths::toDisplayText(filePath).toStdString()));
        return Core::Paths::toDisplayText(filePath);
    }

    QRect PinWindow::globalRect() const
    {
        return QRect(mapToGlobal(QPoint(0, 0)), size());
    }

    QRect PinWindow::imageGlobalRect() const
    {
        // 与 applyGeometry 同一套算法：先按「图本身」算出窗口里居中的那块矩形，再换成全屏坐标。
        // 刻意不拿 globalRect() 去抓——那是旋转后的外接框（斜着时比图大一圈），照它抓会把邻居窗口的像素
        // 一起框进这张图；窗口为投影留的那一圈带子也不能算进来，那里面画的是桌面而不是内容
        const auto scaled = m_thumbnail ? QSizeF{m_thumbnailEdge, m_thumbnailEdge} : windowSizeFor(m_logicalSize, m_scale);
        const auto width  = std::max(kMinimumEdge, scaled.width());
        const auto height = std::max(kMinimumEdge, scaled.height());

        const QPointF center = QRectF(QPointF(0.0, 0.0), QSizeF(size())).center();
        const QRectF  local{center - QPointF{width / 2.0, height / 2.0}, QSizeF{width, height}};
        const QRect   aligned = local.toAlignedRect();
        return QRect(aligned.topLeft() + mapToGlobal(QPoint(0, 0)), aligned.size());
    }

    void PinWindow::setTemporarilyHidden(const bool hidden)
    {
        if (hidden)
        {
            // 记下藏之前它在不在桌上：收着的那一张（整桌收起、或在别的组）本来就不可见，
            // 摆回那一步要是无条件 show()，就把用户刻意收起来的东西摆回了桌面
            m_hiddenForGrab = isVisible();
            hide();
            return;
        }
        if (m_hiddenForGrab)
        {
            show();
            m_hiddenForGrab = false;
        }
    }

    PinState PinWindow::state() const
    {
        PinState state{globalRect().topLeft(), m_logicalSize, m_scale, m_rotationDegrees, m_opacity};
        state.group               = m_group;
        state.alwaysOnTop         = m_pinned;
        state.mouseThrough        = m_mouseThrough;
        state.transparentBackdrop = m_transparentBackdrop;
        state.sourceFile          = m_sourceFile;
        return state;
    }


    std::vector<QRectF> PinWindow::handleRects() const
    {
        return Pin::handleRects(contentRect(), kHandleSize);
    }

    std::optional<HandleKind> PinWindow::edgeHandleAt(const QPointF &localPoint) const
    {
        // 带子夹到最短那一边的三分之一以内：图小的时候四边的带子会叠到一起，
        // 整张都成了「改大小」就再没有平移的地方
        const auto band       = std::min(kEdgeResizeBand, std::min(width(), height()) / 3.0);
        const bool nearLeft   = localPoint.x() <= band;
        const bool nearRight  = localPoint.x() >= static_cast<double>(width()) - band;
        const bool nearTop    = localPoint.y() <= band;
        const bool nearBottom = localPoint.y() >= static_cast<double>(height()) - band;

        // 角先判：同时贴着两条边的落点要的是「两个方向一起给」，按边判会把它降成一维
        if (nearLeft && nearTop)
        {
            return HandleKind::TopLeft;
        }
        if (nearRight && nearTop)
        {
            return HandleKind::TopRight;
        }
        if (nearRight && nearBottom)
        {
            return HandleKind::BottomRight;
        }
        if (nearLeft && nearBottom)
        {
            return HandleKind::BottomLeft;
        }
        if (nearTop)
        {
            return HandleKind::Top;
        }
        if (nearRight)
        {
            return HandleKind::Right;
        }
        if (nearBottom)
        {
            return HandleKind::Bottom;
        }
        if (nearLeft)
        {
            return HandleKind::Left;
        }
        return {};
    }

    std::optional<HandleKind> PinWindow::resizeHandleAt(const QPointF &localPoint) const
    {
        const auto handles = handleRects();
        for (std::size_t index = 0; index < handles.size(); ++index)
        {
            if (handles[index].contains(localPoint))
            {
                return static_cast<HandleKind>(index);
            }
        }
        return edgeHandleAt(localPoint);
    }

    void PinWindow::applyHoverCursor(const QPointF &localPoint)
    {
        // 取色那一档的十字归 setColorPickMode 自己管：抢过来会让光标说「要改大小」而点下去仍是一个色
        if (m_colorPickMode)
        {
            return;
        }

        const auto handle = resizeHandleAt(localPoint);
        if (!handle)
        {
            // 只在已经压过形状时才收回：每次悬停移动都 unsetCursor 会让光标闪一下
            if (cursor().shape() != Qt::ArrowCursor)
            {
                unsetCursor();
            }
            return;
        }
        const auto shape = resizeCursorFor(*handle);
        if (cursor().shape() != shape)
        {
            setCursor(shape);
        }
    }

    void PinWindow::setThumbnailMode(const bool enabled)
    {
        if (m_thumbnail == enabled)
        {
            return;
        }
        m_thumbnail = enabled;
        // 位置保持中心不动：缩一下不该让用户重新找它在哪
        applyGeometry();
        update();
    }

    bool PinWindow::isThumbnailMode() const noexcept
    {
        return m_thumbnail;
    }

    void PinWindow::setThumbnailEdge(const double edge)
    {
        const auto clamped = clampThumbnailEdge(edge);
        if (clamped == m_thumbnailEdge)
        {
            return;
        }
        m_thumbnailEdge = clamped;
        // 只有缩着的时候这一格才看得见：正摊开的那张不该被方块的尺寸挪动分毫
        if (m_thumbnail)
        {
            applyGeometry();
        }
        update();
    }

    double PinWindow::thumbnailEdge() const noexcept
    {
        return m_thumbnailEdge;
    }

    void PinWindow::setGroup(const int group)
    {
        m_group = group < kFirstPinGroup ? kFirstPinGroup : group;
    }

    int PinWindow::group() const noexcept
    {
        return m_group;
    }

    void PinWindow::setGroupChoices(const QVector<int> &groups)
    {
        m_groupChoices = groups;
    }

    void PinWindow::setGroupNames(const QMap<int, QString> &names)
    {
        m_groupNames = names;
    }

    void PinWindow::setAnimatedSource(const QString &file)
    {
        m_sourceFile.clear();

        QImageReader reader(file);
        if (file.isEmpty() || !reader.supportsAnimation() || reader.imageCount() < 2)
        {
            // 静图（或读不出）：什么都不做，窗口照旧显示构造时交进来的那一张
            return;
        }

        m_movie = new QMovie(file, QByteArray(), this);
        if (!m_movie->isValid())
        {
            delete m_movie;
            m_movie = nullptr;
            Core::Logger::warning(std::format("动图读不出，这张贴图按静图放着：{}", file.toStdString()));
            return;
        }

        m_sourceFile = file;
        // 帧换了就换手里那张图：绘制、复制与保存读的都是 m_image，一处换全处跟着走
        QObject::connect(m_movie, &QMovie::frameChanged, this,
                         [this]
                         {
                             if (m_movie != nullptr)
                             {
                                 m_image = m_movie->currentImage();
                                 update();
                             }
                         });
        // QMovie 建出来时手里还没有帧：先跳到第 0 帧把它读出来，否则 currentImage() 是空的
        const auto firstFrame = m_movie->jumpToFrame(0) ? m_movie->currentImage() : QImage();
        if (!firstFrame.isNull())
        {
            m_image = firstFrame;
        }
    }

    bool PinWindow::isAnimated() const noexcept
    {
        return m_movie != nullptr;
    }

    bool PinWindow::isAnnotating() const noexcept
    {
        return m_annotating;
    }

    void PinWindow::startAnnotation()
    {
        if (m_annotating)
        {
            return;
        }
        if (m_movie != nullptr)
        {
            // 标注画在一帧上，下一帧就把成品盖掉了：拒绝并说清出路，而不是收下这一轮注定丢掉的改动
            Core::Logger::info("动图不进直接标注：标注会被下一帧盖掉。请先保存成静态图再贴一张来标");
            return;
        }

        // 缩略图态先摆回整张、取色模式先退出：这两种模式下都没有可画的坐标面
        setThumbnailMode(false);
        setColorPickMode(false);

        // 旋转与翻转过的贴图先摆正：标注视图是轴对齐且不镜像的，斜着或翻着的画布铺不平；
        // 退出时两样都恢复。成品记的是图片自己的像素，摆回去之后批注自然跟着图一起转/翻
        m_rotationBeforeAnnotate           = m_rotationDegrees;
        m_mirrorHorizontallyBeforeAnnotate = m_mirroredHorizontally;
        m_mirrorVerticallyBeforeAnnotate   = m_mirroredVertically;
        m_dockCollapsedBeforeAnnotate      = m_dockCollapsed;
        m_dockCollapsed                    = false;
        setRotationDegrees(0.0);
        setMirroredHorizontally(false);
        setMirroredVertically(false);
        applyDockGeometry();

        if (m_annotateEngine == nullptr)
        {
            m_annotateEngine = std::make_unique<Annotation::AnnotationEngine>();
        }
        Core::CapturedImage canvas;
        canvas.image            = m_image;
        canvas.devicePixelRatio = m_image.devicePixelRatio();
        canvas.logicalSize      = m_logicalSize.toSize();
        canvas.screenName       = QString();
        m_annotateEngine->loadCanvas(std::move(canvas));

        if (m_annotateView == nullptr)
        {
            m_annotateView = new Annotation::AnnotationView{*m_annotateEngine, this};
        }
        // 视图铺满整窗：此时旋转已归零，窗口框就是画布框
        m_annotateView->setGeometry(contentRect().toRect());
        m_annotateView->show();
        m_annotateView->raise();

        if (m_annotateToolbar == nullptr)
        {
            if (m_icons == nullptr)
            {
                m_icons = std::make_unique<Ui::IconProvider>();
            }
            m_annotateToolbar = new Annotation::AnnotationToolbar{m_settings, *m_icons, this};
            // 与标注窗同一条摆法：工具栏是独立浮窗，摆在贴图旁边
            m_annotateToolbar->setParent(this, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
            m_annotateBridge = std::make_unique<Annotation::AnnotationToolbarBridge>(*m_annotateToolbar, *m_annotateEngine, this);

            // 四个动作按钮都是「收工并做这件事」：与标注窗里「按下即交出去」同一条形状
            QObject::connect(m_annotateBridge.get(), &Annotation::AnnotationToolbarBridge::copyRequested, this,
                             [this]
                             {
                                 finishAnnotation();
                                 copyToClipboard();
                             });
            QObject::connect(m_annotateBridge.get(), &Annotation::AnnotationToolbarBridge::saveRequested, this,
                             [this]
                             {
                                 finishAnnotation();
                                 if (const auto saved = saveWithDefaultName(); !saved)
                                 {
                                     Core::Logger::error(saved.error().message);
                                 }
                             });
            QObject::connect(m_annotateBridge.get(), &Annotation::AnnotationToolbarBridge::pinRequested, this,
                             [this]
                             {
                                 finishAnnotation();
                                 emit pinAnotherRequested(m_image);
                             });
            QObject::connect(m_annotateBridge.get(), &Annotation::AnnotationToolbarBridge::ocrRequested, this,
                             [this]
                             {
                                 finishAnnotation();
                                 emit ocrRequested(m_image);
                             });
        }

        m_annotateToolbar->adjustSize();
        const auto *screen    = QGuiApplication::screenAt(globalRect().center());
        const auto  available = screen != nullptr ? screen->availableGeometry() : QRect{};
        if (const auto placed = Annotation::placeToolbar(available, m_annotateToolbar->sizeHint(), globalRect()); placed)
        {
            m_annotateToolbar->setGeometry(placed->frame);
        } else
        {
            // 与标注窗同一条兜底：摆不出来时别让工具栏消失
            Core::Logger::warning(std::format("标注工具栏摆放失败，改用贴图下沿兜底：{}", placed.error().message));
            m_annotateToolbar->move(globalRect().left(), globalRect().bottom() + 1);
        }
        m_annotateToolbar->show();
        m_annotateToolbar->raise();

        m_annotating = true;
        update();
        Core::Logger::info(std::format("贴图进入直接标注：{}×{} 逻辑像素", m_logicalSize.width(), m_logicalSize.height()));
    }

    void PinWindow::finishAnnotation()
    {
        if (!m_annotating)
        {
            return;
        }
        m_annotating = false;

        // 收工即烘焙：成品走引擎的导出（与标注窗保存同一条路），画布尺寸不变、逻辑尺寸照旧
        if (m_annotateEngine != nullptr)
        {
            const QImage baked = m_annotateEngine->exportImage();
            if (!baked.isNull())
            {
                m_image = baked;
            }
        }
        if (m_annotateView != nullptr)
        {
            m_annotateView->hide();
        }
        if (m_annotateToolbar != nullptr)
        {
            m_annotateToolbar->hide();
        }

        // 恢复进标注前的那一档：旋转角、翻转与贴边收起态
        m_dockCollapsed = m_dockCollapsedBeforeAnnotate;
        setRotationDegrees(m_rotationBeforeAnnotate);
        setMirroredHorizontally(m_mirrorHorizontallyBeforeAnnotate);
        setMirroredVertically(m_mirrorVerticallyBeforeAnnotate);
        applyDockGeometry();
        update();
    }

    bool PinWindow::isCropping() const noexcept
    {
        return m_cropping;
    }

    std::optional<QRectF> PinWindow::cropRect() const
    {
        if (!m_cropAnchor.has_value() || !m_cropCurrent.has_value())
        {
            return std::nullopt;
        }
        return QRectF(*m_cropAnchor, *m_cropCurrent).normalized();
    }

    void PinWindow::startCropping()
    {
        if (m_cropping || isAnimated())
        {
            return;
        }

        // 缩略图态先摆回整张、取色模式先退出：这两种模式下都没有可画的坐标面
        setThumbnailMode(false);
        setColorPickMode(false);

        // 裁剪框是轴对齐的：旋转与翻转先摆正（与进标注同一条规矩），退出时恢复
        m_rotationBeforeCrop           = m_rotationDegrees;
        m_mirrorHorizontallyBeforeCrop = m_mirroredHorizontally;
        m_mirrorVerticallyBeforeCrop   = m_mirroredVertically;
        setRotationDegrees(0.0);
        setMirroredHorizontally(false);
        setMirroredVertically(false);
        applyGeometry();

        m_cropping = true;
        m_cropAnchor.reset();
        m_cropCurrent.reset();
        setCursor(Qt::CrossCursor);
        update();
        Core::Logger::info(std::format("贴图进入裁剪：{}×{} 逻辑像素，拖一个框后回车裁到那一块", m_logicalSize.width(), m_logicalSize.height()));
    }

    void PinWindow::finishCropping(const bool commit)
    {
        if (!m_cropping)
        {
            return;
        }

        const auto rect = cropRect();
        m_cropping      = false;
        m_cropAnchor.reset();
        m_cropCurrent.reset();
        unsetCursor();

        if (commit && rect.has_value())
        {
            if (const auto pixels = imagePixelRect(*rect, contentRect(), m_image.size()); pixels.has_value())
            {
                if (pixels->width() >= kMinimumCropEdge && pixels->height() >= kMinimumCropEdge)
                {
                    m_image        = m_image.copy(*pixels);
                    m_logicalSize  = m_image.devicePixelRatio() > 0.0 ? QSizeF(m_image.width() / m_image.devicePixelRatio(), m_image.height() / m_image.devicePixelRatio())
                                                                      : QSizeF(m_image.size());
                    m_viewFiltered = {};
                    Core::Logger::info(std::format("贴图裁剪完成：留 {}×{} 像素", m_image.width(), m_image.height()));
                } else
                {
                    Core::Logger::info(std::format("这一框太小（{}×{} 像素），没有裁", pixels->width(), pixels->height()));
                }
            }
        }

        // 恢复进裁剪前的旋转与翻转：裁的是像素，摆回去之后用户看到的那一份还是他摆好的样子
        setRotationDegrees(m_rotationBeforeCrop);
        setMirroredHorizontally(m_mirrorHorizontallyBeforeCrop);
        setMirroredVertically(m_mirrorVerticallyBeforeCrop);
        applyGeometry();
        update();
    }

    void PinWindow::replaceContent(const QImage &image)
    {
        // 空图原样留着：拿不到内容时该由调用方说清原因，而不是把这一张贴图刷成空白
        if (image.isNull())
        {
            return;
        }

        m_image       = image;
        m_logicalSize = image.devicePixelRatio() > 0.0 ? QSizeF(image.width() / image.devicePixelRatio(), image.height() / image.devicePixelRatio()) : QSizeF(image.size());
        // 视图滤镜作废：灰度/反色那份镜像是照旧图算出来的，留着就会把旧像素的镜像盖在新内容上
        m_viewFiltered = {};
        // 摆位与缩放倍率都不动：换的是「这张图」，不是「这张贴图摆在哪儿」
        applyGeometry();
        update();
    }

    void PinWindow::routeAnnotationKey(QKeyEvent *event)
    {
        // 序号工具在用时的 `+`/`-`/`0`：与标注窗同一口径（改的是「下一个号」，不动画布）
        if (m_annotateEngine->activeTool() == Annotation::ToolKind::Number && event->modifiers() == Qt::NoModifier)
        {
            switch (event->key())
            {
                case Qt::Key_Plus:
                case Qt::Key_Equal:
                    if (const auto changed = m_annotateEngine->setNextNumber(m_annotateEngine->nextNumber() + 1); !changed)
                    {
                        Core::Logger::warning(std::format("改下一个序号没成：{}", changed.error().message));
                    }
                    return;
                case Qt::Key_Minus:
                    if (const auto changed = m_annotateEngine->setNextNumber(m_annotateEngine->nextNumber() - 1); !changed)
                    {
                        Core::Logger::warning(std::format("改下一个序号没成：{}", changed.error().message));
                    }
                    return;
                case Qt::Key_0:
                    m_annotateEngine->resetNumbering();
                    return;
                default:
                    break;
            }
        }

        const auto action = Annotation::shortcutActionFor(event->key(), event->modifiers());
        switch (action)
        {
            case Annotation::ShortcutAction::None:
                break; // 还可能是字母工具键，落到下面那张表
            case Annotation::ShortcutAction::Close:
                // Esc 在这里是收工保留：贴图本来就存在，丢弃它不是一个可用的选择
                finishAnnotation();
                return;
            case Annotation::ShortcutAction::Undo:
                if (auto undone = m_annotateEngine->undo(); !undone)
                {
                    Core::Logger::info(std::format("撤销没做成：{}", undone.error().message));
                }
                return;
            case Annotation::ShortcutAction::Redo:
                if (auto redone = m_annotateEngine->redo(); !redone)
                {
                    Core::Logger::info(std::format("重做没做成：{}", redone.error().message));
                }
                return;
            case Annotation::ShortcutAction::NudgeSelection:
            {
                const auto step = Ui::arrowStepFor(event->key());
                if (!step)
                {
                    return;
                }
                const auto stride = event->modifiers().testFlag(Qt::ShiftModifier) ? kAnnotateCoarseNudgeStride : 1;
                if (auto moved = m_annotateEngine->moveSelectionBy(*step * stride); !moved)
                {
                    Core::Logger::info(std::format("微调没做成：{}", moved.error().message));
                }
                return;
            }
            case Annotation::ShortcutAction::DeleteSelection:
                if (auto removed = m_annotateEngine->removeSelection(); !removed)
                {
                    Core::Logger::info(std::format("删除没做成：{}", removed.error().message));
                }
                return;
            case Annotation::ShortcutAction::CopyShape:
                if (m_annotateEngine->selections().empty())
                {
                    finishAnnotation();
                    copyToClipboard();
                    return;
                }
                if (auto copied = m_annotateEngine->copySelection(); !copied)
                {
                    Core::Logger::info(std::format("复制没做成：{}", copied.error().message));
                }
                return;
            case Annotation::ShortcutAction::PasteShape:
                if (auto pasted = m_annotateEngine->pasteClipboard(); !pasted)
                {
                    Core::Logger::info(std::format("粘贴没做成：{}", pasted.error().message));
                }
                return;
            case Annotation::ShortcutAction::RaiseSelection:
                if (auto raised = m_annotateEngine->raiseSelection(); !raised)
                {
                    Core::Logger::info(std::format("上移一层没做成：{}", raised.error().message));
                }
                return;
            case Annotation::ShortcutAction::LowerSelection:
                if (auto lowered = m_annotateEngine->lowerSelection(); !lowered)
                {
                    Core::Logger::info(std::format("下移一层没做成：{}", lowered.error().message));
                }
                return;
            case Annotation::ShortcutAction::BringToFront:
                if (auto fronted = m_annotateEngine->bringSelectionToFront(); !fronted)
                {
                    Core::Logger::info(std::format("置顶没做成：{}", fronted.error().message));
                }
                return;
            case Annotation::ShortcutAction::SendToBack:
                if (auto backed = m_annotateEngine->sendSelectionToBack(); !backed)
                {
                    Core::Logger::info(std::format("置底没做成：{}", backed.error().message));
                }
                return;
            case Annotation::ShortcutAction::Copy:
                // 与标注窗一致：回车=复制成品并收工；工具栏那个按钮走桥的 copyRequested，同一形状
                finishAnnotation();
                copyToClipboard();
                return;
            case Annotation::ShortcutAction::Save:
                finishAnnotation();
                if (const auto saved = saveWithDefaultName(); !saved)
                {
                    Core::Logger::error(saved.error().message);
                }
                return;
            case Annotation::ShortcutAction::Pin:
                finishAnnotation();
                emit pinAnotherRequested(m_image);
                return;
            case Annotation::ShortcutAction::Ocr:
                finishAnnotation();
                emit ocrRequested(m_image);
                return;
        }

        // 字母键切工具：与标注窗读的是同一份键位清册，切完回灌按钮态
        if (const auto tool = Annotation::toolShortcutFor(event->key(), event->modifiers(), m_settings); tool)
        {
            if (auto switched = m_annotateEngine->setActiveTool(*tool); switched)
            {
                m_annotateToolbar->setActiveTool(*tool);
            } else
            {
                Core::Logger::info(std::format("工具没换成：{}", switched.error().message));
            }
        }
    }

    void PinWindow::setColorPickMode(const bool enabled)
    {
        if (m_colorPickMode == enabled)
        {
            return;
        }
        m_colorPickMode = enabled;
        // 进来时把在拖的那一手松开：从手柄上按下时「改大小」与「读像素」两种语义会打架
        m_draggedHandle.reset();
        if (enabled)
        {
            setCursor(Qt::CrossCursor);
        } else
        {
            unsetCursor();
        }
        update();
    }

    bool PinWindow::isColorPickMode() const noexcept
    {
        return m_colorPickMode;
    }

    std::optional<QString> PinWindow::hoveredColorText() const
    {
        if (!m_colorPickMode)
        {
            return std::nullopt;
        }
        // 缩略图态不旋转、翻转照常：与绘制用的是同一个口径，不然读数会取到偏一格的颜色
        const auto pixel = imagePixelAt(m_pickPoint, contentRect(), m_image.size(), m_thumbnail ? 0.0 : m_rotationDegrees, m_mirroredHorizontally, m_mirroredVertically);
        if (!pixel)
        {
            return std::nullopt;
        }

        std::string complaint;
        const auto  format = Pick::colorFormatOrDefault(m_settings.stringValue(Settings::kPickFormat, QStringLiteral("Hex")).toStdString(), complaint);
        if (!complaint.empty())
        {
            // 读数不因坏配置停摆：按 HEX 顶上，原因照旧只进日志（同一句在界面那次取色时会弹给用户）
            Core::Logger::warning(std::format("取色写法读不出：{}，贴图取色按 HEX 顶上", complaint));
        }
        // 读的是显示用的那一份：开了灰度/反色之后，用户看到的就是这个颜色
        return Pick::formatColor(displayImage().pixelColor(*pixel), format);
    }

    void PinWindow::setShadowEnabled(const bool enabled)
    {
        if (m_shadowEnabled == enabled)
        {
            return;
        }
        m_shadowEnabled = enabled;
        applyGeometry();
        update();
    }

    bool PinWindow::shadowEnabled() const noexcept
    {
        return m_shadowEnabled;
    }

    void PinWindow::setSmoothScaleEnabled(const bool enabled)
    {
        // 只重画，不动几何：这一档改的是像素怎么铺大，不是这张贴图有多大、摆在哪儿
        if (m_smoothScaleEnabled == enabled)
        {
            return;
        }
        m_smoothScaleEnabled = enabled;
        update();
    }

    bool PinWindow::smoothScaleEnabled() const noexcept
    {
        return m_smoothScaleEnabled;
    }

    void PinWindow::setSnapCandidateProvider(std::function<std::vector<QRect>()> provider)
    {
        m_snapCandidates = std::move(provider);
    }

    void PinWindow::applyGeometry()
    {
        const auto scaled   = m_thumbnail ? QSizeF{m_thumbnailEdge, m_thumbnailEdge} : windowSizeFor(m_logicalSize, m_scale);
        const auto rotation = m_thumbnail ? 0.0 : m_rotationDegrees;
        const auto width    = std::max(kMinimumEdge, scaled.width());
        const auto height   = std::max(kMinimumEdge, scaled.height());

        const QPointF center = QRectF(pos(), size()).center();
        const auto    box    = rotatedBoundingBox(center, QSizeF(width, height), rotation);

        // 窗口阴影：窗口比外接框每边大一圈，投影画在带子里；内容在窗口里居中（见 contentRect），
        // 于是留边出现或消失时内容一个像素都不动。缩略图态不铺——那枚小方块是临时让位用的
        const bool   shadowOn = m_shadowEnabled && !m_thumbnail;
        const QRectF padded   = shadowOn ? box.adjusted(-kShadowMargin, -kShadowMargin, kShadowMargin, kShadowMargin) : box;

        // 尺寸取外接框（有阴影时再套上留边），绘制时按同样的角度转回去：命中、拖动与手柄都落在这个轴对齐框里算
        setGeometry(padded.toAlignedRect());

        // 贴边的那张尺寸变了（缩放、旋转、缩略图）还要按同一条边重排：不然它会被居中算法拉回屏里，
        // 变成一张「贴过边但已经不在边上」的图，收起与展开也没有了依据
        applyDockGeometry();
    }

    void PinWindow::applyDockGeometry()
    {
        if (m_edgeDock == ScreenEdge::None || m_dockArea.isEmpty())
        {
            return;
        }

        // 露出时的目标矩形：按当前尺寸贴齐那条边，另一轴保持现在的中心
        auto revealed = QRect(pos(), size());
        switch (m_edgeDock)
        {
            case ScreenEdge::Left:
                revealed.moveLeft(m_dockArea.left());
                break;
            case ScreenEdge::Right:
                revealed.moveLeft(m_dockArea.right() - revealed.width() + 1);
                break;
            case ScreenEdge::Top:
                revealed.moveTop(m_dockArea.top());
                break;
            case ScreenEdge::Bottom:
                revealed.moveTop(m_dockArea.bottom() - revealed.height() + 1);
                break;
            case ScreenEdge::None:
                return;
        }
        setGeometry(dockedRect(revealed, m_dockArea, m_edgeDock, m_dockCollapsed));
    }

    QRectF PinWindow::contentRect() const
    {
        const auto   scaled = m_thumbnail ? QSizeF{m_thumbnailEdge, m_thumbnailEdge} : windowSizeFor(m_logicalSize, m_scale);
        const double width  = std::max(kMinimumEdge, scaled.width());
        const double height = std::max(kMinimumEdge, scaled.height());

        // 画布居中在窗口框里：窗口框是旋转后的外接框，两者中心重合时旋转才不会把图甩出窗口
        const QPointF windowCenter = QRectF(QPointF(0.0, 0.0), QSizeF(size())).center();
        return QRectF(windowCenter - QPointF(width / 2.0, height / 2.0), QSizeF(width, height));
    }

    void PinWindow::paintEvent(QPaintEvent *event)
    {
        Q_UNUSED(event);

        QPainter painter(this);
        // 平滑缩放跟着这一张的档位走：关着时放大画的是硬边像素格，读截图里的小字要的就是这一档
        painter.setRenderHint(QPainter::SmoothPixmapTransform, m_smoothScaleEnabled);

        const auto content = contentRect();
        painter.translate(content.center());
        // 缩略图态不旋转：一枚小方块里转起来看不清是什么，退出后旋转照旧。翻转不改外框，照常生效
        painter.rotate(m_thumbnail ? 0.0 : m_rotationDegrees);
        // 翻转在旋转之前做：转的是翻好的那一份，与 imagePixelAt 的反变换成对（先反旋转、后反翻转）
        painter.scale(m_mirroredHorizontally ? -1.0 : 1.0, m_mirroredVertically ? -1.0 : 1.0);
        painter.translate(-content.center());

        // 窗口阴影：几圈递减的描边叠出一圈暗晕，贴着内容四外画在留边带子里。排在白板与图像之前——
        // 它是「垫在底下」的那一层，跟着同一套转与翻走
        if (m_shadowEnabled && !m_thumbnail)
        {
            painter.setBrush(Qt::NoBrush);
            for (int layer = 1; layer <= kShadowLayers; ++layer)
            {
                QColor shadow{0, 0, 0};
                // 贴着内容那一圈最深、往外一格格淡下去：常数越小越像「远处的一圈光晕」
                shadow.setAlpha(8 + 32 * (kShadowLayers - layer) / kShadowLayers);
                QPen pen{shadow};
                pen.setWidth(2);
                painter.setPen(pen);
                painter.drawRect(content.adjusted(-layer, -layer, layer, layer));
            }
        }
        // 白板：垫在图的后面，透明像素处露出来的就是它；画在同一个坐标系里，跟着转与翻
        if (m_backgroundColor.isValid())
        {
            painter.fillRect(content, m_backgroundColor);
        }
        // 透明区两档棋盘：画在底板之后、图之前，于是只有图上是透明的地方才看得见它
        if (const auto checker = checkerColorsFor(m_transparentBackdrop); checker)
        {
            painter.fillRect(content, QBrush{checkerboardTile(checker->first, checker->second, kCheckerCellPixels)});
        }
        painter.drawImage(content, displayImage(), QRectF(QPointF(0.0, 0.0), QSizeF(m_image.width(), m_image.height())));
        painter.resetTransform();

        // 悬停、拖手柄或选进多选集才画边框，平时一张干净的图压在屏幕上不该有装饰；
        // 选中的那一圈加粗一档：多选之后一眼看得出这一组是哪几张
        const bool decorated = underMouse() || m_draggedHandle.has_value() || m_selected;
        if (decorated)
        {
            auto border = QPen(GyanisScribe::Ui::kAccentColor);
            border.setWidth(m_selected ? kSelectedBorderWidth : kBorderWidth);
            painter.setPen(border);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(rect().adjusted(0, 0, -1, -1));

            for (const auto &handle: handleRects())
            {
                painter.setBrush(QColor(255, 255, 255));
                painter.drawRect(handle);
            }
        }

        if (m_colorPickMode)
        {
            paintColorReadout(painter);
        }

        // 裁剪模式：框外压一层暗，留下的那一块亮着；框本身用虚线圈出来
        if (m_cropping)
        {
            const auto   box = cropRect();
            QPainterPath outside;
            outside.addRect(QRectF(rect()));
            if (box.has_value())
            {
                outside.addRect(*box);
            }
            // 两条矩形走奇偶填充：框内那一条被挖掉，压暗只落在框外
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, kCropDimAlpha));
            painter.drawPath(outside);

            if (box.has_value())
            {
                QPen cropPen(GyanisScribe::Ui::kAccentColor, 1.0, Qt::DashLine);
                cropPen.setCapStyle(Qt::FlatCap);
                painter.setPen(cropPen);
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(*box);
            }
        }
    }

    void PinWindow::paintColorReadout(QPainter &painter)
    {
        const auto text  = hoveredColorText();
        const auto pixel = imagePixelAt(m_pickPoint, contentRect(), m_image.size(), m_thumbnail ? 0.0 : m_rotationDegrees, m_mirroredHorizontally, m_mirroredVertically);
        if (!text || !pixel)
        {
            return;
        }

        const QColor swatch = displayImage().pixelColor(*pixel);

        // 读数框：一块色块加那串写法。底色与字色固定成深底浅字——读数要压在任意内容上都看得清，
        // 跟着主题走会在浅色主题里与截图里的浅色糊在一起
        const int swatchEdge = kColorReadoutSwatchEdge;
        QFont     font       = painter.font();
        font.setPointSize(kColorReadoutFontPointSize);
        const QFontMetrics metrics(font);
        const int          textWidth = metrics.horizontalAdvance(*text);

        QRectF box(m_pickPoint + QPointF(kColorReadoutOffset, kColorReadoutOffset),
                   QSizeF(kColorReadoutPadding * 2 + swatchEdge + kColorReadoutPadding + textWidth, kColorReadoutPadding * 2 + std::max(swatchEdge, metrics.height())));
        // 贴到窗口边上时把框翻到光标另一侧：读数不该被窗口边界裁掉一半
        if (box.right() > rect().right())
        {
            box.moveLeft(m_pickPoint.x() - kColorReadoutOffset - box.width());
        }
        if (box.bottom() > rect().bottom())
        {
            box.moveTop(m_pickPoint.y() - kColorReadoutOffset - box.height());
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 200));
        painter.drawRoundedRect(box, 3.0, 3.0);

        const QRectF swatchRect(box.left() + kColorReadoutPadding, box.center().y() - swatchEdge / 2.0, swatchEdge, swatchEdge);
        painter.setBrush(swatch);
        painter.setPen(QPen(QColor(255, 255, 255, 120)));
        painter.drawRect(swatchRect);

        painter.setPen(QColor(255, 255, 255));
        painter.setFont(font);
        painter.drawText(QRectF(swatchRect.right() + kColorReadoutPadding, box.top(), textWidth, box.height()), Qt::AlignVCenter | Qt::AlignLeft, *text);
    }

    void PinWindow::mousePressEvent(QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton)
        {
            return;
        }

        // 标注模式：鼠标归标注视图（它铺满整窗收事件），窗口这一层什么都不做
        if (m_annotating)
        {
            return;
        }

        // 裁剪模式：左键起一笔新的裁剪框（进这一档时取色与缩略图都已经退出）
        if (m_cropping)
        {
            m_cropAnchor  = event->position();
            m_cropCurrent = event->position();
            update();
            return;
        }

        // Ctrl+左键：切进/切出多选集（不拖动）。选中集是管理器的账，这里只报意图
        if (event->modifiers().testFlag(Qt::ControlModifier))
        {
            emit selectionToggleRequested();
            return;
        }

        // 取色模式：点一下就把那一像素的颜色交出去，点完自己退出——与屏幕取色那边「点一下拿一个色」同一口径
        if (m_colorPickMode)
        {
            const auto pixel = imagePixelAt(event->position(), contentRect(), m_image.size(), m_thumbnail ? 0.0 : m_rotationDegrees, m_mirroredHorizontally, m_mirroredVertically);
            if (pixel)
            {
                // 交出去的是显示色：开了灰度/反色之后，用户挑中的就是屏上那一个
                emit colorPicked(displayImage().pixelColor(*pixel));
                setColorPickMode(false);
            }
            return;
        }

        m_dragMoved = false;
        // 起笔时取一次参照快照：整趟拖动都用它，每帧重新枚举窗口会把鼠标移动拖慢
        m_dragCandidates = m_snapCandidates ? m_snapCandidates() : std::vector<QRect>{};

        const auto point = event->position();
        // 起笔点先认手柄与边带：碰上了就是改大小，没碰上才是整张平移
        if (const auto handle = resizeHandleAt(point); handle)
        {
            m_draggedHandle       = *handle;
            m_dragInitialScale    = m_scale;
            m_dragInitialEdge     = m_thumbnailEdge;
            m_dragInitialDistance = std::hypot(point.x() - rect().center().x(), point.y() - rect().center().y());
            return;
        }

        m_dragOffset = event->globalPosition().toPoint() - pos();
    }

    void PinWindow::mouseMoveEvent(QMouseEvent *event)
    {
        if (m_annotating)
        {
            return;
        }

        // 裁剪模式：拖的是那一框（按下时已经起笔）
        if (m_cropping)
        {
            if (m_cropAnchor.has_value())
            {
                m_cropCurrent = event->position();
                update();
            }
            return;
        }

        // 取色模式：鼠标只读像素，不拖窗也不缩图——读数框跟着光标走
        if (m_colorPickMode)
        {
            m_pickPoint = event->position();
            update();
            return;
        }

        if (m_draggedHandle)
        {
            const auto point    = event->position();
            const auto distance = std::hypot(point.x() - rect().center().x(), point.y() - rect().center().y());
            const auto before   = QRect(pos(), size());
            // 缩着的那一枚改的是方块边长，摊开的那张改的是缩放：同一只手感（按到中心的距离给倍率），落到各自的量上
            if (m_thumbnail)
            {
                setThumbnailEdge(thumbnailEdgeFromDrag(m_dragInitialEdge, m_dragInitialDistance, distance));
            } else
            {
                setScale(scaleFromDrag(m_dragInitialScale, m_dragInitialDistance, distance));
            }
            // applyGeometry 是按中心重算尺寸的；这里把对面那条边钉回原位——按住右边往外拖时
            // 左边不该一起跑，那看着像整张在漂而不是在改大小
            move(anchoredTopLeft(*m_draggedHandle, before, QRect(pos(), size())));
            return;
        }

        if (event->buttons().testFlag(Qt::LeftButton))
        {
            m_dragMoved = true;
            auto target = event->globalPosition().toPoint() - m_dragOffset;
            // 按住 Shift 拖动 = 把这一张对到桌上别的贴图或屏上窗口：差一格就松手也能齐，
            // 不按住时一律不吸——自由摆放的手感不能被参照抢走
            if (event->modifiers().testFlag(Qt::ShiftModifier) && !m_dragCandidates.empty())
            {
                target = snapToCandidateEdges(QRect(target, size()), m_dragCandidates).topLeft();
            }
            const QPoint delta = target - pos();
            move(target);
            // 选中集里其余几张按同一位移跟着挪：本张已经挪过，管理器只动其余的
            emit draggedWithSelection(delta);
        } else if (event->buttons() == Qt::NoButton)
        {
            // 没有按键的移动就是悬停：落点挨着哪一条边，光标就换成那一条边的改大小形状——
            // 不换光标没人知道这四条边是可以拖的，那八枚小手柄太细，对着小图基本按不中
            applyHoverCursor(event->position());
        }
    }

    void PinWindow::mouseReleaseEvent(QMouseEvent *event)
    {
        Q_UNUSED(event);
        if (m_annotating)
        {
            return;
        }

        // 这一趟拖动的参照到此为止：下一次起笔重新取快照，别拿上一趟的位置当参照
        m_dragCandidates.clear();

        if (m_cropping)
        {
            if (m_cropAnchor.has_value())
            {
                m_cropCurrent = event->position();
                update();
            }
            return;
        }
        if (m_draggedHandle)
        {
            m_draggedHandle.reset();
            update();
            return;
        }

        // 只是点了一下（没拖动）不吸边：在屏边附近点一下就把图收起来是没人要过的行为
        if (!m_dragMoved)
        {
            update();
            return;
        }

        // 拖完落在屏边附近就贴上去：贴边那一刻记下这块屏的可用区，之后的收起与展开都按它算，
        // 免得收起后再去问「窗口在哪块屏」——那时它大部分已经在屏外了
        const auto *screen = QGuiApplication::screenAt(QRectF(pos(), size()).center().toPoint());
        if (screen != nullptr)
        {
            const auto snapped = snapToScreenEdges(QRect(pos(), size()), screen->availableGeometry());
            if (snapped.edge != ScreenEdge::None)
            {
                m_dockArea = screen->availableGeometry();
                m_edgeDock = snapped.edge;
                // 松手那一刻指针还在这张图上：先露着，等指针离开再收回去（见 leaveEvent）
                m_dockCollapsed = false;
                setGeometry(dockedRect(snapped.rect, m_dockArea, m_edgeDock, false));
                update();
                return;
            }
            m_edgeDock      = ScreenEdge::None;
            m_dockCollapsed = false;
        }

        update();
    }

    void PinWindow::enterEvent(QEnterEvent *event)
    {
        QWidget::enterEvent(event);
        // 贴边收着的时候悬停要露出来：收成一条之后能接到的交互只有那条边
        if (m_edgeDock != ScreenEdge::None && m_dockCollapsed)
        {
            m_dockCollapsed = false;
            applyDockGeometry();
        }
    }

    void PinWindow::leaveEvent(QEvent *event)
    {
        QWidget::leaveEvent(event);
        // 悬停时压上的改大小形状跟着收回去：不然下次进来还没移动，第一眼仍是「这条边能拖」
        if (!m_colorPickMode)
        {
            unsetCursor();
        }
        // 指针走了就收回去：贴边这一档本来就是「别占着地方」；标注中不收——
        // 光标去点工具栏就是一次离开窗口，把贴图收走等于把正在画的这张纸抽掉
        if (m_edgeDock != ScreenEdge::None && !m_dockCollapsed && !m_annotating)
        {
            m_dockCollapsed = true;
            applyDockGeometry();
        }
    }

    void PinWindow::showEvent(QShowEvent *event)
    {
        QWidget::showEvent(event);
        if (m_movie != nullptr)
        {
            m_movie->start();
        }
    }

    void PinWindow::hideEvent(QHideEvent *event)
    {
        QWidget::hideEvent(event);
        // 收起或关闭发生在标注中途时先收工：已经画上的东西烘进图里，不留一个悬着的会话
        if (m_annotating)
        {
            finishAnnotation();
        }
        // 停而不是暂停：藏起来的贴图再过一会儿多半也不会被看到，让它一直记着帧反而是白烧 CPU
        if (m_movie != nullptr)
        {
            m_movie->stop();
        }
    }

    void PinWindow::wheelEvent(QWheelEvent *event)
    {
        // 标注模式：滚轮不缩不转——那两种手势会把视图铺着的坐标面改掉
        if (m_annotating)
        {
            return;
        }

        const auto steps = event->angleDelta().y() / 120.0; // 一格 120 度，允许半格（精密滚轮）
        if (steps == 0.0)
        {
            return;
        }

        if (event->modifiers().testFlag(Qt::ControlModifier))
        {
            setRotationDegrees(m_rotationDegrees + steps * kWheelRotationStep);
            return;
        }
        if (event->modifiers().testFlag(Qt::ShiftModifier))
        {
            setOpacityLevel(m_opacity + steps * kWheelOpacityStep);
            return;
        }

        // 不加修饰键就是缩放：按倍率乘除，滚轮一格 1.1 倍。选中集里其余几张按同一倍率跟着缩
        const double factor = steps > 0.0 ? kWheelScaleStep : 1.0 / kWheelScaleStep;
        setScale(m_scale * factor);
        emit scaledWithSelection(factor);
    }

    void PinWindow::keyPressEvent(QKeyEvent *event)
    {
        // 标注模式：键盘归标注那一套（与标注窗同一张表）；路由之后一律吞掉，
        // 不能漏到下面的贴图键上——按 T 把正在标的图缩成小方块不是任何人想要的
        if (m_annotating)
        {
            routeAnnotationKey(event);
            return;
        }

        // 裁剪模式：回车（或双击）裁到框里，Esc 原样退出；别的键一律不认——
        // 这时候按 T/R/字母都是手滑，不该改掉这张图的别的状态
        if (m_cropping)
        {
            if (event->key() == Qt::Key_Escape)
            {
                finishCropping(false);
            } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            {
                finishCropping(true);
            }
            return;
        }

        if (event->key() == Qt::Key_Escape)
        {
            // 取色模式里 Esc 先退出取色：用户想反悔的那一下多半是「不取了」，
            // 而不是把这张贴图也关掉（那是撤销不回的动作）
            if (m_colorPickMode)
            {
                setColorPickMode(false);
                return;
            }
            // 多选里 Esc 先清选择（关闭是一组一起的动作，得再按一次才落）
            if (m_selected && m_selectionCount > 1)
            {
                emit clearSelectionRequested();
                return;
            }
            // 先自己收场再报出去：管理器收到之后会把这张（与选中集里其余几张）从册上摘掉、
            // 当场析构；反过来先报再收场，回调回来时 this 已经没了
            close();
            emit closedByUser();
            return;
        }

        // 裸按 `T` 缩到小图 / 还原：与右键菜单那一项同一条路
        if (event->key() == Qt::Key_T && event->modifiers() == Qt::NoModifier)
        {
            setThumbnailMode(!m_thumbnail);
            return;
        }

        // `R` 顺转一挡、`Shift+R` 逆转一挡：带 Ctrl 的同一个字母不认，那是另一类组合键的地盘
        if (event->key() == Qt::Key_R && (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier))
        {
            const auto stride = event->modifiers().testFlag(Qt::ShiftModifier) ? -kKeyboardRotationStride : kKeyboardRotationStride;
            setRotationDegrees(m_rotationDegrees + stride);
            return;
        }

        if (event->key() == Qt::Key_0 && event->modifiers() == Qt::NoModifier)
        {
            resetScaleAndRotation();
            return;
        }

        // `Ctrl+A` 把桌上看得见的贴图一起选进多选集：谁在屏上是管理器的账，这一层只报意图
        if (event->key() == Qt::Key_A && event->modifiers() == Qt::ControlModifier)
        {
            emit selectAllRequested();
            return;
        }

        // `F5` 重抓这块屏幕：按住不放由键盘的自动重复连着报，于是能盯着实时画面挑一帧，松手定在最后那一帧
        if (event->key() == Qt::Key_F5 && event->modifiers() == Qt::NoModifier)
        {
            emit refreshRequested();
            return;
        }

        // `Ctrl+V` 拿剪贴板里那一份换掉这一张：裸 `V` 是上下翻转，两者共用一个字母，靠修饰键分开
        if (event->key() == Qt::Key_V && event->modifiers() == Qt::ControlModifier)
        {
            emit clipboardReplaceRequested();
            return;
        }

        // 方向键把这一张逐像素挪走：一格、`Shift` 十格——遮罩与标注窗早就是这个口径，
        // 贴图这一头原先只能用鼠标拖，贴着别的窗对边线时差的那几格摸不出来
        if (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier)
        {
            if (const auto step = Ui::arrowStepFor(event->key()); step)
            {
                const int    stride = event->modifiers().testFlag(Qt::ShiftModifier) ? kKeyboardNudgeStride : 1;
                const QPoint delta{step->x() * stride, step->y() * stride};
                move(pos() + delta);
                // 与鼠标拖动同一只信号：这一张已经挪过，选中的其余那几张由管理器按同一位移跟上
                emit draggedWithSelection(delta);
                return;
            }
        }

        // `H`/`V` 翻转、`G` 灰度、`I` 反色：都是视图变换，再按一次原样退回
        if (event->modifiers() == Qt::NoModifier)
        {
            switch (event->key())
            {
                case Qt::Key_H:
                    setMirroredHorizontally(!m_mirroredHorizontally);
                    return;
                case Qt::Key_V:
                    setMirroredVertically(!m_mirroredVertically);
                    return;
                case Qt::Key_G:
                    setGrayscale(!m_grayscale);
                    return;
                case Qt::Key_I:
                    setInverted(!m_inverted);
                    return;
                default:
                    break;
            }
        }

        QWidget::keyPressEvent(event);
    }

    void PinWindow::mouseDoubleClickEvent(QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton)
        {
            QWidget::mouseDoubleClickEvent(event);
            return;
        }

        // 标注模式：双击归标注视图（重编文字那一路），窗口这一层不抢
        if (m_annotating)
        {
            return;
        }

        // 裁剪模式：双击就是「就这一框」——与回车同一条路
        if (m_cropping)
        {
            finishCropping(true);
            return;
        }

        // 左键双击同样回到原始比例与角度：已经在 1:1 且没转角时这是幂等，而不是「按了没反应」
        resetScaleAndRotation();
    }

    void PinWindow::resetScaleAndRotation()
    {
        // 透明度不在复位范围内：那是用户刻意调出来的一档，一起抹掉等于替他做了没要过的事
        setScale(1.0);
        setRotationDegrees(0.0);
    }

    QMenu *PinWindow::buildContextMenu()
    {
        auto *menu = new QMenu(this);

        // 多选里的这一张：菜单头上一句读数，末尾一项取消选择；下面那些状态开关对整组生效
        const bool inMultiSelection = m_selected && m_selectionCount > 1;
        if (inMultiSelection)
        {
            auto *countLabel = menu->addAction(GyanisScribe::Core::uiText("已选中 %1 张").arg(m_selectionCount));
            countLabel->setEnabled(false);
            menu->addSeparator();
        }

        menu->addAction(GyanisScribe::Core::uiText("复制"), this, &PinWindow::copyToClipboard);
        // 复制为文件：剪贴板里放的是一条本地文件路径，粘进资源管理器/聊天窗口得到一份 .png
        // （与「复制」是两条出口：那一条粘进文档里是一张图）
        menu->addAction(GyanisScribe::Core::uiText("复制为文件"), this, &PinWindow::copyAsFileToClipboard);

        // 自定义尺寸：滚轮是一格一格缩，这一项要的是「正好 1280 宽」这种精确值。
        // 对话框预填当前尺寸，认「宽x高」这一套写法（与截图选区那个输入框同一处解析）
        menu->addAction(GyanisScribe::Core::uiText("自定义尺寸…"), this,
                        [this]
                        {
                            const auto current  = QStringLiteral("%1x%2").arg(qRound(m_logicalSize.width())).arg(qRound(m_logicalSize.height()));
                            bool       accepted = false;
                            const auto text     = QInputDialog::getText(this, GyanisScribe::Core::uiText("自定义贴图尺寸"), GyanisScribe::Core::uiText("宽 x 高（像素）"),
                                                                        QLineEdit::Normal, current, &accepted);
                            if (!accepted)
                            {
                                return; // 取消是一种正常收场：什么都没发生，也什么都不用说
                            }
                            if (const auto parsed = GyanisScribe::Core::parseSizeText(text); parsed)
                            {
                                setExactSize(*parsed);
                            } else
                            {
                                QMessageBox::information(this, GyanisScribe::Core::uiText("自定义贴图尺寸"),
                                                         GyanisScribe::Core::uiText("没读懂这个尺寸：请按「宽x高」写，比如 640x360"));
                            }
                        });

        // 保存与「在文件夹中查看」是同一条路：后者在存下之后再打开那个目录，
        // 两处各写一份起名与写盘的代码，将来改命名模板就会漏掉一处
        menu->addAction(GyanisScribe::Core::uiText("保存"), this,
                        [this]
                        {
                            if (const auto saved = saveWithDefaultName(); !saved)
                            {
                                Core::Logger::error(saved.error().message);
                            }
                        });
        menu->addAction(GyanisScribe::Core::uiText("在文件夹中查看"), this,
                        [this]
                        {
                            const auto directory = screenshotDirectory(m_settings);
                            // 贴图是内存里的像素，桌面上并没有一个「它所在的文件夹」：先按设置的目录与模板存一份，
                            // 再把那个目录打开——用户要的是「这张图在盘上的样子」
                            if (const auto saved = saveWithDefaultName(); !saved)
                            {
                                Core::Logger::error(saved.error().message);
                                return;
                            }
                            const auto shown = Core::Paths::toDisplayText(directory);
                            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(shown)))
                            {
                                Core::Logger::warning(std::format("文件夹打不开：{}", shown.toStdString()));
                            }
                        });

        menu->addAction(GyanisScribe::Core::uiText("识别文字"), this, [this] { emit ocrRequested(m_image); });

        // 标注：在贴图上直接标注。动图不进这一档（标注会被下一帧盖掉），灰掉并写明出路，
        // 不把「点了没反应」留给用户猜
        auto *annotateAction = menu->addAction(GyanisScribe::Core::uiText("标注"), this, [this] { startAnnotation(); });
        if (isAnimated())
        {
            annotateAction->setEnabled(false);
            annotateAction->setToolTip(GyanisScribe::Core::uiText("动图不能直接标注：标注会被下一帧盖掉。请先保存成静态图，再把那张贴出来标注"));
        }

        // 取色：进模式之后鼠标只读像素，点一下把那一格的颜色交出去（见 mousePressEvent）
        auto *pickAction = menu->addAction(GyanisScribe::Core::uiText("取色"), this, [this] { setColorPickMode(!m_colorPickMode); });
        pickAction->setCheckable(true);
        pickAction->setChecked(m_colorPickMode);

        // 裁剪：拖一个框、回车裁到那一块。动图不进这一档（裁完会被下一帧盖回原尺寸），
        // 与「标注」同一条口径：灰掉并写明出路
        auto *cropAction = menu->addAction(GyanisScribe::Core::uiText("裁剪"), this, [this] { startCropping(); });
        if (isAnimated())
        {
            cropAction->setEnabled(false);
            cropAction->setToolTip(GyanisScribe::Core::uiText("动图不能裁剪：裁完会被下一帧盖回原尺寸。请先保存成静态图，再把那张贴出来裁"));
        }

        // 刷新这一张：重抓图本身此刻盖住的那块屏幕。动图与缩到小图那两档都不进——
        // 前者会被下一帧盖回去，后者那块小方块的面积不是这张图的面积
        auto *refreshAction = menu->addAction(GyanisScribe::Core::uiText("刷新这一张"), this, [this] { emit refreshRequested(); });
        refreshAction->setToolTip(GyanisScribe::Core::uiText("重抓这块屏幕换成新的画面，标注之外的设定都不动；按住 `F5` 可以连着刷新，松手定在最后抓到的那一帧"));
        if (isAnimated())
        {
            refreshAction->setEnabled(false);
            refreshAction->setToolTip(GyanisScribe::Core::uiText("动图不刷新：换上的那一帧静态像素会被下一帧盖掉。要盯实时画面请先存成静态图再贴出来"));
        }
        if (m_thumbnail)
        {
            refreshAction->setEnabled(false);
            refreshAction->setToolTip(GyanisScribe::Core::uiText("缩到小图这一档不刷新：那枚方块的面积不是这张图的面积。按 `T` 摆回原大小再刷新"));
        }

        auto *thumbnailAction = menu->addAction(GyanisScribe::Core::uiText("缩到小图"), this,
                                                [this]
                                                {
                                                    const bool wanted = !m_thumbnail;
                                                    applyPinBatchAction(*this, PinBatchAction::Thumbnail, wanted);
                                                    emit batchActionRequested(PinBatchAction::Thumbnail, wanted);
                                                });
        thumbnailAction->setCheckable(true);
        thumbnailAction->setChecked(m_thumbnail);
        thumbnailAction->setToolTip(GyanisScribe::Core::uiText("缩成屏上一枚小方块先让开地方；拖它的四边与四角能给这枚方块定大小，缩放与旋转原样留着，再点一下回到原样"));

        // 背景色（白板）：垫在图的后面，透明像素处露出来的就是它；「无背景」回到透出桌面那一档。
        // 四项预设各一项，外加一个开取色对话框的自定义
        auto      *backgroundMenu      = menu->addMenu(GyanisScribe::Core::uiText("背景色"));
        const auto addBackgroundChoice = [this, backgroundMenu](const QString &label, const QColor &color)
        {
            auto *item = backgroundMenu->addAction(label);
            item->setCheckable(true);
            item->setChecked(m_backgroundColor.isValid() && m_backgroundColor == color);
            QObject::connect(item, &QAction::triggered, this, [this, color] { setBackgroundColor(color); });
        };
        {
            // 这张窗口不是透明背景的（见构造里那行 WA_TranslucentBackground）：不垫背景色时
            // 透明像素处露出来的是主题的窗口底色，所以这一项的写法是「跟随主题」而不是「无背景」
            auto *none = backgroundMenu->addAction(GyanisScribe::Core::uiText("跟随主题"));
            none->setCheckable(true);
            none->setChecked(!m_backgroundColor.isValid());
            QObject::connect(none, &QAction::triggered, this, [this] { setBackgroundColor(QColor{}); });
        }
        addBackgroundChoice(GyanisScribe::Core::uiText("白色"), QColor(255, 255, 255));
        addBackgroundChoice(GyanisScribe::Core::uiText("黑色"), QColor(0, 0, 0));
        addBackgroundChoice(GyanisScribe::Core::uiText("灰色"), QColor(128, 128, 128));
        backgroundMenu->addSeparator();
        QObject::connect(backgroundMenu->addAction(GyanisScribe::Core::uiText("自定义…")), &QAction::triggered, this,
                         [this]
                         {
                             const QColor picked = QColorDialog::getColor(m_backgroundColor, this, GyanisScribe::Core::uiText("背景色"));
                             if (picked.isValid())
                             {
                                 setBackgroundColor(picked);
                             }
                         });

        // 透明区：带透明通道的图，那些像素底下画什么。四档各管一件事，所以把「露底色」与「让桌面透出来」摆在前面
        auto      *backdropMenu      = menu->addMenu(GyanisScribe::Core::uiText("透明区"));
        const auto addBackdropChoice = [this, backdropMenu](const QString &label, const TransparentBackdrop mode, const QString &hint)
        {
            auto *item = backdropMenu->addAction(label);
            item->setCheckable(true);
            item->setChecked(m_transparentBackdrop == mode);
            item->setToolTip(hint);
            QObject::connect(item, &QAction::triggered, this, [this, mode] { setTransparentBackdrop(mode); });
        };
        addBackdropChoice(GyanisScribe::Core::uiText("跟随窗口底色"), TransparentBackdrop::ThemeBase, GyanisScribe::Core::uiText("透明像素露出窗口的底色（一直以来的那一档）"));
        addBackdropChoice(GyanisScribe::Core::uiText("桌面透出"), TransparentBackdrop::SeeThrough, GyanisScribe::Core::uiText("整窗改成逐像素合成，透明那几块把底下的桌面透出来"));
        addBackdropChoice(GyanisScribe::Core::uiText("深色棋盘"), TransparentBackdrop::DarkChecker, GyanisScribe::Core::uiText("按深色棋盘画出透明区，摆在白文档上看得出边界"));
        addBackdropChoice(GyanisScribe::Core::uiText("亮色棋盘"), TransparentBackdrop::LightChecker, GyanisScribe::Core::uiText("按亮色棋盘画出透明区，摆在深色桌面上看得出边界"));

        // 只显示这一张：其余的收起来，屏幕腾给这一张；再点一下恢复按组的那套规则。
        // 与「标注」那两档不同，它对谁用都成立（动图也一样）
        auto *soloAction = menu->addAction(GyanisScribe::Core::uiText("只显示这一张"), this, [this] { emit soloRequested(!m_soloState); });
        soloAction->setCheckable(true);
        soloAction->setChecked(m_soloState);

        // 贴边隐藏：贴着屏边收成一条，悬停展开。拖着放到屏边也会进这一档（见 mouseReleaseEvent）
        auto *dockAction = menu->addAction(GyanisScribe::Core::uiText("贴边隐藏"), this, [this] { setEdgeDocked(m_edgeDock == ScreenEdge::None); });
        dockAction->setCheckable(true);
        dockAction->setChecked(m_edgeDock != ScreenEdge::None);

        // 移动到分组：列出管理器推过来的现有分组号，当前那一项带勾且灰掉（「现在就在这儿」），
        // 末尾一项按现有最大组号 +1 现建新组。移过去等于把它从当前这组暂时收起来，
        // 与实际改动都由管理器一处落账
        auto *groupMenu = menu->addMenu(GyanisScribe::Core::uiText("移动到分组"));
        for (const int group: m_groupChoices)
        {
            // 起过名字的组带出名字，没起过的还是「分组 N」：加一档命名不该把老账改写成另一种样子
            const auto name = m_groupNames.value(group);
            auto      *groupAction =
                    groupMenu->addAction(name.isEmpty() ? GyanisScribe::Core::uiText("分组 %1").arg(group) : GyanisScribe::Core::uiText("分组 %1：%2").arg(group).arg(name));
            groupAction->setCheckable(true);
            groupAction->setChecked(group == m_group);
            groupAction->setEnabled(group != m_group);
            QObject::connect(groupAction, &QAction::triggered, this, [this, group] { emit moveToGroupRequested(group); });
        }
        groupMenu->addSeparator();
        const auto nextGroup = m_groupChoices.isEmpty() ? kFirstPinGroup : m_groupChoices.back() + 1;
        QObject::connect(groupMenu->addAction(GyanisScribe::Core::uiText("新建分组")), &QAction::triggered, this, [this, nextGroup] { emit moveToGroupRequested(nextGroup); });
        groupMenu->addSeparator();

        // 改名与删组都只管报出去：名字表与谁在哪一组是管理器的账，这一层收用户敲的那一串
        QObject::connect(groupMenu->addAction(GyanisScribe::Core::uiText("重命名这一组…")), &QAction::triggered, this,
                         [this]
                         {
                             bool       accepted = false;
                             const auto text = QInputDialog::getText(this, GyanisScribe::Core::uiText("给分组起名字"),
                                                                     GyanisScribe::Core::uiText("这一组的名字（留空就回到「分组 %1」这种叫法）").arg(m_group), QLineEdit::Normal,
                                                                     m_groupNames.value(m_group), &accepted);
                             if (!accepted)
                             {
                                 return; // 取消是一种正常收场：什么都没发生，也什么都不用说
                             }
                             emit renameGroupRequested(m_group, text.trimmed());
                         });
        auto *deleteGroupAction = groupMenu->addAction(GyanisScribe::Core::uiText("删除这一组"), this, [this] { emit deleteGroupRequested(m_group); });
        // 第一组是删别组时成员并回去的落点：把它删了就没有落点，这一项在这儿灰着并说明为什么
        deleteGroupAction->setEnabled(m_group != kFirstPinGroup);
        if (m_group == kFirstPinGroup)
        {
            deleteGroupAction->setToolTip(GyanisScribe::Core::uiText("第一组是删掉别的组时成员并回去的落点，删不掉；想把这一张挪走请用上面的分组"));
        }

        menu->addSeparator();

        const auto capabilities = m_integration.capabilities();
        auto      *pinAction    = menu->addAction(GyanisScribe::Core::uiText("保持置顶"), this,
                                                  [this]
                                                  {
                                              const bool wanted = !m_pinned;
                                              applyPinBatchAction(*this, PinBatchAction::AlwaysOnTop, wanted);
                                              emit batchActionRequested(PinBatchAction::AlwaysOnTop, wanted);
                                                  });
        pinAction->setCheckable(true);
        pinAction->setChecked(m_pinned);

        auto *throughAction = menu->addAction(GyanisScribe::Core::uiText("鼠标穿透"), this,
                                              [this]
                                              {
                                                  const bool wanted = !m_mouseThrough;
                                                  applyPinBatchAction(*this, PinBatchAction::MouseThrough, wanted);
                                                  emit batchActionRequested(PinBatchAction::MouseThrough, wanted);
                                              });
        throughAction->setCheckable(true);
        throughAction->setChecked(m_mouseThrough);
        throughAction->setVisible(capabilities.clickThrough);
        throughAction->setToolTip(GyanisScribe::Core::uiText("滚轮缩放，Ctrl+滚轮旋转，Shift+滚轮调透明度，R 转正一挡（Shift+R 反向），0 或双击回到原始比例，Esc 关闭"));

        // 窗口阴影：窗口比内容每边大一圈、投影画在带子里，内容坐标不动（见 setShadowEnabled）。
        // 缩略图态不铺：那枚小方块是临时让位用的
        auto *shadowAction = menu->addAction(GyanisScribe::Core::uiText("窗口阴影"), this, [this] { setShadowEnabled(!m_shadowEnabled); });
        shadowAction->setCheckable(true);
        shadowAction->setChecked(m_shadowEnabled);
        shadowAction->setToolTip(GyanisScribe::Core::uiText("给这一张围一圈投影：窗口比内容大一圈，内容坐标与吸附位置都不变；缩到小方块（T）时不带"));

        // 平滑缩放：放大看截图里的小字时，插值会把笔画糊成一片灰——关掉就是硬边像素格。
        // 与窗口阴影同一档形状：只改「看」，复制、保存、标注与识别拿到的仍是原始像素
        auto *smoothAction = menu->addAction(GyanisScribe::Core::uiText("平滑缩放"), this, [this] { setSmoothScaleEnabled(!m_smoothScaleEnabled); });
        smoothAction->setCheckable(true);
        smoothAction->setChecked(m_smoothScaleEnabled);
        smoothAction->setToolTip(GyanisScribe::Core::uiText("放大时把像素铺平：关掉这一档，一格源像素就是屏上一块硬边方格，读小字更清楚（只改显示，不进成品）"));

        // 视图变换四档：翻转与灰度/反色都只改「看」，复制、保存、标注拿到的仍是原始像素——
        // 与旋转同一条规矩。菜单项的勾选态就是当前读数，键位与它们同一条路（H/V/G/I）
        menu->addSeparator();
        auto *flipHorizontalAction = menu->addAction(GyanisScribe::Core::uiText("左右翻转"), this,
                                                     [this]
                                                     {
                                                         const bool wanted = !m_mirroredHorizontally;
                                                         applyPinBatchAction(*this, PinBatchAction::FlipHorizontal, wanted);
                                                         emit batchActionRequested(PinBatchAction::FlipHorizontal, wanted);
                                                     });
        flipHorizontalAction->setCheckable(true);
        flipHorizontalAction->setChecked(m_mirroredHorizontally);
        auto *flipVerticalAction = menu->addAction(GyanisScribe::Core::uiText("上下翻转"), this,
                                                   [this]
                                                   {
                                                       const bool wanted = !m_mirroredVertically;
                                                       applyPinBatchAction(*this, PinBatchAction::FlipVertical, wanted);
                                                       emit batchActionRequested(PinBatchAction::FlipVertical, wanted);
                                                   });
        flipVerticalAction->setCheckable(true);
        flipVerticalAction->setChecked(m_mirroredVertically);
        auto *grayscaleAction = menu->addAction(GyanisScribe::Core::uiText("灰度显示"), this,
                                                [this]
                                                {
                                                    const bool wanted = !m_grayscale;
                                                    applyPinBatchAction(*this, PinBatchAction::Grayscale, wanted);
                                                    emit batchActionRequested(PinBatchAction::Grayscale, wanted);
                                                });
        grayscaleAction->setCheckable(true);
        grayscaleAction->setChecked(m_grayscale);
        auto *invertAction = menu->addAction(GyanisScribe::Core::uiText("反色"), this,
                                             [this]
                                             {
                                                 const bool wanted = !m_inverted;
                                                 applyPinBatchAction(*this, PinBatchAction::Inverted, wanted);
                                                 emit batchActionRequested(PinBatchAction::Inverted, wanted);
                                             });
        invertAction->setCheckable(true);
        invertAction->setChecked(m_inverted);

        if (inMultiSelection)
        {
            menu->addSeparator();
            // 整组出口：一次把选中集里每一张都存一份。存哪、叫什么名是每张自己的账（都走同一个目录与命名模板），
            // 谁在选中集里是管理器的账，这一层只报「有人要批量存」
            menu->addAction(GyanisScribe::Core::uiText("批量保存（%1 张）").arg(m_selectionCount), this, [this] { emit batchSaveRequested(); });
            menu->addAction(GyanisScribe::Core::uiText("取消选择（%1 张）").arg(m_selectionCount), this, [this] { emit clearSelectionRequested(); });
        }

        menu->addSeparator();
        menu->addAction(GyanisScribe::Core::uiText("关闭"), this,
                        [this]
                        {
                            // 与 Esc 那条同一条次序：先自己收场再报出去（管理器收到后会现场析构这一张）
                            close();
                            emit closedByUser();
                        });
        // 销毁与关闭只差在「还能不能按回来」：这一张整组动作不参与（一次毁掉一桌太顺手了），
        // 也不进那份可恢复的册子
        auto *destroyAction = menu->addAction(GyanisScribe::Core::uiText("销毁这一张"), this,
                                              [this]
                                              {
                                                  close();
                                                  emit destroyRequested();
                                              });
        destroyAction->setToolTip(GyanisScribe::Core::uiText("彻底关掉这一张：它不再留在「重新贴出最近关掉的贴图」那五张里。只想暂时腾个地方请用「关闭」"));
        return menu;
    }

    void PinWindow::contextMenuEvent(QContextMenuEvent *event)
    {
        // 标注模式：右键不出菜单——退出标注走 Esc，菜单里的动作会与正在画的这一张打架
        if (m_annotating)
        {
            return;
        }

        QMenu *menu = buildContextMenu();
        menu->exec(event->globalPos());
        delete menu;
    }

    void PinWindow::dragEnterEvent(QDragEnterEvent *event)
    {
        // 标注中不收拖放：这一阵子这张图是一张画布，往画布上丢文件不是任何人的意图
        if (m_annotating)
        {
            return;
        }

        // 只做粗判：带文件、图像或文本就收下。细判要在落下那一刻读文件，
        // 拖动途中读一遍纯属白费——具体的可用性与拒绝原因由内容渲染那一层回答
        if (dragPayloadIsPinnable(event->mimeData()))
        {
            event->acceptProposedAction();
        }
    }

    void PinWindow::dragMoveEvent(QDragMoveEvent *event)
    {
        // 与进入同一判据：拖动途中不逐格放行的话，这一拖根本落不下来
        if (dragPayloadIsPinnable(event->mimeData()))
        {
            event->acceptProposedAction();
        }
    }

    void PinWindow::dropEvent(QDropEvent *event)
    {
        if (m_annotating || event->mimeData() == nullptr)
        {
            return;
        }

        event->acceptProposedAction();
        // 只报「谁被拖到了哪」：渲染成什么、贴几张、失败怎么说，都是界面层的事；
        // 载荷指针只在本次事件处理期间有效，接收方要当场把它读掉
        emit dropRequested(event->mimeData(), mapToGlobal(event->position().toPoint()));
    }

} // namespace GyanisScribe::Pin
