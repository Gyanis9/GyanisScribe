#include "Gui/Annotation/AnnotationView.h"

#include "Core/Image/ImageFit.h"
#include "Core/Pick/PickGeometry.h"
#include "Base/Logger.h"
#include "Base/Text.h"
#include "Gui/Theme/DesignTokens.h"

#include <QFontMetrics>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QRectF>
#include <QTextCursor>
#include <QTextDocument>
#include "Gui/Controls/TextEdit.h"

#include <QTextEdit>

#include <algorithm>
#include <cmath>
#include <format>

namespace
{
    using GyanisScribe::Core::IAnnotationEngine;

    /// 透明底垫的那格棋盘：一格取主题底色、一格取它深一档，两格并成一块可平铺的图
    [[nodiscard]] QPixmap checkerTile(const QColor &base)
    {
        constexpr int kCell = 8;
        QPixmap       tile{QSize{kCell * 2, kCell * 2}};
        tile.fill(base);
        QPainter painter{&tile};
        painter.setPen(Qt::NoPen);
        painter.setBrush(base.darker(118));
        painter.drawRect(0, 0, kCell, kCell);
        painter.drawRect(kCell, kCell, kCell, kCell);
        painter.end();
        return tile;
    }

    /// 行内文字输入框的默认宽度，逻辑像素：再窄就打不开一个像样的句子
    constexpr int kTextEditorWidth = 180;

    /// 输入框在文字行之外补的高度余量，逻辑像素：贴着字号摆会让光标被裁掉一半
    constexpr int kTextEditorHeightPad = 6;

    /// 输入框的高度下限，逻辑像素：至少装得下一行与光标
    constexpr int kTextEditorMinimumHeight = 24;

    /// 重裁放大镜的边框线宽，逻辑像素：与截图遮罩那一枚同档
    constexpr int kMagnifierPenWidth = 2;

    /// 重裁放大镜读数条的高度，逻辑像素
    constexpr int kMagnifierStripHeight = 24;

    /// 图内按住后挪多远算「要整体挪」：几像素的手抖仍按单击清选中处理
    constexpr int kCaptureMoveSlop = 3;

    /// 拖动节拍：8ms 一拍（约 125Hz），高于多数显示器刷新率；再密只是把主线程烧在重复帧上
    constexpr int kDragUpdateIntervalMs = 8;

    /// 提交前削掉尾巴上的空行：多半是「敲完回车就提交」带出来的，留着会在图形里多出一行空白
    QString textWithoutTrailingBlankLines(const QString &text)
    {
        QString trimmed = text;
        while (trimmed.endsWith(QLatin1Char('\n')))
        {
            trimmed.chop(1);
        }
        return trimmed;
    }

    /**
     * @brief 鼠标修饰键翻成拖手柄的两档开关：Ctrl 中心点、Shift 等比例，与主流图像工具同一套键位
     * @param modifiers 此刻按住的修饰键
     * @return 拖手柄时用的两档
     */
    [[nodiscard]] GyanisScribe::Core::SelectionDragModifiers selectionDragModifiersOf(const Qt::KeyboardModifiers modifiers)
    {
        return GyanisScribe::Core::SelectionDragModifiers{.fromCenter = modifiers.testFlag(Qt::ControlModifier), .proportional = modifiers.testFlag(Qt::ShiftModifier)};
    }
} // namespace

namespace GyanisScribe::Gui
{
    QPoint toCanvasPoint(const QPoint &widgetPoint, const QRect &displayRect, const QSize &canvasLogicalSize)
    {
        if (displayRect.isEmpty() || canvasLogicalSize.isEmpty())
        {
            return {};
        }

        const auto scaleX = static_cast<qreal>(canvasLogicalSize.width()) / displayRect.width();
        const auto scaleY = static_cast<qreal>(canvasLogicalSize.height()) / displayRect.height();

        // 越界夹到边缘：拖出画布的意思是「画到边界」，把它当非法参数报出去只会让画笔在边缘卡住
        const auto x = std::clamp(static_cast<int>(std::lround((widgetPoint.x() - displayRect.left()) * scaleX)), 0, canvasLogicalSize.width() - 1);
        const auto y = std::clamp(static_cast<int>(std::lround((widgetPoint.y() - displayRect.top()) * scaleY)), 0, canvasLogicalSize.height() - 1);
        return {x, y};
    }

    QPoint toWidgetPoint(const QPoint &canvasPoint, const QRect &displayRect, const QSize &canvasLogicalSize)
    {
        if (displayRect.isEmpty() || canvasLogicalSize.isEmpty())
        {
            return {};
        }

        const auto scaleX = static_cast<qreal>(displayRect.width()) / canvasLogicalSize.width();
        const auto scaleY = static_cast<qreal>(displayRect.height()) / canvasLogicalSize.height();
        return {displayRect.left() + static_cast<int>(std::lround(canvasPoint.x() * scaleX)), displayRect.top() + static_cast<int>(std::lround(canvasPoint.y() * scaleY))};
    }

