/**
 * @file AnnotationEngine.h
 * @brief 标注引擎实现：工具集合、选择、撤销栈与导出
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/IAnnotationEngine.h"

#include <QRectF>
#include <QUndoStack>

#include <optional>
#include <utility>
#include <vector>

namespace GyanisScribe::Core
{

    /**
     * @brief IAnnotationEngine 的实现
     * @details 工具集在构造时按 ToolKind 建齐，切换只是改「谁接收事件」——
     *          每类图形归各自工具管，撤销栈管的正是「插哪条、取哪条、移哪条」这三件事。
     *          层叠顺序由引擎统一发号：每条图形落定时领到一个全局层叠号（AnnotationShape::zOrder），
     *          绘制与命中都按那个号排序后进行，于是「后画的压在先画的上面」跨工具也成立，
     *          置顶/置底只是换号。
     */
    class AnnotationEngine : public IAnnotationEngine
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造引擎并按 ToolKind 建齐全部工具
         * @param parent QObject 父对象，默认无
         */
        explicit AnnotationEngine(QObject *parent = nullptr);

        /**
         * @brief 析构引擎，撤销栈随之清空
         */
        ~AnnotationEngine() override;

        /**
         * @brief 载入画布，见 IAnnotationEngine::loadCanvas()
         * @param image 抓屏产物
         * @details 重写基类接口：清图形、清撤销栈、清选中——换一帧就是换一张画布。
         */
        void loadCanvas(Base::CapturedImage image) override;

        /**
         * @brief 换底图保标注，见 IAnnotationEngine::refreshCanvas()
         * @param image 重新抓到的那一帧
         * @details 重写基类接口：只换 m_canvas 这一份，图形、选中、层序与撤销历史原地留着。
         */
        void refreshCanvas(Base::CapturedImage image) override;

        /**
         * @brief 当前画布，见 IAnnotationEngine::canvas()
         * @return 载入的那一帧
         */
        [[nodiscard]] const Base::CapturedImage &canvas() const noexcept override;

        /**
         * @brief 导出烘焙好的图，见 IAnnotationEngine::exportImage()
         * @return 画布副本上烘好全部图形；没画布时返回空图
         * @details 重写基类接口：像素倍比由「物理宽 ÷ 逻辑宽」现算，
         *          不拿屏幕的 devicePixelRatio——缩放窗口后两者才是同一条账。
         *          倍比不是 1.0 时把这个倍比标回导出图，让贴图那一步能反推逻辑尺寸。
         */
        [[nodiscard]] QImage exportImage() const override;

        /**
         * @brief 切换当前工具，见 IAnnotationEngine::setActiveTool()
         * @param kind 目标种类
         * @return 成功返回空载荷；种类不在册返回 InvalidArgument
         */
        [[nodiscard]] Base::Expected<void> setActiveTool(ToolKind kind) override;

        /**
         * @brief 当前工具种类，见 IAnnotationEngine::activeTool()
         * @return 生效中的那一个
         */
        [[nodiscard]] ToolKind activeTool() const noexcept override;

        /**
         * @brief 下发画笔参数，见 IAnnotationEngine::setPenParameters()
         * @param parameters 一整套样式参数（颜色、线宽、字号、填充档、双向箭头）
         * @return 成功返回空载荷；非正时返回写明取值范围的中文原因
         * @details 有选中项时把改样式记进撤销栈，并与栈顶同一条图形的上一次改动合并：
         *          滑块拖一趟只算一步，撤销一次回到抬手之前。
         */
        [[nodiscard]] Base::Expected<void> setPenParameters(const PenParameters &parameters) override;

        /**
         * @brief 起笔，见 IAnnotationEngine::beginStroke()
         * @param point 画布逻辑坐标
         */
        void beginStroke(const QPoint &point) override;

        /**
         * @brief 行进，见 IAnnotationEngine::moveStroke()
         * @param point 当前逻辑坐标
         */
        void moveStroke(const QPoint &point) override;

        /**
         * @brief 收笔入栈，见 IAnnotationEngine::endStroke()
         * @return 新图形的位置；退化时 InvalidArgument
         */
        [[nodiscard]] Base::Expected<AnnotationSelection> endStroke() override;

        /**
         * @brief 设定在途文字，见 IAnnotationEngine::setPendingText()
         * @param text 文本
         * @return 成功返回空载荷；当前工具不是文字时返回 InvalidArgument
         */
        [[nodiscard]] Base::Expected<void> setPendingText(const QString &text) override;

        /**
         * @brief 点名序号的下一个号，见 IAnnotationEngine::setNextNumber()
         * @param number 下一个号，小于 1 会被夹到 1
         * @return 成功返回空载荷；序号工具不在册时返回 InternalError
         */
        /**
         * @brief 把在途的那条落定，见 IAnnotationEngine::finishPendingStroke()
         * @return 成功返回刚落定那条的位置；没有在途图形时返回 InvalidArgument
         */
        [[nodiscard]] Base::Expected<AnnotationSelection> finishPendingStroke() override;
        [[nodiscard]] Base::Expected<void>                setNextNumber(int number) override;

        /// 序号回到自动编号，见 IAnnotationEngine::resetNumbering()
        void resetNumbering() override;

        /// 下一个序号，见 IAnnotationEngine::nextNumber()
        [[nodiscard]] int nextNumber() const override;

        /**
         * @brief 切换序号工具的计数组，见 IAnnotationEngine::setNumberGroup()
         * @param group 组号，夹在 1..kNumberGroupCount
         * @return 成功返回空载荷；序号工具不在册时返回 InternalError
         */
        [[nodiscard]] Base::Expected<void> setNumberGroup(int group) override;

        /// 当前计数组，见 IAnnotationEngine::numberGroup()
        [[nodiscard]] int numberGroup() const override;

        /**
         * @brief 改已放下那条文字的内容，见 IAnnotationEngine::setTextOfSelection()
         * @param text 新的文字
         * @return 成功返回空载荷；四种拒法各带自己的原因（没选中、选的不是文字、文本为空、那条已不在）
         * @details 重写基类接口：与移动、改样式同一形状——先验选中，再压一条撤销命令，
         *          界面不需要知道撤销栈的存在。
         */
        [[nodiscard]] Base::Expected<void> setTextOfSelection(const QString &text) override;

        /**
         * @brief 取选中那条文字的内容，见 IAnnotationEngine::textOfSelection()
         * @return 选中且是文字图形时返回它的文字；其余情况返回空
         */
        [[nodiscard]] std::optional<QString> textOfSelection() const override;

        /**
         * @brief 命中测试，见 IAnnotationEngine::selectionAt()
         * @param point 画布逻辑坐标
         * @return 命中的图形；没有则空
         * @details 重写基类接口：按工具登记顺序的倒序找，层叠最上的先答。
         */
        [[nodiscard]] std::optional<AnnotationSelection> selectionAt(const QPoint &point) const override;

        /**
         * @brief 选中一条图形（替换整个选中集），见 IAnnotationEngine::select()
         * @param selection 目标
         * @return 成功返回空载荷；不在册返回 InvalidArgument
         */
        [[nodiscard]] Base::Expected<void> select(const AnnotationSelection &selection) override;

        /**
         * @brief 把一条图形并进选中集，见 IAnnotationEngine::addToSelection()
         * @param selection 目标
         * @return 成功返回空载荷；不在册返回 InvalidArgument
         */
        [[nodiscard]] Base::Expected<void> addToSelection(const AnnotationSelection &selection) override;

        /**
         * @brief 在选中集里翻转一条图形，见 IAnnotationEngine::toggleSelection()
         * @param selection 目标
         * @return 成功返回空载荷；不在册返回 InvalidArgument
         */
        [[nodiscard]] Base::Expected<void> toggleSelection(const AnnotationSelection &selection) override;

        /// 清空选中集，见 IAnnotationEngine::clearSelection()
        void clearSelection() override;

        /**
         * @brief 当前选中集，见 IAnnotationEngine::selections()
         * @return 选中图形的位置列表
         */
        [[nodiscard]] const std::vector<AnnotationSelection> &selections() const noexcept override;

        /**
         * @brief 选中项的包围盒，见 IAnnotationEngine::selectionBounds()
         * @return 各条的外接框并集；没选中或索引失效时返回空
         * @details 重写基类接口：界面只拿到矩形，不需要认识工具与图形记录。
         */
        [[nodiscard]] std::optional<QRect> selectionBounds() const override;

        /**
         * @brief 选中框的八枚手柄位置，见 IAnnotationEngine::selectionFrame()
         * @return 恰好选中一条时给出手柄点；其余情况为空
         */
        [[nodiscard]] std::optional<std::array<QPointF, 8>> selectionFrame() const override;

        /**
         * @brief 开场一次手柄拖动，见 IAnnotationEngine::beginSelectionDrag()
         * @param handle 抓住的手柄
         * @param point 按下点
         * @return 成功返回空载荷；四种拒法各带自己的原因
         */
        [[nodiscard]] Base::Expected<void> beginSelectionDrag(SelectionHandle handle, const QPoint &point) override;

        /**
         * @brief 手柄拖动中，见 IAnnotationEngine::updateSelectionDrag()
         * @param point 当前点
         * @param modifiers 此刻按住的修饰键（Ctrl 中心点缩放、Shift 等比例缩放）
         * @return 成功返回空载荷；没有开场过或那条已不在册时返回 InvalidArgument
         */
        [[nodiscard]] Base::Expected<void> updateSelectionDrag(const QPoint &point, const SelectionDragModifiers &modifiers) override;

        /// 结束手柄拖动，见 IAnnotationEngine::endSelectionDrag()
        void endSelectionDrag() override;

        /// 上移一层，见 IAnnotationEngine::raiseSelection()
        [[nodiscard]] Base::Expected<void> raiseSelection() override;

        /// 下移一层，见 IAnnotationEngine::lowerSelection()
        [[nodiscard]] Base::Expected<void> lowerSelection() override;

        /// 置顶，见 IAnnotationEngine::bringSelectionToFront()
        [[nodiscard]] Base::Expected<void> bringSelectionToFront() override;

        /// 置底，见 IAnnotationEngine::sendSelectionToBack()
        [[nodiscard]] Base::Expected<void> sendSelectionToBack() override;

        /// 复制选中项到引擎内剪贴板，见 IAnnotationEngine::copySelection()
        [[nodiscard]] Base::Expected<void> copySelection() override;

        /// 粘贴剪贴板，见 IAnnotationEngine::pasteClipboard()
        [[nodiscard]] Base::Expected<void> pasteClipboard() override;

        /**
         * @brief 画全部图形给界面，见 IAnnotationEngine::paintShapes()
         * @param painter 目标画家，坐标系为画布逻辑坐标
         * @details 重写基类接口：底图的物理/逻辑倍比按画布现算，让马赛克这类要取源像素的工具
         *          在屏显与导出两条路上拿到同一份采样。
         */
        void paintShapes(QPainter &painter) const override;

        /**
         * @brief 移动选中项，见 IAnnotationEngine::moveSelectionBy()
         * @param delta 位移
         * @return 成功返回空载荷；没选中或索引失效返回 InvalidArgument
         * @details 重写基类接口：压一条平移命令，并与栈顶同一条图形的上一次位移并成一步，
         *          拖一趟与连按方向键都只算一步撤销。
         */
        [[nodiscard]] Base::Expected<void> moveSelectionBy(const QPoint &delta) override;

        /**
         * @brief 删除选中项，见 IAnnotationEngine::removeSelection()
         * @return 成功返回空载荷；没选中返回 InvalidArgument
         */
        [[nodiscard]] Base::Expected<void> removeSelection() override;

        /**
         * @brief 能否撤销，见 IAnnotationEngine::canUndo()
         * @return true 栈里还有动作
         */
        [[nodiscard]] bool canUndo() const override;

        /**
         * @brief 能否重做，见 IAnnotationEngine::canRedo()
         * @return true 还有可重做的动作
         */
        [[nodiscard]] bool canRedo() const override;

        /**
         * @brief 撤销一步，见 IAnnotationEngine::undo()
         * @return 成功返回空载荷；没得撤返回 InvalidArgument
         */
        [[nodiscard]] Base::Expected<void> undo() override;

        /**
         * @brief 重做一步，见 IAnnotationEngine::redo()
         * @return 成功返回空载荷；没得重做返回 InvalidArgument
         */
        [[nodiscard]] Base::Expected<void> redo() override;

    private:
        /// 全局层叠表里的一条：哪个工具的哪一条图形、压在第几层
        struct ZEntry
        {
            AnnotationTool *tool;   ///< 所属工具，所有权在引擎
            int             index;  ///< 该工具内的索引
            int             zOrder; ///< 全局层叠号
        };

        /// 手柄拖动开场时记下的现场：缩放与旋转都从这一份重新算，中途的取整不会攒成漂移
        struct HandleDrag
        {
            SelectionHandle handle     = SelectionHandle::None; ///< 抓住的手柄
            ToolKind        tool       = ToolKind::Rectangle;   ///< 目标工具
            int             shapeIndex = -1;                    ///< 目标图形索引
            AnnotationShape before;                             ///< 改动之前的那一份图形
            QRectF          localBox;                           ///< 改动之前的局部外接框
            qreal           startAngle = 0.0;                   ///< 按下点相对局部中心的方位角，旋转用
        };

        /**
         * @brief 按种类找工具
         * @param kind 目标种类
         * @return 找到的工具指针（所有权在引擎），不在册时为空
         */
        [[nodiscard]] AnnotationTool *toolFor(ToolKind kind) const noexcept;

        /// 把当前画笔参数下发给全部工具：构造与改参数两处共用一个口径
        void applyPenParametersToTools();

        /// 全部图形按层叠号升序排好（最底层的在前）
        [[nodiscard]] std::vector<ZEntry> zOrderedEntries() const;

        /// 一条选中项到底在不在册
        [[nodiscard]] bool selectionIsValid(const AnnotationSelection &selection) const;

        /// 一条选中项的外接框（含旋转）；索引失效时为空
        [[nodiscard]] std::optional<QRect> boundsOf(const AnnotationSelection &selection) const;

        /// 层叠调整的四个方向
        enum class ReorderMode
        {
            Raise,   ///< 上移一层
            Lower,   ///< 下移一层
            ToFront, ///< 置顶
            ToBack,  ///< 置底
        };

        /// 上移/下移/置顶/置底的共同实现：排出目标次序、逐个换号、一步撤销
        [[nodiscard]] Base::Expected<void> reorderSelection(ReorderMode mode);

        Base::CapturedImage                               m_canvas;                           ///< 当前画布
        std::vector<std::unique_ptr<AnnotationTool>>      m_tools;                            ///< 全部工具，按 ToolKind 顺序登记
        QUndoStack                                        m_undoStack;                        ///< 图形级撤销历史
        std::vector<AnnotationSelection>                  m_selection;                        ///< 当前选中集：多选是常态，单条只是它的一格
        ToolKind                                          m_activeTool = ToolKind::Rectangle; ///< 当前工具
        PenParameters                                     m_penParameters;                    ///< 画笔默认参数整套
        int                                               m_topZ = 0;                         ///< 已发出的最大层叠号：新图形总领到更大的号，落在最上层
        std::optional<HandleDrag>                         m_handleDrag;                       ///< 进行中的手柄拖动现场
        std::vector<std::pair<ToolKind, AnnotationShape>> m_clipboard;                        ///< 引擎内剪贴板：复制下来的图形副本，与系统剪贴板无关
        int                                               m_pasteSeries = 0;                  ///< 连续粘贴的轮次：每粘一次多偏一格，两次粘贴不会完全叠在一起
    };

} // namespace GyanisScribe::Core
