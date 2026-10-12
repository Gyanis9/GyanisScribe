#include "Annotation/AnnotationView.h"

#include "Core/ImageFit.h"
#include "Core/Logger.h"
#include "Core/Text.h"
#include "Ui/DesignTokens.h"

#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <format>

namespace
{
    using GyanisScribe::Annotation::IAnnotationEngine;

    /// 行内文字输入框的默认宽度，逻辑像素：再窄就打不开一个像样的句子
    constexpr int kTextEditorWidth = 180;

    /// 输入框的高度余量，逻辑像素：贴着字号摆会让光标被裁掉一半
    constexpr int kTextEditorHeightPad = 6;
} // namespace

namespace GyanisScribe::Annotation
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

    AnnotationView::AnnotationView(IAnnotationEngine &engine, QWidget *parent) : QWidget(parent), m_engine(engine)
    {
        setMouseTracking(false);
        setCursor(Qt::CrossCursor);
        // 引擎的图形变了就重绘：这里不缓存合成图，在途的那一条也要立刻看得见
        QObject::connect(&engine, &IAnnotationEngine::canvasChanged, this, [this] { update(); });
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
        const auto &canvas = m_engine.canvas();
        const auto  rect   = currentDisplayRect();
        if (canvas.image.isNull() || rect.isEmpty())
        {
            return;
        }

        QPainter painter{this};
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        // 底图按物理像素给源矩形、按逻辑矩形给目标：这样高 DPI 下两者对齐，不需要再乘一遍倍比
        painter.drawImage(QRectF{rect}, canvas.image, QRectF{{0.0, 0.0}, QSizeF{canvas.image.size()}});

        // 图形画在「显示区 + 逻辑坐标」这一层：工具的坐标记的就是逻辑值
        const auto shapeScale = static_cast<qreal>(rect.width()) / canvas.logicalSize.width();
        painter.save();
        painter.translate(rect.topLeft());
        painter.scale(shapeScale, shapeScale);
        m_engine.paintShapes(painter);
        painter.restore();

        // 选框走控件坐标而不是逻辑坐标：线宽与虚线间隔不该随缩放变粗变细
        if (const auto frame = m_engine.selectionFrame(); frame)
        {
            // 单条选中：画跟着图形一起转的定向框，四角连成多边形
            const auto toWidget = [this, &rect, &canvas](const QPointF &canvasPoint)
            { return QPointF{toWidgetPoint(QPoint{qRound(canvasPoint.x()), qRound(canvasPoint.y())}, rect, canvas.logicalSize)}; };

            QPolygonF outline;
            for (const int node: {0, 2, 4, 6})
            {
                outline << toWidget((*frame)[node]);
            }
            QPen outlinePen{GyanisScribe::Ui::kAccentColor, 1.0, Qt::DashLine};
            outlinePen.setCapStyle(Qt::FlatCap);
            painter.setPen(outlinePen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(outline);

            // 缩放八枚手柄：白底描边的小方块，压在图形上也看得见
            QPen handlePen{GyanisScribe::Ui::kAccentColor, 1.0};
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
            QPen outline{GyanisScribe::Ui::kAccentColor, 1.0, Qt::DashLine};
            outline.setCapStyle(Qt::FlatCap);
            painter.setPen(outline);
            painter.setBrush(Qt::NoBrush);
            const auto topLeft     = toWidgetPoint(bounds->topLeft(), rect, canvas.logicalSize);
            const auto bottomRight = toWidgetPoint(bounds->bottomRight(), rect, canvas.logicalSize);
            painter.drawRect(QRect{topLeft, bottomRight}.adjusted(0, 0, 1, 1));
        }
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

        // 手柄压在最前面问：它们坐在选中框的角与边中点上，那些点往往正落在图形自己的命中区里
        if (const auto handle = handleAt(event->pos()); handle != SelectionHandle::None)
        {
            if (auto begun = m_engine.beginSelectionDrag(handle, point); begun)
            {
                m_handleDragging = true;
            } else
            {
                Core::Logger::warning(std::format("手柄拖动没开场：{}", begun.error().message));
            }
            event->accept();
            return;
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
                    Core::Logger::warning(std::format("多选没改成：{}", changed.error().message));
                }
                event->accept();
                return;
            }
            // 已经选中着的再按住：保持整组选中并进入拖动——多选之后拖其中任意一条都是整组挪，
            // 塌缩成单选的时机是「按在没选中的图形上」
            const bool alreadySelected = std::any_of(m_engine.selections().begin(), m_engine.selections().end(),
                                                     [hit](const AnnotationSelection &item) { return item.tool == hit->tool && item.shapeIndex == hit->shapeIndex; });
            if (!alreadySelected)
            {
                if (auto selected = m_engine.select(*hit); !selected)
                {
                    Core::Logger::warning(std::format("命中测试给出的图形没能选中：{}", selected.error().message));
                    return;
                }
            }
            m_dragging = true;
            update();
            event->accept();
            return;
        }

        // 点空白：先清掉选中再起笔——选框不该留着指向「刚才那条」
        m_engine.clearSelection();
        m_engine.beginStroke(point);
        m_stroking = true;

        // 文字工具的图形要有内容才算成立，所以起笔之后立刻摆输入框，由输入框提交而不是抬笔提交
        if (m_engine.activeTool() == ToolKind::Text)
        {
            m_awaitingText = true;
            openTextEditor(point, QString{}, false);
        }
        event->accept();
    }

    void AnnotationView::mouseMoveEvent(QMouseEvent *event)
    {
        const auto rect  = currentDisplayRect();
        const auto point = toCanvasPoint(event->pos(), rect, m_engine.canvas().logicalSize);

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
            if (auto dragged = m_engine.updateSelectionDrag(point); dragged)
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

        if (m_handleDragging)
        {
            m_engine.endSelectionDrag();
            m_handleDragging = false;
            event->accept();
            return;
        }

        m_dragging = false;
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
                        Core::Logger::warning(std::format("新画好的图形没能选中：{}", selected.error().message));
                    }
                } else
                {
                    // 退化图形（点一下没拖动）不是错误，报一条 Info 让用户知道为什么没留下东西
                    Core::Logger::info(std::format("这一笔没有落成图形：{}", settled.error().message));
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
        m_textEditor    = new QLineEdit{this};
        m_textEditor->setPlaceholderText(GyanisScribe::Core::uiText("输入文字后回车"));
        m_textEditor->setText(initial);
        // 重编时全选着打开：改的是这一条，全选后直接打字就换掉，不必先手动清
        m_textEditor->selectAll();
        m_textEditor->setFont(font());
        const auto height = m_textEditor->sizeHint().height() + kTextEditorHeightPad;
        // 宽度夹在显示区内：贴着右边缘点下的文字框不该跑到画布外面去
        const auto width = std::min(kTextEditorWidth, std::max(40, rect.right() - widget.x()));
        m_textEditor->setGeometry(widget.x(), widget.y(), width, height);
        m_textEditor->show();
        m_textEditor->setFocus();

        QObject::connect(m_textEditor, &QLineEdit::editingFinished, this,
                         [this, editor = m_textEditor, reEditing]
                         {
                             if (reEditing)
                             {
                                 // 改已放下的那一条：走撤销栈，界面不碰图形本体
                                 if (auto applied = m_engine.setTextOfSelection(editor->text()); !applied)
                                 {
                                     Core::Logger::warning(std::format("这条文字没能改成：{}", applied.error().message));
                                 }
                                 m_reEditingText = false;
                                 editor->deleteLater();
                                 if (m_textEditor == editor)
                                 {
                                     m_textEditor = nullptr;
                                 }
                                 update();
                                 return;
                             }

                             // 提交路径：先填内容再收笔，收笔会把这条图形放进撤销栈
                             if (auto text = m_engine.setPendingText(editor->text()); !text)
                             {
                                 Core::Logger::warning(std::format("文字标注没能落定：{}", text.error().message));
                             }
                             if (auto settled = m_engine.endStroke(); !settled)
                             {
                                 Core::Logger::info(std::format("这条文字标注是空的，没有留下图形"));
                             }
                             m_stroking     = false;
                             m_awaitingText = false;
                             editor->deleteLater();
                             if (m_textEditor == editor)
                             {
                                 m_textEditor = nullptr;
                             }
                         });
        // Esc 由本视图的事件过滤器接：QLineEdit 没有对应的信号，只有受保护的虚函数
        m_textEditor->installEventFilter(this);
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
        if (!busy && m_engine.activeTool() == ToolKind::Polyline)
        {
            if (const auto finished = m_engine.finishPendingStroke(); finished)
            {
                if (auto selected = m_engine.select(*finished); !selected)
                {
                    Core::Logger::warning(std::format("刚收笔的折线没能选中：{}", selected.error().message));
                }
                m_ignoreNextRelease = true;
            } else
            {
                Core::Logger::info(std::format("这次双击没有可收笔的折线：{}", finished.error().message));
            }
            event->accept();
            return;
        }

        if (hit && hit->tool == ToolKind::Text && !busy)
        {
            if (auto selected = m_engine.select(*hit); selected)
            {
                openTextEditor(point, m_engine.textOfSelection().value_or(QString{}), true);
            } else
            {
                Core::Logger::warning(std::format("双击要改的那条文字没能选中：{}", selected.error().message));
            }
            event->accept();
            return;
        }

        QWidget::mouseDoubleClickEvent(event);
    }

    bool AnnotationView::eventFilter(QObject *watched, QEvent *event)
    {
        if (watched == m_textEditor && event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
        {
            if (m_reEditingText)
            {
                // 重编那一格的 Esc 是「不改了」：原内容一个字都没动，不该去收笔也不该压撤销
                m_reEditingText = false;
                m_textEditor->deleteLater();
                m_textEditor = nullptr;
                return true;
            }
            // 取消这一条：文本没提交过，引擎会把这条退化图形拒掉——拒掉正是「取消成功」的样子
            if (auto cancelled = m_engine.endStroke(); cancelled)
            {
                Core::Logger::warning("按 Esc 取消文字标注，但引擎仍然收下了一条图形：这条不该走到这里");
            }
            m_stroking     = false;
            m_awaitingText = false;
            m_textEditor->deleteLater();
            m_textEditor = nullptr;
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }

    QPointF AnnotationView::rotateHandlePointWidget(const std::array<QPointF, 8> &frame) const
    {
        const auto rect     = currentDisplayRect();
        const auto toWidget = [this, &rect](const QPointF &canvasPoint)
        { return QPointF{toWidgetPoint(QPoint{qRound(canvasPoint.x()), qRound(canvasPoint.y())}, rect, m_engine.canvas().logicalSize)}; };

        // 次序见 SelectionHandle：索引 1 是上边中点、索引 5 是下边中点
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

    SelectionHandle AnnotationView::handleAt(const QPoint &widgetPoint) const
    {
        const auto frame = m_engine.selectionFrame();
        if (!frame)
        {
            return SelectionHandle::None;
        }

        const auto      rect         = currentDisplayRect();
        SelectionHandle best         = SelectionHandle::None;
        qreal           bestDistance = kHandleHitRadiusWidget;

        const auto consider = [&](const QPointF &candidate, const SelectionHandle handle)
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
            consider(QPointF{widget}, static_cast<SelectionHandle>(node + 1));
        }
        consider(rotateHandlePointWidget(*frame), SelectionHandle::Rotate);
        return best;
    }

} // namespace GyanisScribe::Annotation
