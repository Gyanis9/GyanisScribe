#include "Pin/PinWindow.h"

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

    void PinWindow::setImage(const QImage &image)
    {
        m_image       = image;
        m_logicalSize = QSizeF(image.width(), image.height());
        if (image.devicePixelRatio() > 1.0)
        {
            m_logicalSize = QSizeF(image.width() / image.devicePixelRatio(), image.height() / image.devicePixelRatio());
        }
        applyGeometry();
        update();
    }

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

    void PinWindow::copyToClipboard()
    {
        QApplication::clipboard()->setImage(m_image);
        Core::Logger::info(std::format("贴图已交剪贴板：{}×{} 像素", m_image.width(), m_image.height()));
    }

    Core::Expected<QString> PinWindow::saveToFile(const std::filesystem::path &directory, const QString &fileName)
    {
        if (const auto ensured = Core::Paths::ensureDirectory(directory); !ensured)
        {
            return std::unexpected(ensured.error());
        }

        // 文件名归调用方：右键菜单那一路用 Core::Paths::imageFileName 起名，布局那一路用序号
        const auto filePath = directory / Core::Paths::fromDisplayText(fileName);

        if (!m_image.save(Core::Paths::toDisplayText(filePath)))
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

    QSizeF PinWindow::logicalImageSize() const noexcept
    {
        return m_logicalSize;
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
        QWidget::keyPressEvent(event);
    }

    QMenu *PinWindow::buildContextMenu()
    {
        auto *menu = new QMenu(this);
        menu->addAction(GyanisScribe::Core::uiText("复制"), this, &PinWindow::copyToClipboard);

        const auto directory = screenshotDirectory(m_settings);
        menu->addAction(GyanisScribe::Core::uiText("保存"), this,
                        [this, directory]
                        {
                            if (const auto saved = saveToFile(directory, Core::Paths::imageFileName(QDateTime::currentDateTime(), "png")); !saved)
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
        throughAction->setToolTip(GyanisScribe::Core::uiText("滚轮缩放，Ctrl+滚轮旋转，Shift+滚轮调透明度，Esc 关闭"));

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
