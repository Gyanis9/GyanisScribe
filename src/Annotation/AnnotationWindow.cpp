#include "Annotation/AnnotationWindow.h"

#include "Annotation/AnnotationEngine.h"
#include "Annotation/IAnnotationEngine.h"
#include "Annotation/AnnotationToolbarGeometry.h"
#include "Core/Logger.h"
#include "Ui/KeyStep.h"
#include "Core/Types.h"

#include <QChar>
#include <QKeyEvent>
#include <QVBoxLayout>

#include <format>
#include <utility>

namespace
{
    /// 按住 Shift 时一次挪多少格：一格太慢、十格在 4K 屏上仍是「一下就跳过目标」，取中间值
    constexpr int kCoarseNudgeStride = 10;
} // namespace

namespace GyanisScribe::Annotation
{
    ShortcutAction shortcutActionFor(const int key, const Qt::KeyboardModifiers modifiers)
    {
        const auto control = modifiers.testFlag(Qt::ControlModifier);
        const auto shift   = modifiers.testFlag(Qt::ShiftModifier);

        switch (key)
        {
            case Qt::Key_Escape:
                // 关闭不带修饰键才认：Esc 在 Qt 里本就有「退出当前操作」的含义，加修饰键反而歧义
                return modifiers == Qt::NoModifier ? ShortcutAction::Close : ShortcutAction::None;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                return modifiers == Qt::NoModifier ? ShortcutAction::Copy : ShortcutAction::None;
            case Qt::Key_Z:
                // Ctrl+Z 撤销、Ctrl+Shift+Z 重做：两个方向共用一个键位是惯例，反过来不认
                return control ? (shift ? ShortcutAction::Redo : ShortcutAction::Undo) : ShortcutAction::None;
            case Qt::Key_Y:
                return control ? ShortcutAction::Redo : ShortcutAction::None;
            case Qt::Key_S:
                return control ? ShortcutAction::Save : ShortcutAction::None;
            case Qt::Key_D:
                return control ? ShortcutAction::Pin : ShortcutAction::None;
            case Qt::Key_O:
                return control ? ShortcutAction::Ocr : ShortcutAction::None;
            case Qt::Key_Delete:
            case Qt::Key_Backspace:
                return modifiers == Qt::NoModifier ? ShortcutAction::DeleteSelection : ShortcutAction::None;
            case Qt::Key_Left:
            case Qt::Key_Right:
            case Qt::Key_Up:
            case Qt::Key_Down:
                // 带 Ctrl 就不认：行内的文字框是 QLineEdit，Ctrl+方向键在它那儿是按词移光标，截走就少了这条既有行为
                return modifiers == Qt::NoModifier || shift ? ShortcutAction::NudgeSelection : ShortcutAction::None;
            default:
                return ShortcutAction::None;
        }
    }

    std::optional<ToolKind> toolShortcutFor(const int key, const Qt::KeyboardModifiers modifiers)
    {
        // 裸字母才切工具：Ctrl+Z、Ctrl+Shift+Z、Ctrl+S 这些组合里也含字母，认了就把撤销当成换工具
        if (modifiers != Qt::NoModifier)
        {
            return std::nullopt;
        }

        const auto letter = QChar(static_cast<ushort>(key)).toUpper();
        for (const auto &entry: kToolTable)
        {
            // 键位就取登记表的第四列：界面上提示的那枚字母与这里认的是同一个字符，两处不会各说各话
            const auto shortcut = std::get<3>(entry);
            if (!shortcut.empty() && QChar(static_cast<ushort>(shortcut.front())) == letter)
            {
                return std::get<0>(entry);
            }
        }
        return std::nullopt;
    }

