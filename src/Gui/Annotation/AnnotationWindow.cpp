#include "Gui/Annotation/AnnotationWindow.h"

#include "Core/Annotation/AnnotationEngine.h"
#include "Core/Annotation/IAnnotationEngine.h"
#include "Core/Capture/ScreenUtils.h"
#include "Gui/Annotation/AnnotationToolbarGeometry.h"
#include "Base/Logger.h"
#include "Core/Settings/ISettingsStore.h"
#include "Gui/Controls/AppShortcuts.h"
#include "Gui/Controls/KeyStep.h"
#include "Base/Types.h"

#include <QKeyEvent>
#include <QVBoxLayout>

#include <cmath>
#include <format>
#include <utility>

namespace
{
    /// 按住 Shift 时一次挪多少格：一格太慢、十格在 4K 屏上仍是「一下就跳过目标」，取中间值
    constexpr int kCoarseNudgeStride = 10;
} // namespace

namespace GyanisScribe::Gui
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
            case Qt::Key_F5:
                // 刷新截图：重抓这块区域换底图，标注留着。按住不放由键盘自动重复兜住——
                // 每一次重复按键就是一次重抓，刷到画面追上为止
                return modifiers == Qt::NoModifier ? ShortcutAction::Refresh : ShortcutAction::None;
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

    std::optional<Core::ToolKind> toolShortcutFor(const int key, const Qt::KeyboardModifiers modifiers, const Core::ISettingsStore &settings)
    {
        // 整颗组合相等才算命中：默认那十三颗是裸字母，于是 Ctrl+Z 这类组合不会顺手把撤销当成换工具；
        // 用户把某一档换成带修饰键的组合后，那颗组合也就归这一档了
        for (const auto &entry: kToolTable)
        {
            const auto *shortcut = GyanisScribe::Gui::appShortcutEntry(std::get<3>(entry));
            if (shortcut != nullptr && GyanisScribe::Gui::isAppShortcutHit(settings, *shortcut, key, modifiers))
            {
                return std::get<0>(entry);
            }
        }
        return std::nullopt;
    }

    AnnotationWindow::AnnotationWindow(Core::IAnnotationEngine &engine, Core::ISettingsStore &settings, std::unique_ptr<AnnotationToolbar> toolbar, QWidget *parent) :
        QWidget(parent), m_engine(engine), m_settings(settings), m_toolbar(std::move(toolbar))
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

        // 工具栏与引擎的接线全在桥里：十五项样式参数、工具切换、撤销重做与按钮态回灌一处接齐。
        // 贴图上直接标注那一处共用同一个桥，将来加新参数不会出现「这边接了那边漏了」
        m_toolbarBridge = std::make_unique<AnnotationToolbarBridge>(*m_toolbar, engine, this);

        // 可见段收放让工具栏尺寸变了就重摆一次：收窄后还钉在旧位置会偏出选区中心
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::contextLayoutChanged, this, [this] { placeToolbarForSelection(); });
        // X 与 Esc 同一条路：用户不必记住「要关只能按键盘」
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::closeClicked, this, &AnnotationWindow::closeAnnotation);
        QObject::connect(m_view, &AnnotationView::captureResized, this, [this](const QRect &globalGeometry) { emit cropRequested(m_refreshScreenName, globalGeometry); });
        QObject::connect(m_view, &AnnotationView::captureResizeMoved, this, [this](const QRect &globalGeometry) { updateCropPreview(globalGeometry); });

        // 四个动作按钮与快捷键汇到同一个出口：谁按的都走 deliver，界面不需要知道来源
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::copyRequested, this, [this] { deliver(ShortcutAction::Copy); });
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::saveRequested, this, [this] { deliver(ShortcutAction::Save); });
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::pinRequested, this, [this] { deliver(ShortcutAction::Pin); });
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::ocrRequested, this, [this] { deliver(ShortcutAction::Ocr); });
        // 扫码不关窗：与打印同一条口径，识别结果由宿主提示，编辑现场留着
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::scanRequested, this, [this] { emit scanRequested(m_engine.exportImage()); });
    }

    void AnnotationWindow::startAnnotation(const Base::CapturedImage &captured, const QRect &screenAvailable, const QRect &selection)
    {
        // 记下这一轮抓的是哪块屏、哪块区域：刷新截图（F5）要对着同一块地方重抓
        m_refreshScreenName   = captured.screenName;
        m_refreshRegion       = captured.logicalRegion;
        m_frozenImage         = captured.frozenImage;
        m_frozenLogicalSize   = captured.frozenLogicalSize;
        m_frozenLogicalRegion = captured.frozenLogicalRegion;
        // 摆放依据也记下来：切工具会收放可见段、尺寸跟着变，重摆必须用同一份选区与可用区
        m_placedScreenAvailable = screenAvailable;
        m_placedSelection       = QRect{selection.topLeft(), captured.logicalSize};

        // 载入会发 canvasChanged，桥在那里把按钮态与当前工具一起回灌，这里不用再同步一次
        m_engine.loadCanvas(captured);

        // 窗口正好盖住截到的那一块：用户看到的画面与之后的成品逐像素对齐
        resize(captured.logicalSize);
        move(selection.topLeft());
        show();
        raise();
        activateWindow();

        m_toolbar->adjustSize();
        placeToolbarForSelection();
        m_toolbar->show();
        m_toolbar->raise();
    }

    void AnnotationWindow::closeAnnotation()
    {
        // 工具栏是独立顶层窗：藏这一边不会把它一起带走
        m_toolbar->hide();
        hide();
    }

    void AnnotationWindow::placeToolbarForSelection()
    {
        // 还没摆过第一轮（构造期或上一轮遗留的回灌）不动：此刻选区与可用区还是空矩形
        if (!m_placedSelection.isValid() || !m_placedScreenAvailable.isValid())
        {
            return;
        }
        const auto placed = placeToolbar(m_placedScreenAvailable, m_toolbar->sizeHint(), m_placedSelection);
        if (placed)
        {
            m_toolbar->setGeometry(placed->frame);
            return;
        }
        // 摆不出来的情况只在入参退化时出现，这里给一个不至于让工具栏消失的兜底位置
        Base::Logger::warning(std::format("工具栏摆放失败，改用窗口下沿兜底：{}", placed.error().message));
        m_toolbar->move(m_placedSelection.left(), m_placedSelection.bottom() + 1);
    }

    AnnotationToolbar &AnnotationWindow::toolbar() const noexcept
    {
        return *m_toolbar;
    }

    void AnnotationWindow::refreshCapture(const Base::CapturedImage &captured)
    {
        // 刷新的区域就是原来那块：尺寸该同形；真差了一点也不动窗口——用户的标注坐标还是老坐标系，
        // 挪窗口只会让画面与标注错开。引擎只换底图这一份，标注与撤销栈原地留着
        m_engine.refreshCanvas(captured);
    }

    void AnnotationWindow::recropAnnotation(const Base::CapturedImage &captured, const QRect &screenAvailable, const QRect &selection)
    {
        // 旧画布左上在新画布里的位置：图形按这个位移搬过去，屏幕上看起来不动
        const QPoint offset{m_placedSelection.topLeft() - selection.topLeft()};
        m_placedScreenAvailable = screenAvailable;
        m_placedSelection       = QRect{selection.topLeft(), captured.logicalSize};
        m_refreshScreenName     = captured.screenName;
        m_refreshRegion         = captured.logicalRegion;
        m_frozenImage           = captured.frozenImage;
        m_frozenLogicalSize     = captured.frozenLogicalSize;
        m_frozenLogicalRegion   = captured.frozenLogicalRegion;
        m_view->clearCropPreview();

        m_engine.recropCanvas(captured, offset);
        resize(captured.logicalSize);
        move(selection.topLeft());
        show();
        raise();
        activateWindow();

        m_toolbar->adjustSize();
        placeToolbarForSelection();
        m_toolbar->show();
        m_toolbar->raise();
    }

    void AnnotationWindow::restoreAfterCrop(const QRect &selection, const QRect &screenAvailable)
    {
        m_placedScreenAvailable = screenAvailable;
        m_placedSelection       = QRect{selection.topLeft(), m_engine.canvas().logicalSize};
        m_view->clearCropPreview();
        resize(m_engine.canvas().logicalSize);
        move(selection.topLeft());
        show();
        raise();
        activateWindow();
        m_toolbar->adjustSize();
        placeToolbarForSelection();
        m_toolbar->show();
        m_toolbar->raise();
    }

    void AnnotationWindow::updateCropPreview(const QRect &globalSelection)
    {
        if (m_frozenImage.isNull() || m_frozenLogicalSize.isEmpty())
        {
            return;
        }
        auto *screen = GyanisScribe::Core::screenByName(m_refreshScreenName);
        if (screen == nullptr)
        {
            return;
        }

        const QPoint origin = screen->geometry().topLeft();
        const QRect  oldLocal{m_placedSelection.topLeft() - origin, m_placedSelection.size()};
        const QRect  newLocal{globalSelection.topLeft() - origin, globalSelection.size()};
        const qreal  dpr           = static_cast<qreal>(m_frozenImage.width()) / static_cast<qreal>(m_frozenLogicalSize.width());
        const QRect  sourceLogical = newLocal.translated(-m_frozenLogicalRegion.topLeft());
        const QRect  sourcePhysical{QPoint{static_cast<int>(std::lround(sourceLogical.left() * dpr)), static_cast<int>(std::lround(sourceLogical.top() * dpr))},
                                    QSize{static_cast<int>(std::lround(sourceLogical.width() * dpr)), static_cast<int>(std::lround(sourceLogical.height() * dpr))}};
        const QRect  clipped = sourcePhysical.intersected(m_frozenImage.rect());
        if (clipped.isEmpty())
        {
            return;
        }

        QImage preview = m_frozenImage.copy(clipped);
        preview.setDevicePixelRatio(dpr);
        m_view->setCropPreview(preview, oldLocal.topLeft() - newLocal.topLeft());
    }

    void AnnotationWindow::keyPressEvent(QKeyEvent *event)
    {
        // 序号工具在用的时候：`+`/`-` 改下一个号、`0` 回到自动编号。改号本身不动画布，
        // 点一下就能看见新号落在牌子上；别的工具在用时不认，把键交回 Qt
        if (m_engine.activeTool() == Core::ToolKind::Number && event->modifiers() == Qt::NoModifier)
        {
            switch (event->key())
            {
                case Qt::Key_Plus:
                case Qt::Key_Equal:
                    if (const auto changed = m_engine.setNextNumber(m_engine.nextNumber() + 1); !changed)
                    {
                        Base::Logger::warning(std::format("改下一个序号没成：{}", changed.error().message));
                    }
                    return;
                case Qt::Key_Minus:
                    if (const auto changed = m_engine.setNextNumber(m_engine.nextNumber() - 1); !changed)
                    {
                        Base::Logger::warning(std::format("改下一个序号没成：{}", changed.error().message));
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
            // 字母键是换工具：引擎才是真源，切成了要回灌按钮态，否则按钮亮着一个、画的却是另一个
            if (const auto tool = toolShortcutFor(event->key(), event->modifiers(), m_settings); tool)
            {
                if (auto switched = m_engine.setActiveTool(*tool); switched)
                {
                    m_toolbar->setActiveTool(*tool);
                } else
                {
                    Base::Logger::info(std::format("工具没换成：{}", switched.error().message));
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
                closeAnnotation();
                return;
            case ShortcutAction::Undo:
                if (auto undone = m_engine.undo(); !undone)
                {
                    Base::Logger::info(std::format("撤销没做成：{}", undone.error().message));
                }
                return;
            case ShortcutAction::Redo:
                if (auto redone = m_engine.redo(); !redone)
                {
                    Base::Logger::info(std::format("重做没做成：{}", redone.error().message));
                }
                return;
            case ShortcutAction::NudgeSelection:
            {
                const auto step = GyanisScribe::Gui::arrowStepFor(event->key());
                if (!step)
                {
                    // 分派表只从方向键走到这一支；走到别处说明两张表不同解，宁可什么都不做也不瞎挪
                    return;
                }
                const auto stride = event->modifiers().testFlag(Qt::ShiftModifier) ? kCoarseNudgeStride : 1;
                if (auto moved = m_engine.moveSelectionBy(*step * stride); !moved)
                {
                    Base::Logger::info(std::format("微调没做成：{}", moved.error().message));
                }
                return;
            }
            case ShortcutAction::DeleteSelection:
                if (auto removed = m_engine.removeSelection(); !removed)
                {
                    Base::Logger::info(std::format("删除没做成：{}", removed.error().message));
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
                    Base::Logger::info(std::format("复制没做成：{}", copied.error().message));
                }
                return;
            case ShortcutAction::PasteShape:
                if (auto pasted = m_engine.pasteClipboard(); !pasted)
                {
                    Base::Logger::info(std::format("粘贴没做成：{}", pasted.error().message));
                }
                return;
            case ShortcutAction::RaiseSelection:
                if (auto raised = m_engine.raiseSelection(); !raised)
                {
                    Base::Logger::info(std::format("上移一层没做成：{}", raised.error().message));
                }
                return;
            case ShortcutAction::LowerSelection:
                if (auto lowered = m_engine.lowerSelection(); !lowered)
                {
                    Base::Logger::info(std::format("下移一层没做成：{}", lowered.error().message));
                }
                return;
            case ShortcutAction::BringToFront:
                if (auto fronted = m_engine.bringSelectionToFront(); !fronted)
                {
                    Base::Logger::info(std::format("置顶没做成：{}", fronted.error().message));
                }
                return;
            case ShortcutAction::SendToBack:
                if (auto backed = m_engine.sendSelectionToBack(); !backed)
                {
                    Base::Logger::info(std::format("置底没做成：{}", backed.error().message));
                }
                return;
            case ShortcutAction::Refresh:
                // 交给宿主去重抓：抓屏的口子在采集服务那边，窗口自己不碰来源
                emit refreshRequested(m_refreshScreenName, m_refreshRegion);
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

} // namespace GyanisScribe::Gui
