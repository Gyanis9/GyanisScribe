#include "Pin/PinWindow.h"

#include "Core/ImageFormat.h"
#include "Core/Logger.h"
#include "Core/Paths.h"
#include "Core/Text.h"
#include "Pin/PinLayout.h"
#include "Ui/DesignTokens.h"
#include "Platform/IPlatformIntegration.h"
#include "Settings/ISettingsStore.h"
#include "Settings/SettingsKeys.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QKeyEvent>

#include <QGuiApplication>
#include <QScreen>

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
         * @return 成功返回「文件名后缀 + 质量」；配置里存了词表外的取值或越界的质量时返回那一格的回绝原因
         * @details 不替用户猜一档：猜错就是把成品按另一种压缩写出去，事后无从找回原样。
         *          PNG 是无损的，质量那一格对它不起作用，交给 Qt 自己忽略。
         */
        [[nodiscard]] Core::Expected<std::pair<std::string_view, int>> imageSaveTarget(const Settings::ISettingsStore &settings)
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
            return std::pair{Core::imageFormatExtension(*format), quality};
        }
    } // namespace

    PinWindow::PinWindow(const QImage &image, Settings::ISettingsStore &settings, const Platform::IPlatformIntegration &integration, QWidget *parent) :
        QWidget(parent), m_settings(settings), m_integration(integration)
    {
        // 无边框 + 置顶 + Tool：Tool 让它不进任务栏，贴图是浮在屏幕上的东西，不该占一个窗口条目
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setMouseTracking(true); // 悬停要亮边框，不追踪就等不到进入事件
        setAttribute(Qt::WA_TranslucentBackground, false);

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

    void PinWindow::copyToClipboard()
    {
        QApplication::clipboard()->setImage(m_image);
        Core::Logger::info(std::format("贴图已交剪贴板：{}×{} 像素", m_image.width(), m_image.height()));
    }

    Core::Expected<QString> PinWindow::saveToFile(const std::filesystem::path &directory, const QString &fileName, const int quality)
    {
        if (const auto ensured = Core::Paths::ensureDirectory(directory); !ensured)
        {
            return std::unexpected(ensured.error());
        }

        // 文件名归调用方：右键菜单那一路用 Core::Paths::imageFileName 起名（后缀按设置里那一档），
        // 布局那一路用序号。格式不单独传：Qt 按后缀挑编码器，再传一次格式名就是第二个出处
        const auto filePath = directory / Core::Paths::fromDisplayText(fileName);

        if (!m_image.save(Core::Paths::toDisplayText(filePath), nullptr, quality))
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
        return PinState{globalRect().topLeft(), m_logicalSize, m_scale, m_rotationDegrees, m_opacity};
    }


    std::vector<QRectF> PinWindow::handleRects() const
    {
        return Pin::handleRects(contentRect(), kHandleSize);
    }

    void PinWindow::applyGeometry()
    {
        const auto scaled = windowSizeFor(m_logicalSize, m_scale);
        const auto width  = std::max(kMinimumEdge, scaled.width());
        const auto height = std::max(kMinimumEdge, scaled.height());

        const QPointF center = QRectF(pos(), size()).center();
        const auto    box    = rotatedBoundingBox(center, QSizeF(width, height), m_rotationDegrees);

        // 尺寸取外接框，绘制时按同样的角度转回去：命中、拖动与手柄都落在这个轴对齐框里算
        setGeometry(box.toAlignedRect());
    }

    QRectF PinWindow::contentRect() const
    {
        const auto   scaled = windowSizeFor(m_logicalSize, m_scale);
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
        painter.rotate(m_rotationDegrees);
        painter.translate(-content.center());
        painter.drawImage(content, m_image, QRectF(QPointF(0.0, 0.0), QSizeF(m_image.width(), m_image.height())));
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
    }

    void PinWindow::mousePressEvent(QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton)
        {
            return;
        }

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
        if (m_draggedHandle)
        {
            const auto point    = event->position();
            const auto distance = std::hypot(point.x() - rect().center().x(), point.y() - rect().center().y());
            setScale(scaleFromDrag(m_dragInitialScale, m_dragInitialDistance, distance));
            return;
        }

        if (event->buttons().testFlag(Qt::LeftButton))
        {
            move(event->globalPosition().toPoint() - m_dragOffset);
        }
    }

    void PinWindow::mouseReleaseEvent(QMouseEvent *event)
    {
        Q_UNUSED(event);
        m_draggedHandle.reset();
        update();
    }

    void PinWindow::wheelEvent(QWheelEvent *event)
    {
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
        if (event->key() == Qt::Key_Escape)
        {
            emit closedByUser();
            close();
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

        QWidget::keyPressEvent(event);
    }

    void PinWindow::mouseDoubleClickEvent(QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton)
        {
            QWidget::mouseDoubleClickEvent(event);
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

        const auto directory = screenshotDirectory(m_settings);
        menu->addAction(GyanisScribe::Core::uiText("保存"), this,
                        [this, directory]
                        {
                            const auto target = imageSaveTarget(m_settings);
                            if (!target)
                            {
                                // 格式或质量那一格存坏了：不写一个猜出来的文件，原因照旧只进日志
                                Core::Logger::error(target.error().message);
                                return;
                            }
                            if (const auto saved = saveToFile(directory, Core::Paths::imageFileName(QDateTime::currentDateTime(), target->first), target->second); !saved)
                            {
                                Core::Logger::error(saved.error().message);
                            }
                        });

        menu->addAction(GyanisScribe::Core::uiText("识别文字"), this, [this] { emit ocrRequested(m_image); });
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
        QMenu *menu = buildContextMenu();
        menu->exec(event->globalPos());
        delete menu;
    }

} // namespace GyanisScribe::Pin
