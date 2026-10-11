/**
 * @file AnnotationEngine.h
 * @brief 标注引擎实现：工具集合、选择、撤销栈与导出
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/IAnnotationEngine.h"

#include <QUndoStack>

#include <vector>

namespace GyanisScribe::Annotation
{

    /**
     * @brief IAnnotationEngine 的实现
     * @details 工具集在构造时按 ToolKind 建齐，切换只是改「谁接收事件」——
     *          每类图形归各自工具管，撤销栈管的正是「插哪条、取哪条、移哪条」这三件事。
     * @note ponytail: 层叠顺序按工具种类的登记顺序（矩形在最下、序号在最上），
     *       做不到「后画的一条一定压住先画的另一条」。要补这一点，得把图形收进引擎的一张全局表，
     *       工具只作为交互器存在——那时命中测试与绘制都读同一张表。
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
        void loadCanvas(Core::CapturedImage image) override;

        /**
         * @brief 当前画布，见 IAnnotationEngine::canvas()
         * @return 载入的那一帧
         */
        [[nodiscard]] const Core::CapturedImage &canvas() const noexcept override;

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
        Core::Expected<void> setActiveTool(ToolKind kind) override;

        /**
         * @brief 当前工具种类，见 IAnnotationEngine::activeTool()
         * @return 生效中的那一个
         */
        [[nodiscard]] ToolKind activeTool() const noexcept override;

        /**
         * @brief 下发画笔参数，见 IAnnotationEngine::setPenParameters()
         * @param color 颜色
         * @param lineWidth 线宽，必须为正
         * @param fontSize 字号，必须为正
         * @return 成功返回空载荷；非正时返回写明取值范围的中文原因
         * @details 有选中项时把改样式记进撤销栈，并与栈顶同一条图形的上一次改动合并：
         *          滑块拖一趟只算一步，撤销一次回到抬手之前。
         */
        Core::Expected<void> setPenParameters(const QColor &color, int lineWidth, int fontSize) override;

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
        Core::Expected<AnnotationSelection> endStroke() override;

        /**
         * @brief 设定在途文字，见 IAnnotationEngine::setPendingText()
         * @param text 文本
         * @return 成功返回空载荷；当前工具不是文字时返回 InvalidArgument
         */
        Core::Expected<void> setPendingText(const QString &text) override;

        /**
         * @brief 改已放下那条文字的内容，见 IAnnotationEngine::setTextOfSelection()
         * @param text 新的文字
         * @return 成功返回空载荷；四种拒法各带自己的原因（没选中、选的不是文字、文本为空、那条已不在）
         * @details 重写基类接口：与移动、改样式同一形状——先验选中，再压一条撤销命令，
         *          界面不需要知道撤销栈的存在。
         */
        Core::Expected<void> setTextOfSelection(const QString &text) override;

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
         * @brief 选中一条图形，见 IAnnotationEngine::select()
         * @param selection 目标
         * @return 成功返回空载荷；不在册返回 InvalidArgument
         */
        Core::Expected<void> select(const AnnotationSelection &selection) override;

        /**
         * @brief 取消选中，见 IAnnotationEngine::clearSelection()
         */
        void clearSelection() override;

        /**
         * @brief 当前选中项，见 IAnnotationEngine::selection()
         * @return 有选中时那一条
         */
        [[nodiscard]] const std::optional<AnnotationSelection> &selection() const noexcept override;

        /**
         * @brief 选中项的包围盒，见 IAnnotationEngine::selectionBounds()
         * @return 由所属工具的 boundingBoxOf 算出；没选中或索引失效时返回空
         * @details 重写基类接口：界面只拿到矩形，不需要认识工具与图形记录。
         */
        [[nodiscard]] std::optional<QRect> selectionBounds() const override;

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
        Core::Expected<void> moveSelectionBy(const QPoint &delta) override;

        /**
         * @brief 删除选中项，见 IAnnotationEngine::removeSelection()
         * @return 成功返回空载荷；没选中返回 InvalidArgument
         */
        Core::Expected<void> removeSelection() override;

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
        Core::Expected<void> undo() override;

        /**
         * @brief 重做一步，见 IAnnotationEngine::redo()
         * @return 成功返回空载荷；没得重做返回 InvalidArgument
         */
        Core::Expected<void> redo() override;

    private:
        /**
         * @brief 按种类找工具
         * @param kind 目标种类
         * @return 找到的工具指针（所有权在引擎），不在册时为空
         */
        [[nodiscard]] AnnotationTool *toolFor(ToolKind kind) const noexcept;

        Core::CapturedImage                          m_canvas;                           ///< 当前画布
        std::vector<std::unique_ptr<AnnotationTool>> m_tools;                            ///< 全部工具，按 ToolKind 顺序登记
        QUndoStack                                   m_undoStack;                        ///< 图形级撤销历史
        std::optional<AnnotationSelection>           m_selection;                        ///< 当前选中项
        ToolKind                                     m_activeTool = ToolKind::Rectangle; ///< 当前工具
        QColor                                       m_color      = QColor(255, 0, 0);   ///< 画笔默认色
        int                                          m_lineWidth  = 3;                   ///< 默认线宽
        int                                          m_fontSize   = 16;                  ///< 默认字号
    };

} // namespace GyanisScribe::Annotation