    AnnotationWindow::AnnotationWindow(IAnnotationEngine &engine, std::unique_ptr<AnnotationToolbar> toolbar, QWidget *parent) :
        QWidget(parent), m_engine(engine), m_toolbar(std::move(toolbar))
    {
        // 无边框 + 置顶：标注的是刚刚截下的那一块，界面不该被别的窗口盖住，也不该带一条系统标题栏
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setFocusPolicy(Qt::StrongFocus);

        auto *layout = new QVBoxLayout{this};
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        m_view = new AnnotationView{engine, this};
        layout->addWidget(m_view);

        // 工具栏是自己的一层浮窗：它要摆在窗口外面，所以不能进上面的布局
        m_toolbar->setParent(this, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

        // 起点参数整套照工具栏的初值抄一遍：十处必须与工具栏同解，否则开窗第一笔的样式与按钮显示的不一致
        m_penParameters = PenParameters{.color           = m_toolbar->currentColor(),
                                        .lineWidth       = m_toolbar->currentWidth(),
                                        .fontSize        = m_toolbar->currentFontSize(),
                                        .fillStyle       = m_toolbar->currentFillStyle(),
                                        .doubleHeaded    = m_toolbar->currentDoubleHeaded(),
                                        .mosaicBlockSize = m_toolbar->currentMosaicBlockSize(),
                                        .highlightAlpha  = m_toolbar->currentHighlightAlpha(),
                                        .fontFamily      = m_toolbar->currentFontFamily(),
                                        .textBold        = m_toolbar->currentTextBold(),
                                        .textItalic      = m_toolbar->currentTextItalic()};

        QObject::connect(m_toolbar.get(), &AnnotationToolbar::toolChanged, this,
                         [this](ToolKind kind)
                         {
                             if (auto switched = m_engine.setActiveTool(kind); !switched)
                             {
                                 Core::Logger::warning(std::format("切工具没成：{}", switched.error().message));
                             }
                         });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::colorChanged, this,
                         [this](const QColor &color)
                         {
                             m_penParameters.color = color;
                             applyPenParameters();
                         });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::widthChanged, this,
                         [this](int width)
                         {
                             m_penParameters.lineWidth = width;
                             applyPenParameters();
                         });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::fontSizeChanged, this,
                         [this](int size)
                         {
                             m_penParameters.fontSize = size;
                             applyPenParameters();
                         });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::fillStyleChanged, this,
                         [this](int style)
                         {
                             m_penParameters.fillStyle = style;
                             applyPenParameters();
                         });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::doubleHeadedChanged, this,
                         [this](bool enabled)
                         {
                             m_penParameters.doubleHeaded = enabled;
                             applyPenParameters();
                         });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::mosaicBlockSizeChanged, this,
                         [this](int blockSize)
                         {
                             m_penParameters.mosaicBlockSize = blockSize;
                             applyPenParameters();
                         });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::highlightAlphaChanged, this,
                         [this](int alpha)
                         {
                             m_penParameters.highlightAlpha = alpha;
                             applyPenParameters();
                         });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::fontFamilyChanged, this,
                         [this](const QString &family)
                         {
                             m_penParameters.fontFamily = family;
                             applyPenParameters();
                         });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::textBoldChanged, this,
                         [this](bool enabled)
                         {
                             m_penParameters.textBold = enabled;
                             applyPenParameters();
                         });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::textItalicChanged, this,
                         [this](bool enabled)
                         {
                             m_penParameters.textItalic = enabled;
                             applyPenParameters();
                         });

        QObject::connect(m_toolbar.get(), &AnnotationToolbar::undoClicked, this,
                         [this]
                         {
                             if (auto undone = m_engine.undo(); !undone)
                             {
                                 Core::Logger::info(std::format("撤销没做成：{}", undone.error().message));
                             }
                         });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::redoClicked, this,
                         [this]
                         {
                             if (auto redone = m_engine.redo(); !redone)
                             {
                                 Core::Logger::info(std::format("重做没做成：{}", redone.error().message));
                             }
                         });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::copyClicked, this, [this] { deliver(ShortcutAction::Copy); });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::saveClicked, this, [this] { deliver(ShortcutAction::Save); });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::pinClicked, this, [this] { deliver(ShortcutAction::Pin); });
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::ocrClicked, this, [this] { deliver(ShortcutAction::Ocr); });

        // 引擎的栈深变了就刷按钮：让用户看见「还能不能撤」而不是点了才知道没得撤
        QObject::connect(&engine, &IAnnotationEngine::canvasChanged, this,
                         [this]
                         {
                             m_toolbar->setUndoRedoAvailable(m_engine.canUndo(), m_engine.canRedo());
                             // 工具与引擎同步：快捷键也能切工具，切完要把按钮态回灌，别让按钮亮着错的
                             m_toolbar->setActiveTool(m_engine.activeTool());
                         });

        applyPenParameters();
    }

    void AnnotationWindow::startAnnotation(const Core::CapturedImage &captured, const QRect &screenAvailable, const QRect &selection)
    {
        m_engine.loadCanvas(captured);
        m_toolbar->setUndoRedoAvailable(m_engine.canUndo(), m_engine.canRedo());
        m_toolbar->setActiveTool(m_engine.activeTool());

        // 窗口正好盖住截到的那一块：用户看到的画面与之后的成品逐像素对齐
        resize(captured.logicalSize);
        move(selection.topLeft());
        show();
        raise();
        activateWindow();

        m_toolbar->adjustSize();
        if (const auto placed = placeToolbar(screenAvailable, m_toolbar->sizeHint(), QRect{selection.topLeft(), captured.logicalSize}); placed)
        {
            m_toolbar->setGeometry(placed->frame);
        } else
        {
            // 摆不出来的情况只在入参退化时出现，这里给一个不至于让工具栏消失的兜底位置
            Core::Logger::warning(std::format("工具栏摆放失败，改用窗口下沿兜底：{}", placed.error().message));
            m_toolbar->move(selection.left(), selection.bottom() + 1);
        }
        m_toolbar->show();
        m_toolbar->raise();
    }

    AnnotationToolbar &AnnotationWindow::toolbar() const noexcept
    {
        return *m_toolbar;
    }

    void AnnotationWindow::keyPressEvent(QKeyEvent *event)
    {
        // 序号工具在用的时候：`+`/`-` 改下一个号、`0` 回到自动编号。改号本身不动画布，
        // 点一下就能看见新号落在牌子上；别的工具在用时不认，把键交回 Qt
        if (m_engine.activeTool() == ToolKind::Number && event->modifiers() == Qt::NoModifier)
        {
            switch (event->key())
            {
                case Qt::Key_Plus:
                case Qt::Key_Equal:
                    if (const auto changed = m_engine.setNextNumber(m_engine.nextNumber() + 1); !changed)
                    {
                        Core::Logger::warning(std::format("改下一个序号没成：{}", changed.error().message));
                    }
                    return;
                case Qt::Key_Minus:
                    if (const auto changed = m_engine.setNextNumber(m_engine.nextNumber() - 1); !changed)
                    {
                        Core::Logger::warning(std::format("改下一个序号没成：{}", changed.error().message));
                    }
                    return;
                case Qt::Key_0:
                    m_engine.resetNumbering();
                    return;
                default:
                    break;
            }
        }


        const auto action = shortcutActionFor(event->key(), event->modifiers());
        if (action == ShortcutAction::None)
        {
            // 裸字母是换工具：引擎才是真源，切成了要回灌按钮态，否则按钮亮着一个、画的却是另一个
            if (const auto tool = toolShortcutFor(event->key(), event->modifiers()); tool)
            {
                if (auto switched = m_engine.setActiveTool(*tool); switched)
                {
                    m_toolbar->setActiveTool(*tool);
                } else
                {
                    Core::Logger::info(std::format("工具没换成：{}", switched.error().message));
                }
                return;
            }

            // 行内文字框在输入时自己吃掉按键，走到这里的都是界面级快捷键
            QWidget::keyPressEvent(event);
            return;
        }

        switch (action)
        {
            case ShortcutAction::Close:
                m_toolbar->hide();
                hide();
                return;
            case ShortcutAction::Undo:
                if (auto undone = m_engine.undo(); !undone)
                {
                    Core::Logger::info(std::format("撤销没做成：{}", undone.error().message));
                }
                return;
            case ShortcutAction::Redo:
                if (auto redone = m_engine.redo(); !redone)
                {
                    Core::Logger::info(std::format("重做没做成：{}", redone.error().message));
                }
                return;
            case ShortcutAction::NudgeSelection:
            {
                const auto step = GyanisScribe::Ui::arrowStepFor(event->key());
                if (!step)
                {
                    // 分派表只从方向键走到这一支；走到别处说明两张表不同解，宁可什么都不做也不瞎挪
                    return;
                }
                const auto stride = event->modifiers().testFlag(Qt::ShiftModifier) ? kCoarseNudgeStride : 1;
                if (auto moved = m_engine.moveSelectionBy(*step * stride); !moved)
                {
                    Core::Logger::info(std::format("微调没做成：{}", moved.error().message));
                }
                return;
            }
            case ShortcutAction::DeleteSelection:
                if (auto removed = m_engine.removeSelection(); !removed)
                {
                    Core::Logger::info(std::format("删除没做成：{}", removed.error().message));
                }
                return;
            default:
                deliver(action);
                return;
        }
    }

    void AnnotationWindow::deliver(const ShortcutAction action)
    {
        // 收界面在导出之前：不然浮窗与选框会被下一轮看见，也会被误当成画面的一部分
        m_toolbar->hide();
        hide();

        const auto image = m_engine.exportImage();
        switch (action)
        {
            case ShortcutAction::Copy:
                emit copyRequested(image);
                break;
            case ShortcutAction::Save:
                emit saveRequested(image);
                break;
            case ShortcutAction::Pin:
                emit pinRequested(image);
                break;
            case ShortcutAction::Ocr:
                emit ocrRequested(image);
                break;
            default:
                return;
        }
    }

    void AnnotationWindow::applyPenParameters()
    {
        if (auto applied = m_engine.setPenParameters(m_penParameters); !applied)
        {
            Core::Logger::warning(std::format("画笔参数没下发：{}", applied.error().message));
        }
    }

} // namespace GyanisScribe::Annotation