    AnnotationView::AnnotationView(Core::IAnnotationEngine &engine, QWidget *parent) : QWidget(parent), m_engine(engine)
    {
        setMouseTracking(true);
        setCursor(Qt::ArrowCursor);
        // 拖动节拍常开：移动事件只记最后一帧，落几何与重绘都排在拍上；松手时冲刷并停表
        m_dragUpdateTimer.setInterval(kDragUpdateIntervalMs);
        m_dragUpdateTimer.setSingleShot(false);
        QObject::connect(&m_dragUpdateTimer, &QTimer::timeout, this,
                         [this]
                         {
                             if (m_hasPendingDragGeometry)
                             {
                                 applyPendingDragGeometry();
                             }
                         });
        // 引擎的图形变了就重绘：这里不缓存合成图，在途的那一条也要立刻看得见
        QObject::connect(&engine, &Core::IAnnotationEngine::canvasChanged, this, [this] { update(); });
    }

    QSize AnnotationView::sizeHint() const
    {
        return m_engine.canvas().logicalSize;
    }

    QRect AnnotationView::currentDisplayRect() const
    {
        return Core::displayRectFor(size(), m_engine.canvas().logicalSize);
    }

    void AnnotationView::paintEvent(QPaintEvent *)
    {
        if (m_hasCropPreview)
        {
            QPainter painter{this};
            // 直接从整屏冻帧的源矩形画到整窗：不先 copy 一块新图，拖动时没有逐帧分配与拷贝
            painter.drawImage(QRect{QPoint{0, 0}, size()}, m_cropPreviewFrame, m_cropPreviewSource);
            painter.save();
            // 预览图先按控件铺满，图形跟着同一份逻辑尺寸缩放：裁掉一截的预览与图形才不会各按各的比例落
            if (!m_cropPreviewLogicalSize.isEmpty())
            {
                painter.scale(size().width() / m_cropPreviewLogicalSize.width(), size().height() / m_cropPreviewLogicalSize.height());
            }
            painter.translate(m_cropPreviewShapeOrigin);
            // 重裁预览里的图形也要抗锯齿：预览与成品画出的边缘是同一档
            painter.setRenderHint(QPainter::Antialiasing, true);
            m_engine.paintShapes(painter);
            painter.restore();
            paintCaptureFrame(painter);
            paintCropMagnifier(painter);
            return;
        }

        const auto &canvas = m_engine.canvas();
        const auto  rect   = currentDisplayRect();
        if (canvas.image.isNull() || rect.isEmpty())
        {
            return;
        }

        QPainter painter{this};
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        // 图形边缘与导出用同一档抗锯齿：斜线、箭头与椭圆的屏上样子要与成品逐像素对得上
        painter.setRenderHint(QPainter::Antialiasing);

        // 透明底要有东西衬着才看得出来：先垫一层棋盘格。白底那一路完全不透明，垫在上面也看不见
        if (canvas.image.hasAlphaChannel())
        {
            painter.fillRect(rect, QBrush{checkerTile(palette().color(QPalette::Base))});
        }

        // 底图按物理像素给源矩形、按逻辑矩形给目标：这样高 DPI 下两者对齐，不需要再乘一遍倍比
        painter.drawImage(QRectF{rect}, canvas.image, QRectF{{0.0, 0.0}, QSizeF{canvas.image.size()}});

        // 图形画在「显示区 + 逻辑坐标」这一层：工具的坐标记的就是逻辑值
        const auto shapeScale = static_cast<qreal>(rect.width()) / canvas.logicalSize.width();
        painter.save();
        painter.translate(rect.topLeft());
        painter.scale(shapeScale, shapeScale);
        m_engine.paintShapes(painter);
        painter.restore();

        // 选框、手柄与提示线是界面辅助线，不要抗锯齿：虚线与小方块糊上一层灰边就看不清了
        painter.setRenderHint(QPainter::Antialiasing, false);

        // 选框走控件坐标而不是逻辑坐标：线宽与虚线间隔不该随缩放变粗变细
        if (const auto frame = m_engine.selectionFrame(); frame)
        {
            // 单条选中：画跟着图形一起转的定向框，四角连成多边形
            const auto toWidget = [&rect, &canvas](const QPointF &canvasPoint)
            { return QPointF{toWidgetPoint(QPoint{qRound(canvasPoint.x()), qRound(canvasPoint.y())}, rect, canvas.logicalSize)}; };

            QPolygonF outline;
            for (const int node: {0, 2, 4, 6})
            {
                outline << toWidget((*frame)[node]);
            }
            QPen outlinePen{GyanisScribe::Gui::kAccentColor, 1.0, Qt::DashLine};
            outlinePen.setCapStyle(Qt::FlatCap);
            painter.setPen(outlinePen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(outline);

            // 缩放八枚手柄：白底描边的小方块，压在图形上也看得见
            QPen handlePen{GyanisScribe::Gui::kAccentColor, 1.0};
            painter.setPen(handlePen);
            painter.setBrush(Qt::white);
            for (const auto &node: *frame)
            {
                const QPoint widget = toWidget(node).toPoint();
                painter.drawRect(QRect{widget.x() - kHandleSizeWidget / 2, widget.y() - kHandleSizeWidget / 2, kHandleSizeWidget, kHandleSizeWidget});
            }

            // 旋转手柄：上边中点连一条细线到圆点，形状与缩放手柄分开，一眼认得出是「转」不是「拉」
            const QPointF rotatePoint = rotateHandlePointWidget(*frame);
            painter.drawLine(toWidget((*frame)[1]), rotatePoint);
            painter.drawEllipse(rotatePoint, kHandleSizeWidget / 2.0, kHandleSizeWidget / 2.0);
        } else if (const auto bounds = m_engine.selectionBounds(); bounds)
        {
            // 多选（或手柄算不出来的退化）：只画联合虚线框
            QPen outline{GyanisScribe::Gui::kAccentColor, 1.0, Qt::DashLine};
            outline.setCapStyle(Qt::FlatCap);
            painter.setPen(outline);
            painter.setBrush(Qt::NoBrush);
            const auto topLeft     = toWidgetPoint(bounds->topLeft(), rect, canvas.logicalSize);
            const auto bottomRight = toWidgetPoint(bounds->bottomRight(), rect, canvas.logicalSize);
            painter.drawRect(QRect{topLeft, bottomRight}.adjusted(0, 0, 1, 1));
        }

        // 截图窗与桌面内容逐像素相同：屏上必须有一圈提示线，用户才知道哪一块是这张截图。
        // 画在控件边缘、只走屏显这一条路——exportImage 从引擎取图，不会带上这一笔。
        paintCaptureFrame(painter);

        // 拖边时的像素放大镜排在最后：它要盖住边框与画面，用户正要看的就是光标落点
        paintCropMagnifier(painter);
    }

    void AnnotationView::paintCaptureFrame(QPainter &painter)
    {
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen{GyanisScribe::Gui::kAccentColor, 2});
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRect{QPoint{0, 0}, size()}.adjusted(1, 1, -1, -1));

