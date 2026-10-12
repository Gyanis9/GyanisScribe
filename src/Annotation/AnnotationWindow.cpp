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
            case Qt::Key_P:
                // Ctrl+P 打印；裸 P 是画笔工具，带 Ctrl 才分得清（工具键只认裸字母）
                return control ? ShortcutAction::Print : ShortcutAction::None;
            case Qt::Key_C:
                // Ctrl+C 复制选中的图形；一条都没选中时窗口按老习惯把它当「复制整张成品」
                return control && !shift ? ShortcutAction::CopyShape : ShortcutAction::None;
            case Qt::Key_V:
                return control && !shift ? ShortcutAction::PasteShape : ShortcutAction::None;
            case Qt::Key_Delete:
            case Qt::Key_Backspace:
                return modifiers == Qt::NoModifier ? ShortcutAction::DeleteSelection : ShortcutAction::None;
            case Qt::Key_Left:
            case Qt::Key_Right:
                // 带 Ctrl 就不认：行内的文字框是 QTextEdit，Ctrl+方向键在它那儿是按词移光标，截走就少了这条既有行为
                return control ? ShortcutAction::None : ShortcutAction::NudgeSelection;
            case Qt::Key_Up:
            case Qt::Key_Down:
                if (control)
                {
                    // Ctrl+Shift+↑/↓ 改层序；Ctrl+↑/↓ 那几组合留给行内文字框按词移光标
                    if (!shift)
                    {
                        return ShortcutAction::None;
                    }
                    return key == Qt::Key_Up ? ShortcutAction::RaiseSelection : ShortcutAction::LowerSelection;
                }
                return ShortcutAction::NudgeSelection;
            case Qt::Key_Home:
                // Ctrl+Shift+Home 置底：Home 指向这摞图形的开头，也就是压在底下那一层
                return control && shift ? ShortcutAction::SendToBack : ShortcutAction::None;
            case Qt::Key_End:
                // Ctrl+Shift+End 置顶：End 指向末尾，也就是压在最上面那一层
                return control && shift ? ShortcutAction::BringToFront : ShortcutAction::None;
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

        // 工具栏与引擎的接线全在桥里：十三项样式参数、工具切换、撤销重做与按钮态回灌一处接齐。
        // 贴图上直接标注那一处共用同一个桥，将来加新参数不会出现「这边接了那边漏了」
        m_toolbarBridge = std::make_unique<AnnotationToolbarBridge>(*m_toolbar, engine, this);

        // 四个动作按钮与快捷键汇到同一个出口：谁按的都走 deliver，界面不需要知道来源
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::copyRequested, this, [this] { deliver(ShortcutAction::Copy); });
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::saveRequested, this, [this] { deliver(ShortcutAction::Save); });
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::pinRequested, this, [this] { deliver(ShortcutAction::Pin); });
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::ocrRequested, this, [this] { deliver(ShortcutAction::Ocr); });
    }

    void AnnotationWindow::startAnnotation(const Core::CapturedImage &captured, const QRect &screenAvailable, const QRect &selection)
    {
        // 载入会发 canvasChanged，桥在那里把按钮态与当前工具一起回灌，这里不用再同步一次
        m_engine.loadCanvas(captured);

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
            case ShortcutAction::CopyShape:
                // 一条都没选中时按老习惯走：Ctrl+C 复制整张成品，与回车同一条路
                if (m_engine.selections().empty())
                {
                    deliver(ShortcutAction::Copy);
                    return;
                }
                if (auto copied = m_engine.copySelection(); !copied)
                {
                    Core::Logger::info(std::format("复制没做成：{}", copied.error().message));
                }
                return;
            case ShortcutAction::PasteShape:
                if (auto pasted = m_engine.pasteClipboard(); !pasted)
                {
                    Core::Logger::info(std::format("粘贴没做成：{}", pasted.error().message));
                }
                return;
            case ShortcutAction::RaiseSelection:
                if (auto raised = m_engine.raiseSelection(); !raised)
                {
                    Core::Logger::info(std::format("上移一层没做成：{}", raised.error().message));
                }
                return;
            case ShortcutAction::LowerSelection:
                if (auto lowered = m_engine.lowerSelection(); !lowered)
                {
                    Core::Logger::info(std::format("下移一层没做成：{}", lowered.error().message));
                }
                return;
            case ShortcutAction::BringToFront:
                if (auto fronted = m_engine.bringSelectionToFront(); !fronted)
                {
                    Core::Logger::info(std::format("置顶没做成：{}", fronted.error().message));
                }
                return;
            case ShortcutAction::SendToBack:
                if (auto backed = m_engine.sendSelectionToBack(); !backed)
                {
                    Core::Logger::info(std::format("置底没做成：{}", backed.error().message));
                }
                return;
            case ShortcutAction::Print:
                // 打印不关窗：对话框取消、或是打完还想接着改，编辑现场都留着。
                // 导出与其它出口同一条路——烘的只有已经落定的图形，界面本身不进来
                emit printRequested(m_engine.exportImage());
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

} // namespace GyanisScribe::Annotation
