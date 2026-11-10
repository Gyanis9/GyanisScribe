#include "Gui/Annotation/AnnotationWindow.h"

#include "Core/Annotation/AnnotationEngine.h"
#include "Core/Annotation/IAnnotationEngine.h"
#include "Core/Capture/ScreenUtils.h"
#include "Core/Settings/SettingsKeys.h"
#include "Gui/Annotation/AnnotationToolbarGeometry.h"
#include "Gui/Annotation/CaptureInfoPanel.h"
#include "Base/Logger.h"
#include "Core/Settings/ISettingsStore.h"
#include "Gui/Controls/AppShortcuts.h"
#include "Gui/Controls/KeyStep.h"
#include "Base/Types.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMoveEvent>
#include <QScreen>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace
{
    /// 按住 Shift 时一次挪多少格：一格太慢、十格在 4K 屏上仍是「一下就跳过目标」，取中间值
    constexpr int kCoarseNudgeStride = 10;

    /// 信息面板与工具栏之间的留白，逻辑像素：贴着边摆会让两块浮层的描边叠在一起
    constexpr int kInfoPanelGap = 8;

    /// 工具栏与信息面板都放不下时，面板从屏幕可用区左上角内缩这么多，逻辑像素
    constexpr int kInfoPanelInset = 12;

    /**
     * @brief 这一轮来源是不是一项都没拿到
     * @param source 本轮来源
     * @return 应用、窗口标题、屏幕都是空白且捕获时刻无效时为真
     * @details 空白画布与回溯换帧传的是这份空来源；来源全空时不摆只写「未知」四项的面板
     */
    bool isSourceInfoEmpty(const GyanisScribe::Core::CaptureSourceInfo &source)
    {
        return source.processName.trimmed().isEmpty() && source.windowTitle.trimmed().isEmpty() && source.screenName.trimmed().isEmpty() && !source.capturedAt.isValid();
    }
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

        // 可见段收放让工具栏尺寸变了就重摆一次：收窄后还钉在旧位置会偏出选区中心；
        // 信息面板挂在工具栏旁边，工具栏一挪它也得跟着重摆
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::contextLayoutChanged, this,
                         [this]
                         {
                             placeToolbarForSelection();
                             placeInfoPanel();
                         });
        // X 与 Esc 同一条路：用户不必记住「要关只能按键盘」
        QObject::connect(m_toolbar.get(), &AnnotationToolbar::closeClicked, this, &AnnotationWindow::closeAnnotation);
        QObject::connect(m_view, &AnnotationView::captureResized, this, [this](const QRect &globalGeometry) { emit cropRequested(m_refreshScreenName, globalGeometry); });
        QObject::connect(m_view, &AnnotationView::captureResizeMoved, this, [this](const QRect &globalGeometry) { updateCropPreview(globalGeometry); });

        // 四个动作按钮与快捷键汇到同一个出口：谁按的都走 deliver，界面不需要知道来源
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::copyRequested, this, [this] { deliver(ShortcutAction::Copy); });
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::saveRequested, this, [this] { deliver(ShortcutAction::Save); });
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::pinRequested, this, [this] { deliver(ShortcutAction::Pin); });
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::ocrRequested, this, [this] { deliver(ShortcutAction::Ocr); });
        // 美化分享不关窗：对话框拿一份成品副本，关了还能接着标注
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::shareRequested, this, [this] { emit shareRequested(m_engine.exportImage()); });
        // 扫码不关窗：与打印同一条口径，识别结果由宿主提示，编辑现场留着
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::scanRequested, this, [this] { emit scanRequested(m_engine.exportImage()); });
        // 隐私打码也不关窗：检查要等识别回来，结果由宿主审核后才落到画布上。
        // 交出去的是画布原图：检测框与它同坐标系，烘上图形的成品会让文字与框对不上
        QObject::connect(m_toolbarBridge.get(), &AnnotationToolbarBridge::privacyRequested, this, [this] { emit privacyRequested(m_engine.canvas().image); });
    }

    AnnotationWindow::~AnnotationWindow() = default;

    void AnnotationWindow::startAnnotation(const Base::CapturedImage &captured, const QRect &screenAvailable, const QRect &selection, const Core::CaptureSourceInfo &source)
    {
        // 新一轮开窗重新计收场：上一轮报过的 closed 不该压住这一轮
        m_closedReported = false;
        // 面板也重新评估：上一轮用户亲手收起的那一块不该继续压着这一轮
        m_infoPanelClosedByUser = false;
        // 轮次守卫跟着开窗重新立起来：上一轮关窗后留下的画布还在引擎里，不能拿它当「还在编辑」
        m_roundActive = true;
        // 来源随这一轮留在窗口上：空白画布与回溯换帧那几轮传的是空来源，不继承上一轮的
        m_sourceInfo = source;
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
        // 面板按这一轮的开关与来源决定去留：上一轮用户关掉的那块随新一轮重新评估
        updateInfoPanel();
    }

    void AnnotationWindow::closeAnnotation()
    {
        // 工具栏与信息面板都是独立顶层窗：藏这一边不会把它们一起带走
        m_toolbar->hide();
        hideInfoPanel();
        hide();
        // 收完再报：接收方查可见态时看到的就是「已经收起」
        reportClosedOnce();
    }

    void AnnotationWindow::closeEvent(QCloseEvent *event)
    {
        // 系统关闭（Alt+F4、任务栏）与 Esc/X 走同一条收场路：工具栏是独立顶层窗，必须先收；
        // 轮次守卫也要跟着落地，否则迟到的隐私结果会往一扇看不见的画布上落笔
        closeAnnotation();
        QWidget::closeEvent(event);
        event->accept();
    }

    void AnnotationWindow::moveEvent(QMoveEvent *event)
    {
        QWidget::moveEvent(event);
        // 面板是独立顶层窗，不会跟着本窗口自动平移；窗口一挪就按工具栏的新位置重摆
        placeInfoPanel();
    }

    void AnnotationWindow::reportClosedOnce()
    {
        // 交付出口也会收窗：与 Esc/X 共用这一声，一轮只报一次，免得宿主重复恢复抓帧
        if (m_closedReported)
        {
            return;
        }
        m_closedReported = true;
        // 收场之后这一轮不再可编辑：迟到的异步结果靠它认出窗已经关了
        m_roundActive = false;
        // 面板是独立顶层窗，收场时一并藏起：交付、Esc/X 与系统关窗都汇到这一处
        hideInfoPanel();
        emit closed();
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

    void AnnotationWindow::updateInfoPanel()
    {
        // 开关默认关；来源全空的轮次（空白画布、回溯换帧）也不摆一块只写「未知」的面板；
        // 用户在这一轮亲手收起过，就不再替用户打开
        if (!m_settings.boolValue(Core::kCaptureShowInfoPanel, false) || isSourceInfoEmpty(m_sourceInfo) || m_infoPanelClosedByUser)
        {
            hideInfoPanel();
            return;
        }

        if (!m_infoPanel)
        {
            // 不带父控件：面板要摆在标注窗外面，进了子控件树就会被裁切，也说不清有没有被导出
            m_infoPanel = std::make_unique<CaptureInfoPanel>();
            // 面板自己收起时由窗口再收一次可见态，并记下这一轮用户不要它了：
            // 可见性只归标注窗管，面板只报一声
            QObject::connect(m_infoPanel.get(), &CaptureInfoPanel::closed, this, &AnnotationWindow::handleInfoPanelClosed);
        }
        m_infoPanel->setSource(m_sourceInfo);
        m_infoPanel->adjustSize();
        placeInfoPanel();
        // 窗口还没露出来时不先亮面板：两块浮层一先一后上屏会闪一下
        if (!isVisible())
        {
            m_infoPanel->hide();
            return;
        }
        m_infoPanel->show();
        m_infoPanel->raise();
    }

    void AnnotationWindow::placeInfoPanel()
    {
        if (!m_infoPanel)
        {
            return;
        }

        const auto size      = m_infoPanel->sizeHint().isEmpty() ? m_infoPanel->preferredSize() : m_infoPanel->sizeHint();
        auto       available = m_placedScreenAvailable;
        if (!available.isValid() || available.isEmpty())
        {
            // 摆放依据缺失时退回「窗口当前在哪块屏」；离屏用例没有真实屏幕，这一路可能仍是空
            if (const auto *screen = QGuiApplication::screenAt(mapToGlobal(rect().center())))
            {
                available = screen->availableGeometry();
            } else if (const auto *primary = QGuiApplication::primaryScreen())
            {
                available = primary->availableGeometry();
            }
        }
        if (available.isEmpty())
        {
            m_infoPanel->resize(size);
            return;
        }

        // 先试工具栏右侧，再试左侧：两块浮层同一行排开，视线不用上下跳
        const QRect toolbarFrame = m_toolbar->geometry();
        QRect       target;
        if (!toolbarFrame.isEmpty())
        {
            const QRect onRight{QPoint{toolbarFrame.right() + 1 + kInfoPanelGap, toolbarFrame.top()}, size};
            const QRect onLeft{QPoint{toolbarFrame.left() - kInfoPanelGap - size.width(), toolbarFrame.top()}, size};
            if (available.contains(onRight))
            {
                target = onRight;
            } else if (available.contains(onLeft))
            {
                target = onLeft;
            }
        }
        if (target.isEmpty())
        {
            // 两侧都放不下：退回屏幕可用区左上角，内缩一格免得贴着任务栏与屏边
            target = QRect{available.topLeft() + QPoint{kInfoPanelInset, kInfoPanelInset}, size};
        }
        // 兜底位也可能比可用区还大：把左上角夹回可用区，至少不整块出屏
        const auto maxX = std::max(available.left(), available.right() - size.width() + 1);
        const auto maxY = std::max(available.top(), available.bottom() - size.height() + 1);
        target.moveLeft(std::clamp(target.left(), available.left(), maxX));
        target.moveTop(std::clamp(target.top(), available.top(), maxY));

        m_infoPanel->resize(size);
        m_infoPanel->move(target.topLeft());
        if (m_infoPanel->isVisible())
        {
            // 窗口与工具栏随后可能被抬起；面板要留在它们上面才看得见
            m_infoPanel->raise();
        }
    }

    void AnnotationWindow::hideInfoPanel() noexcept
    {
        if (m_infoPanel)
        {
            m_infoPanel->hide();
        }
    }

    void AnnotationWindow::handleInfoPanelClosed()
    {
        // 用户亲手收起只作用于这一轮：之后抓屏前后的隐藏与恢复不许擅自把它带回来
        m_infoPanelClosedByUser = true;
        hideInfoPanel();
    }

    void AnnotationWindow::hideOverlaysForCapture()
    {
        // 抓屏看到的是桌面：标注窗、工具栏与信息面板三块都盖在选区上，要收就一起收；
        // 先记下窗口与工具栏是否露着，抓完只把原来有的还回去
        m_captureWindowWasVisible  = isVisible();
        m_captureToolbarWasVisible = m_toolbar->isVisible();
        m_toolbar->hide();
        hideInfoPanel();
        hide();
    }

    void AnnotationWindow::restoreOverlaysAfterCapture()
    {
        if (m_captureWindowWasVisible)
        {
            show();
            raise();
            // 连按 F5 时下一颗键要回到这扇窗：不激活的话焦点留在别处
            activateWindow();
        }
        if (m_captureToolbarWasVisible)
        {
            m_toolbar->show();
            m_toolbar->raise();
        }
        if (m_captureWindowWasVisible)
        {
            // 面板按本轮策略重算：开关关、来源空或用户亲手收起过都不重新露出来；
            // 窗口没恢复时 updateInfoPanel 也会把面板留在隐藏态
            updateInfoPanel();
        }
    }

    AnnotationToolbar &AnnotationWindow::toolbar() const noexcept
    {
        return *m_toolbar;
    }

    const Base::CapturedImage &AnnotationWindow::canvas() const noexcept
    {
        return m_engine.canvas();
    }

    bool AnnotationWindow::isRoundActive() const noexcept
    {
        return m_roundActive;
    }

    const Core::CaptureSourceInfo &AnnotationWindow::captureSourceForTesting() const noexcept
    {
        return m_sourceInfo;
    }

    bool AnnotationWindow::isInfoPanelVisible() const noexcept
    {
        return m_infoPanel != nullptr && m_infoPanel->isVisible();
    }

    QString AnnotationWindow::infoPanelText() const
    {
        if (!m_infoPanel)
        {
            return {};
        }
        const QStringList valueNames{
                QStringLiteral("captureInfoApp"),
                QStringLiteral("captureInfoTitle"),
                QStringLiteral("captureInfoMonitor"),
                QStringLiteral("captureInfoTime"),
        };
        QStringList lines;
        for (const auto &name: valueNames)
        {
            if (const auto *label = m_infoPanel->findChild<QLabel *>(name))
            {
                lines.append(label->text());
            }
        }
        return lines.join(QLatin1Char('\n'));
    }

    QWidget *AnnotationWindow::infoPanelForTesting() const noexcept
    {
        return m_infoPanel.get();
    }

    void AnnotationWindow::refreshCapture(const Base::CapturedImage &captured)
    {
        // 刷新的区域就是原来那块：尺寸该同形；真差了一点也不动窗口——用户的标注坐标还是老坐标系，
        // 挪窗口只会让画面与标注错开。引擎只换底图这一份，标注与撤销栈原地留着
        // 刷新这一轮的记录一并换掉：之后拖边框重裁、拖动时那块预览都从新帧取，不能再回看旧帧
        m_refreshScreenName   = captured.screenName;
        m_refreshRegion       = captured.logicalRegion;
        m_frozenImage         = captured.frozenImage;
        m_frozenLogicalSize   = captured.frozenLogicalSize;
        m_frozenLogicalRegion = captured.frozenLogicalRegion;
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
        // 重裁换了窗口与工具栏的位置：抓屏前收起的面板在这里按本轮策略恢复并重摆，
        // 用户关掉的那一块由本轮记录挡住，不会被重新打开
        updateInfoPanel();
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
        // 窗口摆回旧几何之后面板也要按本轮策略恢复并回到工具栏旁边
        updateInfoPanel();
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
        // 预览图被裁过之后可能盖不满新窗口：把裁切片的逻辑原点与逻辑尺寸一起交给视图，
        // 图形才会跟着预览图同一份比例缩放，而不是按老坐标平移
        const QPointF clippedOrigin = QPointF{clipped.topLeft()} / dpr + QPointF{static_cast<qreal>(m_frozenLogicalRegion.left()), static_cast<qreal>(m_frozenLogicalRegion.top())};
        const QPointF shapeOrigin   = QPointF{oldLocal.topLeft()} - clippedOrigin;
        const QSizeF  previewLogicalSize = QSizeF{clipped.size()} / dpr;
        m_view->setCropPreview(preview, shapeOrigin, previewLogicalSize);
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
        hideInfoPanel();
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
                break;
        }
        // 交付完再报收场：复制、保存、贴图、识别与 Esc/X 是同一个终点，宿主据此恢复抓帧
        reportClosedOnce();
    }

} // namespace GyanisScribe::Gui