        // 选择档下四角与四边中点摆出重裁抓点；已有标注会随裁切平移，不丢
        if (m_engine.activeTool() == Core::ToolKind::Select)
        {
            const auto                  frame   = QRect{QPoint{0, 0}, size()};
            const int                   middleX = (frame.left() + frame.right()) / 2;
            const int                   middleY = (frame.top() + frame.bottom()) / 2;
            const int                   edge    = GyanisScribe::Gui::kSelectionGripEdge;
            const std::array<QPoint, 8> centers{QPoint{frame.left(), frame.top()},    QPoint{middleX, frame.top()},          QPoint{frame.right(), frame.top()},
                                                QPoint{frame.right(), middleY},       QPoint{frame.right(), frame.bottom()}, QPoint{middleX, frame.bottom()},
                                                QPoint{frame.left(), frame.bottom()}, QPoint{frame.left(), middleY}};
            painter.setPen(QPen{GyanisScribe::Gui::kAccentColor, 1});
            painter.setBrush(GyanisScribe::Gui::kCaptureHintBackgroundColor);
            for (const auto &center: centers)
            {
                painter.drawRect(QRect{center - QPoint{edge / 2, edge / 2}, QSize{edge, edge}});
            }
        }
    }

    void AnnotationView::setCropPreview(const QImage &frame, const QRect &sourceRect, const QPointF &shapeOrigin, const QSizeF &previewLogicalSize)
    {
        m_cropPreviewFrame       = frame;
        m_cropPreviewSource      = sourceRect;
        m_cropPreviewShapeOrigin = shapeOrigin;
        m_cropPreviewLogicalSize = previewLogicalSize;
        m_hasCropPreview         = !frame.isNull() && !sourceRect.isEmpty();
        update();
    }

    void AnnotationView::clearCropPreview()
    {
        if (!m_hasCropPreview)
        {
            return;
        }
        m_hasCropPreview    = false;
        m_cropPreviewFrame  = {};
        m_cropPreviewSource = {};
        update();
    }

    void AnnotationView::requestDragGeometry(const QRect &geometry)
    {
        m_pendingDragGeometry    = geometry;
        m_hasPendingDragGeometry = true;
        if (!m_dragUpdateTimer.isActive())
        {
            // 第一次立即生效：按下后不该等到下一个 8ms 才看见窗口动
            m_dragUpdateTimer.start();
            applyPendingDragGeometry();
        }
    }

    void AnnotationView::applyPendingDragGeometry()
    {
        if (!m_hasPendingDragGeometry || window() == nullptr)
        {
            return;
        }
        const QRect geometry     = m_pendingDragGeometry;
        m_hasPendingDragGeometry = false;
        // 贴到屏边时每帧算回同一个矩形：不重复 setGeometry，省掉一次窗口重排
        if (window()->geometry() != geometry)
        {
            window()->setGeometry(geometry);
        }
        emit captureResizeMoved(geometry);
        // 放大镜随光标与窗口一起走；整窗重绘顺带把上一帧的放大镜从旧位置擦掉
        update();
    }

    void AnnotationView::flushPendingDragGeometry()
    {
        if (m_hasPendingDragGeometry)
        {
            applyPendingDragGeometry();
        }
        m_dragUpdateTimer.stop();
    }

    void AnnotationView::setCropMagnifierSource(const CropMagnifierSource &source)
    {
        m_cropMagnifierSource = source;
        update();
    }

    void AnnotationView::setMagnifierView(const Core::MagnifierView view)
    {
        m_magnifierView = view;
        update();
    }

    QRect AnnotationView::cropMagnifierRectForTesting() const
    {
        return cropMagnifierGeometry().view;
    }

    QRect AnnotationView::cropMagnifierReadoutRectForTesting() const
    {
        return cropMagnifierGeometry().readout;
    }

    QString AnnotationView::cropReadoutText() const
    {
        // 读数报的是「这一块新范围」而不是画布：拖边时用户核对的就是边框落点与成品尺寸
        const auto cursor = mapToGlobal(m_mousePointWidget);
        return QStringLiteral("%1×%2  (%3, %4)").arg(width()).arg(height()).arg(cursor.x()).arg(cursor.y());
    }

    AnnotationView::CropMagnifierGeometry AnnotationView::cropMagnifierGeometry() const
    {
        CropMagnifierGeometry geometry;
        // 只有拖抓点时才摆：整体移动时鼠标在图内，放大镜挡住的正是用户要对齐的画面
        if (!m_captureResizing || m_captureResizeGrip == Core::SelectionGrip::None || window() == nullptr)
        {
            return geometry;
        }

        const auto viewOrigin = mapToGlobal(QPoint{0, 0});
        const auto windowRect = window()->geometry();
        const auto bounds     = windowRect.isValid() && !windowRect.isEmpty() ? windowRect : QRect{viewOrigin, size()};
        // 取样夹在整屏冻帧里、摆放夹在本窗内：光标在边框上时，放大镜两侧的像素都要看得到
        const auto samplingBounds = m_cropMagnifierSource.frameGeometry.isValid() && !m_cropMagnifierSource.frameGeometry.isEmpty() ? m_cropMagnifierSource.frameGeometry : bounds;
        const auto sampleEdge     = Core::magnifierSampleEdgeFor(m_magnifierView);
        // 窗口比默认的 180 格还小时按短边缩一档：照原尺寸画会被裁成半张，格子就看不清了
        const int  viewEdge = std::max(Core::kMagnifierSampleEdge, std::min({Core::kMagnifierViewEdge, width(), height()}));
        const auto layout   = Core::magnifierLayoutFor(mapToGlobal(m_mousePointWidget), samplingBounds, bounds, sampleEdge > 0 ? sampleEdge : Core::kMagnifierSampleEdge, viewEdge);

        QRect viewRect = layout.viewRect.translated(-viewOrigin);
        // 窗口小于放大镜本身时夹回控件内：夹过之后仍按原取样范围放大，读数与落点不受影响
        viewRect.moveLeft(std::clamp(viewRect.left(), 0, std::max(0, width() - viewRect.width())));
        viewRect.moveTop(std::clamp(viewRect.top(), 0, std::max(0, height() - viewRect.height())));

        // 读数条按文字实际宽度撑开，但不许出控件；下方放不下就翻到插图上方
        QFont readoutFont = font();
        readoutFont.setPointSizeF(std::max<qreal>(GyanisScribe::Gui::kHintFontSize, font().pointSizeF()));
        readoutFont = tabularFont(readoutFont, TextScript::Mixed);
        const QFontMetrics metrics{readoutFont};
        const int          stripWidth = std::clamp(metrics.horizontalAdvance(cropReadoutText()) + 2 * GyanisScribe::Gui::kHintPadding, 1, std::max(1, width()));
        const int          stripLeft  = std::clamp(viewRect.left(), 0, std::max(0, width() - stripWidth));
        int                stripTop   = viewRect.bottom() + kMagnifierPenWidth;
        if (stripTop + kMagnifierStripHeight > height())
        {
            stripTop = std::max(0, viewRect.top() - kMagnifierPenWidth - kMagnifierStripHeight);
        }

        geometry.readout    = QRect{stripLeft, stripTop, stripWidth, kMagnifierStripHeight};
        geometry.source     = layout.sourceRect;
        geometry.sampleEdge = sampleEdge > 0 ? sampleEdge : Core::kMagnifierSampleEdge;
        geometry.cellSize   = layout.cellSize;
        // 「不显示」只收像素插图；读数条照旧留在原位，与截图遮罩那一档同一口径
        if (m_magnifierView != Core::MagnifierView::Off && !m_cropMagnifierSource.frame.isNull())
        {
            geometry.view = viewRect;
        }
        return geometry;
    }

    void AnnotationView::paintCropMagnifier(QPainter &painter)
    {
        const auto geometry = cropMagnifierGeometry();
        if (geometry.readout.isEmpty())
        {
            return;
        }

        if (!geometry.view.isEmpty())
        {
            const auto &frame      = m_cropMagnifierSource.frame;
            const auto  cropOrigin = Core::framePixelFor(geometry.source.topLeft(), m_cropMagnifierSource.frameGeometry, m_cropMagnifierSource.devicePixelRatio, frame.size());
            const auto  cropSpan   = static_cast<int>(std::lround(geometry.sampleEdge * m_cropMagnifierSource.devicePixelRatio));
            const auto  cropSize   = QSize{std::clamp(cropSpan, 1, frame.width() - cropOrigin.x()), std::clamp(cropSpan, 1, frame.height() - cropOrigin.y())};

            // 像素格用最近邻放大：Smooth 会把格子糊成一片，放大镜就没有存在的意义了
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawImage(geometry.view, frame.copy(QRect{cropOrigin, cropSize}));

            painter.setPen(QPen{GyanisScribe::Gui::kAccentColor, kMagnifierPenWidth});
            painter.drawRect(geometry.view);
            painter.setPen(QPen{GyanisScribe::Gui::kCrosshairColor, 1});
            for (auto step = 1; step < geometry.sampleEdge; ++step)
            {
                const auto offset = step * geometry.cellSize;
                painter.drawLine(geometry.view.left() + offset, geometry.view.top(), geometry.view.left() + offset, geometry.view.bottom());
                painter.drawLine(geometry.view.left(), geometry.view.top() + offset, geometry.view.right(), geometry.view.top() + offset);
            }
            painter.restore();
        }

        QFont readoutFont = font();
        readoutFont.setPointSizeF(std::max<qreal>(GyanisScribe::Gui::kHintFontSize, font().pointSizeF()));
        readoutFont = tabularFont(readoutFont, TextScript::Mixed);

        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(GyanisScribe::Gui::kCaptureHintBackgroundColor);
        painter.drawRect(geometry.readout);
        painter.setFont(readoutFont);
        painter.setPen(GyanisScribe::Gui::kCaptureHintTextColor);
        painter.drawText(geometry.readout.adjusted(GyanisScribe::Gui::kHintPadding, 0, -GyanisScribe::Gui::kHintPadding, 0), Qt::AlignVCenter | Qt::AlignLeft, cropReadoutText());
        painter.restore();
    }

    void AnnotationView::mousePressEvent(QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton)
        {
            QWidget::mousePressEvent(event);
            return;
        }

        const auto rect   = currentDisplayRect();
        const auto point  = toCanvasPoint(event->pos(), rect, m_engine.canvas().logicalSize);
        m_lastCanvasPoint = point;

        // 手柄先于窗口抓点：图形手柄可能正落在窗口边缘的抓点带里，那一点用户要拉的是图形
        if (const auto handle = handleAt(event->pos()); handle != Core::SelectionHandle::None)
        {
            if (auto begun = m_engine.beginSelectionDrag(handle, point); begun)
            {
                m_handleDragging = true;
            } else
            {
                Base::Logger::warning(std::format("手柄拖动没开场：{}", begun.error().message));
            }
            event->accept();
            return;
        }

        // 没抓住手柄才轮到窗口边缘：选择档下贴着窗口四边/四角按下＝重裁截图范围；重抓后图形会随裁切平移
        if (m_engine.activeTool() == Core::ToolKind::Select)
        {
            const auto grip = Core::gripAt(QRect{QPoint{0, 0}, size()}, event->pos(), Core::kSelectionGripTolerance);
            if (grip != Core::SelectionGrip::None && grip != Core::SelectionGrip::Inside)
            {
                m_captureResizing               = true;
                m_captureResizeGrip             = grip;
                m_captureResizeStartGlobal      = event->globalPosition().toPoint();
                m_captureResizeOriginalGeometry = window()->geometry();
                m_mousePointWidget              = event->pos();
                // 按下就亮出放大镜：拖边最需要读数的是「压住边框那一刻」，不必等第一次移动
                update();
                event->accept();
                return;
            }
        }

        // 再问命中：点中已有图形就选中并准备拖动，点空白才起笔画新的
        if (const auto hit = m_engine.selectionAt(point); hit)
        {
            const auto modifiers = event->modifiers();
            // Ctrl 反选、Shift 加选：都是「改选中集」的意思，不再进拖动
            if (modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::ShiftModifier))
            {
                const auto changed = modifiers.testFlag(Qt::ControlModifier) ? m_engine.toggleSelection(*hit) : m_engine.addToSelection(*hit);
                if (!changed)
                {
                    Base::Logger::warning(std::format("多选没改成：{}", changed.error().message));
                }
                event->accept();
                return;
            }
            // 已经选中着的再按住：保持整组选中并进入拖动——多选之后拖其中任意一条都是整组挪，
            // 塌缩成单选的时机是「按在没选中的图形上」
            const bool alreadySelected = std::any_of(m_engine.selections().begin(), m_engine.selections().end(),
                                                     [hit](const Core::AnnotationSelection &item) { return item.tool == hit->tool && item.shapeIndex == hit->shapeIndex; });
            if (!alreadySelected)
            {
                if (auto selected = m_engine.select(*hit); !selected)
                {
                    Base::Logger::warning(std::format("命中测试给出的图形没能选中：{}", selected.error().message));
                    return;
                }
            }
            // 这一按是一趟新拖动的起点：与上一趟之间要断开撤销合并
            m_engine.beginMoveGesture();
            m_dragging = true;
            update();
            event->accept();
            return;
        }

        // 点空白：先清掉选中再起笔——选框不该留着指向「刚才那条」
        m_engine.clearSelection();
        if (m_engine.activeTool() == Core::ToolKind::Select)
        {
            // 图内空白按住可整体挪动截图范围；到松手都没超过手抖阈值就仍按单击清选中
            m_captureMovePending          = true;
            m_captureMoving               = false;
            m_captureMoveStartGlobal      = event->globalPosition().toPoint();
            m_captureMoveOriginalGeometry = window()->geometry();
            m_mousePointWidget            = event->pos();
            event->accept();
            return;
        }
        m_engine.beginStroke(point);
        m_stroking = true;

        // 文字工具的图形要有内容才算成立，所以起笔之后立刻摆输入框，由输入框提交而不是抬笔提交
        if (m_engine.activeTool() == Core::ToolKind::Text)
        {
            m_awaitingText = true;
            openTextEditor(point, QString{}, false);
        }
        event->accept();
    }

    void AnnotationView::mouseMoveEvent(QMouseEvent *event)
    {
        m_mousePointWidget = event->pos();

        const auto rect  = currentDisplayRect();
        const auto point = toCanvasPoint(event->pos(), rect, m_engine.canvas().logicalSize);

        if (m_captureResizing)
        {
            const auto available = window()->screen() != nullptr ? window()->screen()->availableGeometry() : m_captureResizeOriginalGeometry;
            const auto delta     = event->globalPosition().toPoint() - m_captureResizeStartGlobal;
            const auto moved     = Core::dragRegionByGrip(m_captureResizeOriginalGeometry, m_captureResizeGrip, delta, available);
            // 少于 8px 的窗口既放不下工具栏也点不中边框：几何不跟，但仍按拍刷新放大镜
            requestDragGeometry(moved.width() >= 8 && moved.height() >= 8 ? moved : window()->geometry());
            event->accept();
            return;
        }

        if (m_captureMovePending || m_captureMoving)
        {
            const auto delta = event->globalPosition().toPoint() - m_captureMoveStartGlobal;
            if (!m_captureMoving)
            {
                // 几像素的手抖仍是单击：清掉选中是原本的行为，不能顺手把窗口挪走
                if (std::abs(delta.x()) + std::abs(delta.y()) <= kCaptureMoveSlop)
                {
                    event->accept();
                    return;
                }
                m_captureMoving = true;
            }

            const auto available = window()->screen() != nullptr ? window()->screen()->availableGeometry() : m_captureMoveOriginalGeometry;
            const auto moved     = Core::dragRegionByGrip(m_captureMoveOriginalGeometry, Core::SelectionGrip::Inside, delta, available);
            requestDragGeometry(moved);
            event->accept();
            return;
        }

        if (m_engine.activeTool() == Core::ToolKind::Select)
        {
            const auto grip = Core::gripAt(QRect{QPoint{0, 0}, size()}, event->pos(), Core::kSelectionGripTolerance);
            setCursor(Core::cursorForGrip(grip));
        } else
        {
            setCursor(Qt::CrossCursor);
        }

        if (m_stroking)
        {
            m_engine.moveStroke(point);
            m_lastCanvasPoint = point;
            update();
            event->accept();
            return;
        }
        if (m_handleDragging)
        {
            // 每次都从开场那份几何重算，失败（那条被撤没了）就安静跳过：拖动途中的报错只会刷屏
            if (auto dragged = m_engine.updateSelectionDrag(point, selectionDragModifiersOf(event->modifiers())); dragged)
            {
                update();
            }
            m_lastCanvasPoint = point;
            event->accept();
            return;
        }
        if (m_dragging)
        {
            if (auto moved = m_engine.moveSelectionBy(point - m_lastCanvasPoint); moved)
            {
                m_lastCanvasPoint = point;
                update();
            }
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void AnnotationView::mouseReleaseEvent(QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton)
        {
            QWidget::mouseReleaseEvent(event);
            return;
        }

        if (m_captureResizing)
        {
            m_captureResizing = false;
            setCursor(Qt::ArrowCursor);
            // 松手落在两个节拍之间时，最后一帧还没落：先冲刷，交出去的才是光标停下的那一块
            flushPendingDragGeometry();
            clearCropPreview();
            emit captureResized(window()->geometry());
            // 放大镜只属于拖动那一瞬：松手必须立刻收掉，别让它压在刚定下来的边框上
            update();
            event->accept();
            return;
        }

        if (m_captureMovePending || m_captureMoving)
        {
            const bool moved     = m_captureMoving;
            m_captureMovePending = false;
            m_captureMoving      = false;
            flushPendingDragGeometry();
            update();
            if (moved)
            {
                emit captureResized(window()->geometry());
            }
            event->accept();
            return;
        }

        if (m_handleDragging)
        {
            m_engine.endSelectionDrag();
            m_handleDragging = false;
            event->accept();
            return;
        }

        if (m_dragging)
        {
            // 这一趟拖动到此为止：松手之后的微调与下一趟拖动各自另算一步撤销
            m_engine.endMoveGesture();
            m_dragging = false;
        }
        if (m_ignoreNextRelease)
        {
            // 上一条事件是「双击收笔」：这一下松手不再走 endStroke（在途图形已经落定了）
            m_ignoreNextRelease = false;
            return;
        }
        if (m_stroking)
        {
            // 抬笔点也要当成一次行进：有的拖拽最后一跳只发 release 不发 move，少这一步图形就会停在半路
            const auto released = toCanvasPoint(event->pos(), currentDisplayRect(), m_engine.canvas().logicalSize);
            m_engine.moveStroke(released);

            // 文字那一条由输入框收笔，这里收会把还没填内容的图形塞进撤销栈
            if (!m_awaitingText)
            {
                if (auto settled = m_engine.endStroke(); settled)
                {
                    // 刚画完的那条立刻选中：下一步就能直接拖它，不用再对准一次
                    if (auto selected = m_engine.select(*settled); !selected)
                    {
                        Base::Logger::warning(std::format("新画好的图形没能选中：{}", selected.error().message));
                    }
                } else
                {
                    // 退化图形（点一下没拖动）不是错误，报一条 Info 让用户知道为什么没留下东西
                    Base::Logger::info(std::format("这一笔没有落成图形：{}", settled.error().message));
                }
            }
            m_stroking = false;
        }
        event->accept();
    }

    void AnnotationView::openTextEditor(const QPoint &canvasPoint, const QString &initial, const bool reEditing)
    {
        const auto rect   = currentDisplayRect();
        const auto widget = toWidgetPoint(canvasPoint, rect, m_engine.canvas().logicalSize);

        if (m_textEditor != nullptr)
        {
            m_textEditor->deleteLater();
        }

        m_reEditingText = reEditing;
        m_textEditor    = new Gui::TextEdit{Core::CornerRole::Control, this};
        // 样式表按这个名字给它输入类那一档的样子：不挂名字就会落到识别结果区那条宽内边距的规则上
        m_textEditor->setObjectName(QStringLiteral("annotationTextEditor"));
        m_textEditor->setAcceptRichText(false);
        // 只在回车处断行（不软换行）：屏上看到的断行就是成品里的断行，不会多出几处凭空折的行
        m_textEditor->setLineWrapMode(QTextEdit::NoWrap);
        m_textEditor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEditor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // Tab 交回界面（与单行时代同解）：在标注里插个制表符没有意义
        m_textEditor->setTabChangesFocus(true);
        m_textEditor->setPlaceholderText(GyanisScribe::Base::uiText("输入文字，按回车键换行，按 Ctrl+回车提交"));
        // 字体与成品同一处拼出：字号、字体族、粗斜体在输入时与提交后才会一致
        const QFont editorFont = m_engine.textInputFont();
        m_textEditor->setFont(editorFont);
        m_textEditor->setPlainText(initial);
        // 重编时光标落到文末，不摆全选：全选之下按个回车就把原文整段替成一个换行，
        // 而多行编辑里「接着往下写一行」才是重开这一格的正事；要整段换掉按 Ctrl+A 自选
        m_textEditor->moveCursor(QTextCursor::End);
        // 宽度夹在显示区内：贴着右边缘点下的文字框不该跑到画布外面去
        const auto width = std::min(kTextEditorWidth, std::max(40, rect.right() - widget.x()));
        // 按下点是首行基线；输入框上沿要退一个 ascent 再加文档边距与边框，提交时文字才不会往上跳
        const QFontMetricsF editorMetrics{editorFont};
        const auto          editorTop =
                widget.y() - static_cast<int>(std::ceil(editorMetrics.ascent() + m_textEditor->document()->documentMargin() + static_cast<qreal>(m_textEditor->frameWidth())));
        m_textEditor->setGeometry(widget.x(), editorTop, width, kTextEditorMinimumHeight);
        m_textEditor->show();
        m_textEditor->setFocus();
        resizeTextEditorToContent();

        QObject::connect(m_textEditor, &QTextEdit::textChanged, this, [this] { resizeTextEditorToContent(); });
        // Esc、Ctrl+回车与失焦由本视图的事件过滤器接：QTextEdit 三者都没有对应的信号
        m_textEditor->installEventFilter(this);
    }

    void AnnotationView::resizeTextEditorToContent()
    {
        if (m_textEditor == nullptr)
        {
            return;
        }

        // 按段落数乘行距自己算内容高度：QTextDocument::size() 要等版面算完才准，
        // 而这里刚 setPlainText 完、也还没进事件循环，自己算不会量到一个半成品的值。
        // 只补文档内边距与边框，不计样式表的上下内边距——#annotationTextEditor 那条规则里上下是 0
        const QFontMetricsF editorMetrics{m_textEditor->font()};
        const auto          margin        = m_textEditor->document()->documentMargin();
        const auto          contentHeight = static_cast<int>(std::ceil(editorMetrics.lineSpacing() * static_cast<qreal>(m_textEditor->document()->blockCount()))) +
                                            static_cast<int>(2 * margin) + 2 * m_textEditor->frameWidth() + kTextEditorHeightPad;
        const auto          maximum       = std::max(kTextEditorMinimumHeight, currentDisplayRect().height());
        m_textEditor->resize(m_textEditor->width(), std::clamp(contentHeight, kTextEditorMinimumHeight, maximum));
    }

    void AnnotationView::commitTextEditor()
    {
        if (m_textEditor == nullptr)
        {
            return;
        }

        const QString text      = textWithoutTrailingBlankLines(m_textEditor->toPlainText());
        const bool    reEditing = m_reEditingText;
        closeTextEditor();

        if (reEditing)
        {
            // 改已放下的那一条：走撤销栈，界面不碰图形本体
            if (auto applied = m_engine.setTextOfSelection(text); !applied)
            {
                Base::Logger::warning(std::format("这条文字没能改成：{}", applied.error().message));
            }
            m_reEditingText = false;
            update();
            return;
        }

        // 提交路径：先填内容再收笔，收笔会把这条图形放进撤销栈
        if (auto filled = m_engine.setPendingText(text); !filled)
        {
            Base::Logger::warning(std::format("文字标注没能落定：{}", filled.error().message));
        }
        if (auto settled = m_engine.endStroke(); !settled)
        {
            Base::Logger::info(std::format("这条文字标注是空的，没有留下图形"));
        }
        m_stroking     = false;
        m_awaitingText = false;
    }

    void AnnotationView::cancelTextEditor()
    {
        // 重编那一格的 Esc 是「不改了」：原内容一个字都没动，不该去收笔也不该压撤销
        if (m_reEditingText)
        {
            m_reEditingText = false;
            closeTextEditor();
            return;
        }

        // 取消这一条：文本没提交过，引擎会把这条退化图形拒掉——拒掉正是「取消成功」的样子
        if (auto cancelled = m_engine.endStroke(); cancelled)
        {
            Base::Logger::warning("按 Esc 取消文字标注，但引擎仍然收下了一条图形：这条不该走到这里");
        }
        m_stroking     = false;
        m_awaitingText = false;
        closeTextEditor();
    }

    void AnnotationView::closeTextEditor()
    {
        if (m_textEditor == nullptr)
        {
            return;
        }

        m_textEditor->deleteLater();
        m_textEditor = nullptr;
    }

    void AnnotationView::mouseDoubleClickEvent(QMouseEvent *event)
    {
        if (event->button() != Qt::LeftButton)
        {
            QWidget::mouseDoubleClickEvent(event);
            return;
        }

        const auto rect  = currentDisplayRect();
        const auto point = toCanvasPoint(event->pos(), rect, m_engine.canvas().logicalSize);
        const auto hit   = m_engine.selectionAt(point);
        // 有在途笔画或输入框开着时不抢这条双击：那一次按下属于正在输入的那一条
        const bool busy = m_stroking || m_awaitingText || m_textEditor != nullptr;

        // 折线：双击就是「就到这里」。收笔之后还要吞掉随后那一次松手——
        // 那一下在 Qt 的事件序里跟在双击后面，收过笔的在途图形已经不在了，再收一次会报「没有落定」
        if (!busy && m_engine.activeTool() == Core::ToolKind::Polyline)
        {
            if (const auto finished = m_engine.finishPendingStroke(); finished)
            {
                if (auto selected = m_engine.select(*finished); !selected)
                {
                    Base::Logger::warning(std::format("刚收笔的折线没能选中：{}", selected.error().message));
                }
                m_ignoreNextRelease = true;
            } else
            {
                Base::Logger::info(std::format("这次双击没有可收笔的折线：{}", finished.error().message));
            }
            event->accept();
            return;
        }

        if (hit && hit->tool == Core::ToolKind::Text && !busy)
        {
            if (auto selected = m_engine.select(*hit); selected)
            {
                openTextEditor(point, m_engine.textOfSelection().value_or(QString{}), true);
            } else
            {
                Base::Logger::warning(std::format("双击要改的那条文字没能选中：{}", selected.error().message));
            }
            event->accept();
            return;
        }

        QWidget::mouseDoubleClickEvent(event);
    }

    bool AnnotationView::eventFilter(QObject *watched, QEvent *event)
    {
        if (watched != m_textEditor)
        {
            return QWidget::eventFilter(watched, event);
        }

        if (event->type() == QEvent::KeyPress)
        {
            const auto *key = dynamic_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape)
            {
                cancelTextEditor();
                return true;
            }
            // 多行输入里回车只负责换行，提交改由 Ctrl+回车承担；这一格得在 QTextEdit 自己处理之前接，
            // 按晚了换行就先插进去了，提交下来图形尾巴上会多一行空白
            if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && key->modifiers().testFlag(Qt::ControlModifier))
            {
                commitTextEditor();
                return true;
            }
        }

        // 点到别处（画布、工具栏、别的窗口）就是「就这些字」：与单行时代 QLineEdit 的 editingFinished 同解
        if (event->type() == QEvent::FocusOut)
        {
            commitTextEditor();
        }

        return QWidget::eventFilter(watched, event);
    }

    QPointF AnnotationView::rotateHandlePointWidget(const std::array<QPointF, 8> &frame) const
    {
        const auto rect     = currentDisplayRect();
        const auto toWidget = [this, &rect](const QPointF &canvasPoint)
        { return QPointF{toWidgetPoint(QPoint{qRound(canvasPoint.x()), qRound(canvasPoint.y())}, rect, m_engine.canvas().logicalSize)}; };

        // 次序见 Core::SelectionHandle：索引 1 是上边中点、索引 5 是下边中点
        const QPointF topMiddle    = toWidget(frame[1]);
        const QPointF bottomMiddle = toWidget(frame[5]);
        QPointF       direction    = topMiddle - bottomMiddle;
        const qreal   length       = std::hypot(direction.x(), direction.y());
        direction                  = length > 0.0 ? direction / length : QPointF{0.0, -1.0};

        const QPointF candidate = topMiddle + direction * kRotateHandleOffsetWidget;
        if (!QRectF{this->rect()}.contains(candidate))
        {
            // 图形贴着上边缘时柄会跑到窗口外：翻到下边，用户还抓得着
            const QPointF flipped = bottomMiddle - direction * kRotateHandleOffsetWidget;
            if (QRectF{this->rect()}.contains(flipped))
            {
                return flipped;
            }
        }
        return candidate;
    }

    Core::SelectionHandle AnnotationView::handleAt(const QPoint &widgetPoint) const
    {
        const auto frame = m_engine.selectionFrame();
        if (!frame)
        {
            return Core::SelectionHandle::None;
        }

        const auto            rect         = currentDisplayRect();
        Core::SelectionHandle best         = Core::SelectionHandle::None;
        qreal                 bestDistance = kHandleHitRadiusWidget;

        const auto consider = [&](const QPointF &candidate, const Core::SelectionHandle handle)
        {
            const qreal distance = std::hypot(candidate.x() - widgetPoint.x(), candidate.y() - widgetPoint.y());
            if (distance <= bestDistance)
            {
                best         = handle;
                bestDistance = distance;
            }
        };

        // 八枚手柄点按 Enum 的次序逐位对应（TopLeft=1 … Left=8）
        for (int node = 0; node < 8; ++node)
        {
            const QPoint widget = toWidgetPoint(QPoint{qRound((*frame)[node].x()), qRound((*frame)[node].y())}, rect, m_engine.canvas().logicalSize);
            consider(QPointF{widget}, static_cast<Core::SelectionHandle>(node + 1));
        }
        consider(rotateHandlePointWidget(*frame), Core::SelectionHandle::Rotate);
        return best;
    }

} // namespace GyanisScribe::Gui
