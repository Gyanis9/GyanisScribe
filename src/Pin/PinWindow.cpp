#include "Pin/PinWindow.h"

#include "Annotation/AnnotationEngine.h"
#include "Annotation/AnnotationToolbar.h"
#include "Annotation/AnnotationToolbarBridge.h"
#include "Annotation/AnnotationToolbarGeometry.h"
#include "Annotation/AnnotationView.h"
#include "Annotation/AnnotationWindow.h"
#include "Core/ImageFormat.h"
#include "Core/Logger.h"
#include "Core/OutputDecoration.h"
#include "Core/Paths.h"
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
#include "Settings/SettingsKeys.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
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
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QKeyEvent>

#include <QGuiApplication>
#include <QScreen>
#include <QUrl>

#include <cmath>
#include <format>

namespace GyanisScribe::Pin
{
    namespace
    {
        /// 手柄边长，逻辑像素
        constexpr double kHandleSize = 10.0;

        /// 滚轮一格的缩放倍率
        constexpr double kWheelScaleStep = 1.1;

        /// 滚轮一格转过的角度
        constexpr double kWheelRotationStep = 5.0;

        /// 滚轮一格改变的透明度
        constexpr double kWheelOpacityStep = 0.05;

        /// 键盘一挡转过的角度：滚轮那 5° 是细调，按一次 `R` 要的是「转正一首」
        constexpr double kKeyboardRotationStride = 90.0;

        /// 选中边框与悬停边框的描边宽度
        constexpr int kBorderWidth = 1;

        /// 贴图的最小边长，逻辑像素：再小就找不到也拖不动
        constexpr double kMinimumEdge = 8.0;

        /// 缩略图模式那枚小方块的边长，逻辑像素
        constexpr double kThumbnailEdge = 48.0;

        /// 取色读数框：色块边长、字号、内边距与离光标的偏移，都是逻辑像素
        constexpr double kColorReadoutSwatchEdge    = 16.0;
        constexpr int    kColorReadoutFontPointSize = 8;
        constexpr double kColorReadoutPadding       = 4.0;
        constexpr double kColorReadoutOffset        = 14.0;

        /// 按住 Shift 时标注微调一次挪多少格：与标注窗那一档同值
        constexpr int kAnnotateCoarseNudgeStride = 10;

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

