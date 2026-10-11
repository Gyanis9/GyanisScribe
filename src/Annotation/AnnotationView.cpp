#include "Annotation/AnnotationView.h"

#include "Core/ImageFit.h"
#include "Core/Logger.h"
#include "Core/Text.h"
#include "Ui/DesignTokens.h"

#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

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
        if (const auto bounds = m_engine.selectionBounds(); bounds)
        {
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

        // 先问命中：点中已有图形就选中并准备拖动，点空白才起笔画新的
        if (const auto hit = m_engine.selectionAt(point); hit)
        {
            if (auto selected = m_engine.select(*hit); !selected)
            {
                Core::Logger::warning(std::format("命中测试给出的图形没能选中：{}", selected.error().message));
                return;
            }
            m_dragging = true;
            update();
            event->accept();
            return;
        }

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

        m_dragging = false;
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

} // namespace GyanisScribe::Annotation