    QImage PinWindow::decorateForOutput(const QImage &image, const bool fillBackground) const
    {
        const auto border = m_settings.boolValue(Settings::kOutputBorder, false);
        const auto shadow = m_settings.boolValue(Settings::kOutputShadow, false);
        return Core::decorateForOutput(image, border, shadow, fillBackground);
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

    PinState PinWindow::state() const
    {
        PinState state{globalRect().topLeft(), m_logicalSize, m_scale, m_rotationDegrees, m_opacity};
        state.group        = m_group;
        state.alwaysOnTop  = m_pinned;
        state.mouseThrough = m_mouseThrough;
        state.sourceFile   = m_sourceFile;
        return state;
    }


    std::vector<QRectF> PinWindow::handleRects() const
    {
        return Pin::handleRects(contentRect(), kHandleSize);
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

        // 裸字母切工具：与标注窗共用 kToolTable 的键位列，切完回灌按钮态
        if (const auto tool = Annotation::toolShortcutFor(event->key(), event->modifiers()); tool)
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

    void PinWindow::applyGeometry()
    {
        const auto scaled   = m_thumbnail ? QSizeF{kThumbnailEdge, kThumbnailEdge} : windowSizeFor(m_logicalSize, m_scale);
        const auto rotation = m_thumbnail ? 0.0 : m_rotationDegrees;
        const auto width    = std::max(kMinimumEdge, scaled.width());
        const auto height   = std::max(kMinimumEdge, scaled.height());

        const QPointF center = QRectF(pos(), size()).center();
        const auto    box    = rotatedBoundingBox(center, QSizeF(width, height), rotation);

        // 尺寸取外接框，绘制时按同样的角度转回去：命中、拖动与手柄都落在这个轴对齐框里算
        setGeometry(box.toAlignedRect());

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
        const auto   scaled = m_thumbnail ? QSizeF{kThumbnailEdge, kThumbnailEdge} : windowSizeFor(m_logicalSize, m_scale);
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
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const auto content = contentRect();
        painter.translate(content.center());
        // 缩略图态不旋转：一枚小方块里转起来看不清是什么，退出后旋转照旧。翻转不改外框，照常生效
        painter.rotate(m_thumbnail ? 0.0 : m_rotationDegrees);
        // 翻转在旋转之前做：转的是翻好的那一份，与 imagePixelAt 的反变换成对（先反旋转、后反翻转）
        painter.scale(m_mirroredHorizontally ? -1.0 : 1.0, m_mirroredVertically ? -1.0 : 1.0);
        painter.translate(-content.center());
        painter.drawImage(content, displayImage(), QRectF(QPointF(0.0, 0.0), QSizeF(m_image.width(), m_image.height())));
        painter.resetTransform();

        // 悬停或选中才画边框，平时一张干净的图压在屏幕上不该有装饰
        const bool decorated = underMouse() || m_draggedHandle.has_value();
        if (decorated)
        {
            auto border = QPen(GyanisScribe::Ui::kAccentColor);
            border.setWidth(kBorderWidth);
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

        const auto point   = event->position();
        const auto handles = handleRects();
        for (std::size_t index = 0; index < handles.size(); ++index)
        {
            if (!handles[index].contains(point))
            {
                continue;
            }
            m_draggedHandle       = static_cast<HandleKind>(index);
            m_dragInitialScale    = m_scale;
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
            setScale(scaleFromDrag(m_dragInitialScale, m_dragInitialDistance, distance));
            return;
        }

        if (event->buttons().testFlag(Qt::LeftButton))
        {
            m_dragMoved = true;
            move(event->globalPosition().toPoint() - m_dragOffset);
        }
    }

    void PinWindow::mouseReleaseEvent(QMouseEvent *event)
    {
        Q_UNUSED(event);
        if (m_annotating)
        {
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

        // 不加修饰键就是缩放：按倍率乘除，滚轮一格 1.1 倍
        setScale(steps > 0.0 ? m_scale * kWheelScaleStep : m_scale / kWheelScaleStep);
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

        if (event->key() == Qt::Key_Escape)
        {
            // 取色模式里 Esc 先退出取色：用户想反悔的那一下多半是「不取了」，
            // 而不是把这张贴图也关掉（那是撤销不回的动作）
            if (m_colorPickMode)
            {
                setColorPickMode(false);
                return;
            }
            emit closedByUser();
            close();
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
        menu->addAction(GyanisScribe::Core::uiText("复制"), this, &PinWindow::copyToClipboard);

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

        auto *thumbnailAction = menu->addAction(GyanisScribe::Core::uiText("缩到小图"), this, [this] { setThumbnailMode(!m_thumbnail); });
        thumbnailAction->setCheckable(true);
        thumbnailAction->setChecked(m_thumbnail);

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
            auto *groupAction = groupMenu->addAction(GyanisScribe::Core::uiText("分组 %1").arg(group));
            groupAction->setCheckable(true);
            groupAction->setChecked(group == m_group);
            groupAction->setEnabled(group != m_group);
            QObject::connect(groupAction, &QAction::triggered, this, [this, group] { emit moveToGroupRequested(group); });
        }
        groupMenu->addSeparator();
        const auto nextGroup = m_groupChoices.isEmpty() ? kFirstPinGroup : m_groupChoices.back() + 1;
        QObject::connect(groupMenu->addAction(GyanisScribe::Core::uiText("新建分组")), &QAction::triggered, this, [this, nextGroup] { emit moveToGroupRequested(nextGroup); });

        menu->addSeparator();

        const auto capabilities = m_integration.capabilities();
        auto      *pinAction    = menu->addAction(GyanisScribe::Core::uiText("保持置顶"), this, [this] { setAlwaysOnTop(!m_pinned); });
        pinAction->setCheckable(true);
        pinAction->setChecked(m_pinned);

        auto *throughAction = menu->addAction(GyanisScribe::Core::uiText("鼠标穿透"), this,
                                              [this]
                                              {
                                                  if (const auto result = setMouseThrough(!m_mouseThrough); !result)
                                                  {
                                                      // 菜单项在没能力的平台上直接不出现，走到这里说明能力表与菜单不一致：记一条错误按实际状态回退
                                                      Core::Logger::error(result.error().message);
                                                  }
                                              });
        throughAction->setCheckable(true);
        throughAction->setChecked(m_mouseThrough);
        throughAction->setVisible(capabilities.clickThrough);
        throughAction->setToolTip(GyanisScribe::Core::uiText("滚轮缩放，Ctrl+滚轮旋转，Shift+滚轮调透明度，R 转正一挡（Shift+R 反向），0 或双击回到原始比例，Esc 关闭"));

        // 视图变换四档：翻转与灰度/反色都只改「看」，复制、保存、标注拿到的仍是原始像素——
        // 与旋转同一条规矩。菜单项的勾选态就是当前读数，键位与它们同一条路（H/V/G/I）
        menu->addSeparator();
        auto *flipHorizontalAction = menu->addAction(GyanisScribe::Core::uiText("左右翻转"), this, [this] { setMirroredHorizontally(!m_mirroredHorizontally); });
        flipHorizontalAction->setCheckable(true);
        flipHorizontalAction->setChecked(m_mirroredHorizontally);
        auto *flipVerticalAction = menu->addAction(GyanisScribe::Core::uiText("上下翻转"), this, [this] { setMirroredVertically(!m_mirroredVertically); });
        flipVerticalAction->setCheckable(true);
        flipVerticalAction->setChecked(m_mirroredVertically);
        auto *grayscaleAction = menu->addAction(GyanisScribe::Core::uiText("灰度显示"), this, [this] { setGrayscale(!m_grayscale); });
        grayscaleAction->setCheckable(true);
        grayscaleAction->setChecked(m_grayscale);
        auto *invertAction = menu->addAction(GyanisScribe::Core::uiText("反色"), this, [this] { setInverted(!m_inverted); });
        invertAction->setCheckable(true);
        invertAction->setChecked(m_inverted);

        menu->addSeparator();
        menu->addAction(GyanisScribe::Core::uiText("关闭"), this,
                        [this]
                        {
                            emit closedByUser();
                            close();
                        });
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
