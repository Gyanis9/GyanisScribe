/**
 * @file IAnnotationEngine.h
 * @brief 标注引擎的对外契约：画布、工具、选择与撤销
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/AnnotationTool.h"
#include "Core/Annotation/PenParameters.h"
#include "Base/Types.h"

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QString>

#include <array>
#include <optional>
#include <vector>

class QImage;
class QPainter;

namespace GyanisScribe::Core
{

    /**
     * @brief 一条被选中的图形
     */
    struct AnnotationSelection
    {
        ToolKind tool       = ToolKind::Rectangle; ///< 所属工具
        int      shapeIndex = -1;                  ///< 该工具内的索引，-1 表示没有选中
    };

    /**
     * @brief 选中框上的手柄
     * @details 八个缩放方向按「左上、上、右上、右、右下、下、左下、左」的次序登记，
     *          与引擎交回的框角数组逐位对应；Rotate 是框外的旋转手柄。
     */
    enum class SelectionHandle
    {
        None,        ///< 没抓住任何手柄
        TopLeft,     ///< 左上角
        Top,         ///< 上边中点
        TopRight,    ///< 右上角
        Right,       ///< 右边中点
        BottomRight, ///< 右下角
        Bottom,      ///< 下边中点
        BottomLeft,  ///< 左下角
        Left,        ///< 左边中点
        Rotate,      ///< 旋转手柄（框外那一枚圆点）
    };

    /**
     * @brief 拖手柄时按住的修饰键：缩放的两条老约定各占一个开关
     * @details 记的是「这一刻按着什么」而不是按下手柄那一刻按着什么：拖到一半再按或松开
     *          是常规操作，按开场那一刻定档会让人以为键没接上。
     */
    struct SelectionDragModifiers
    {
        bool fromCenter   = false; ///< Ctrl：原中心是不动点，拖一条边对面一条对向走同样多的距离
        bool proportional = false; ///< Shift：保持原框长宽比，只拖一轴时另一轴按同一倍率补偿
    };

    /**
     * @brief 标注引擎
     * @details 界面只跟这一个接口打交道：切换工具、走笔、选中、撤销、导出。
     *          引擎不依赖任何具体 Widget，因此容器里没有界面也能把整条标注流程跑进用例。
     */
    class IAnnotationEngine : public QObject
    {
        Q_OBJECT

    public:
        using QObject::QObject;

        /**
         * @brief 析构引擎
         */
        ~IAnnotationEngine() override = default;

        /**
         * @brief 载入要标注的那一帧
         * @param image 抓屏产物
         * @note 载入会清空已有的图形与撤销历史：换一帧就换一张画布，旧图形压在新画布上没有意义
         */
        virtual void loadCanvas(Base::CapturedImage image) = 0;

        /**
         * @brief 换掉底图、保留画面上的一切：图形、选中、层序与撤销历史都不动
         * @param image 重新抓到的那一帧（刷新截图）
         * @details 刷新截图走的这一条：屏幕上的内容变了、标注还留在原地，撤销栈也必须留着——
         *          用户刷完还能把刚才画错的那笔撤掉。取底图像素的工具（马赛克、模糊、放大镜）
         *          下一次绘制就会读到新底图，不用另做处理。
         * @note 与 loadCanvas 的分别就在「清不清」这一条：换一张完全不同的画布用 loadCanvas，
         *       同一块区域的再抓一帧用这一条。
         */
        virtual void refreshCanvas(Base::CapturedImage image) = 0;

        /**
         * @brief 当前画布
         * @return 载入的那一帧；没载入过时返回空帧（image 为 null）
         */
        [[nodiscard]] virtual const Base::CapturedImage &canvas() const noexcept = 0;

        /**
         * @brief 导出烘焙好的图
         * @return 画布的副本上依次把各工具的图形烘进去；没画布时返回空图
         * @note 画布倍比不是 1.0 时图上带着同一倍比：下游（贴图窗口）按 devicePixelRatio 反推
         *       逻辑尺寸，丢了倍比一块 150% 屏上的截图会被开成放大 1.5 倍的窗口
         */
        [[nodiscard]] virtual QImage exportImage() const = 0;

        /**
         * @brief 切换当前工具
         * @param kind 目标工具种类
         * @return 成功返回空载荷；种类不在册时返回 InvalidArgument
         */
        virtual Base::Expected<void> setActiveTool(ToolKind kind) = 0;

        /**
         * @brief 当前工具种类
         * @return 生效中的那一个
         */
        [[nodiscard]] virtual ToolKind activeTool() const noexcept = 0;

        /**
         * @brief 下发画笔默认参数
         * @param parameters 一整套样式参数（颜色、线宽、字号、填充档、双向箭头、马赛克粒度、高亮透明度、文字样式）
         * @return 成功返回空载荷；线宽或字号不是正数时返回 InvalidArgument，说明收哪些值
         * @note 参数同时下发给所有工具：切工具不该看到笔又变回默认值
         * @note 有选中图形时这些图形也按新参数改样式，并记进撤销栈（同一条图形的连续改动合成一步）：
         *       样式是每条图形自己的属性，工具上的值只给之后画出的图形用。全部选中项的样式都与之全等时不进步骤。
         */
        virtual Base::Expected<void> setPenParameters(const PenParameters &parameters) = 0;

        /**
         * @brief 起笔
         * @param point 画布逻辑坐标
         */
        virtual void beginStroke(const QPoint &point) = 0;

        /**
         * @brief 行进
         * @param point 当前逻辑坐标
         */
        virtual void moveStroke(const QPoint &point) = 0;

        /**
         * @brief 收笔并把这条图形放进撤销栈
         * @return 成功返回选中的新图形位置；图形退化时返回 InvalidArgument
         */
        virtual Base::Expected<AnnotationSelection> endStroke() = 0;

        /**
         * @brief 给在途的文字图形设定内容
         * @param text 文本
         * @return 成功返回空载荷；当前工具不是文字时返回 InvalidArgument
         */
        virtual Base::Expected<void> setPendingText(const QString &text) = 0;

        /**
         * @brief 把在途的那条落定（折线双击收笔走的这一条）
         * @return 成功返回刚落定那条的位置；没有在途图形时返回 InvalidArgument
         * @details 与 endStroke() 的区别在「谁说了算」：endStroke 是松手时试落定，多步工具这时
         *          还在等下一段、会交回空（错误码是 Busy）；这一条是用户明确说「就到这里」，
         *          工具才把在途图形交出来。
         */
        [[nodiscard]] virtual Base::Expected<AnnotationSelection> finishPendingStroke() = 0;

        /**
         * @brief 点名序号工具当前计数组的下一个号
         * @param number 下一个号，小于 1 会被夹到 1
         * @return 成功返回空载荷；序号工具不在册时返回 InternalError
         * @details 点名之后该组编号从那个号开始连排（5、6、7…），不再跟着现有条数走；
         *          回到自动口径用 resetNumbering()。
         */
        [[nodiscard]] virtual Base::Expected<void> setNextNumber(int number) = 0;

        /**
         * @brief 序号工具当前计数组回到「按现有条数 +1」的自动编号
         */
        virtual void resetNumbering() = 0;

        /**
         * @brief 下一个序号会是多少（当前计数组）
         * @return 该组点名过的号，或该组现有条数 +1
         */
        [[nodiscard]] virtual int nextNumber() const = 0;

        /**
         * @brief 切换序号工具的计数组
         * @param group 组号，夹在 1..kNumberGroupCount
         * @return 成功返回空载荷；序号工具不在册时返回 InternalError
         * @details 每一组各自连排自己的号：切组之后再落牌从那一组的下一号接着走，
         *          各组点过的名也各记各的。
         */
        [[nodiscard]] virtual Base::Expected<void> setNumberGroup(int group) = 0;

        /**
         * @brief 当前计数组
         * @return 1..kNumberGroupCount
         */
        [[nodiscard]] virtual int numberGroup() const = 0;

        /**
         * @brief 改已放下那条文字图形的内容
         * @param text 新的文字，不许为空
         * @return 成功返回空载荷；没选中、选中的不是唯一一条文字图形、文本为空、或那条已经不在了都返回 InvalidArgument
         * @details 走撤销栈：改一次文字是一步操作，Ctrl+Z 要回到改之前那一份，而不是把整条抹掉。
         */
        virtual Base::Expected<void> setTextOfSelection(const QString &text) = 0;

        /**
         * @brief 取选中那条文字图形的内容
         * @return 恰好选中一条文字图形时返回它当前的文字；其余情况返回空
         * @details 界面拿它预填重编辑的输入框：不预填的话用户要重打一遍，改一个字也得从头输入。
         */
        [[nodiscard]] virtual std::optional<QString> textOfSelection() const = 0;

        /**
         * @brief 命中测试
         * @param point 画布逻辑坐标
         * @return 命中的图形（层叠最上的那条）；没命中返回空
         */
        [[nodiscard]] virtual std::optional<AnnotationSelection> selectionAt(const QPoint &point) const = 0;

        /**
         * @brief 选中一条图形（替换整个选中集）
         * @param selection 目标图形
         * @return 成功返回空载荷；工具或索引不在册时返回 InvalidArgument
         */
        virtual Base::Expected<void> select(const AnnotationSelection &selection) = 0;

        /**
         * @brief 把一条图形并进选中集（Shift+点选那一路）
         * @param selection 目标图形
         * @return 成功返回空载荷；工具或索引不在册时返回 InvalidArgument
         * @note 已经在选中集里就什么都不做：Shift 连点同一条不该造出重复项
         */
        virtual Base::Expected<void> addToSelection(const AnnotationSelection &selection) = 0;

        /**
         * @brief 在选中集里翻转一条图形（Ctrl+点选那一路）
         * @param selection 目标图形
         * @return 成功返回空载荷；工具或索引不在册时返回 InvalidArgument
         */
        virtual Base::Expected<void> toggleSelection(const AnnotationSelection &selection) = 0;

        /**
         * @brief 清空选中集
         * @note 点空白起笔时界面会先叫这一声：选框不该留着指向「刚才那条」
         */
        virtual void clearSelection() = 0;

        /**
         * @brief 当前选中集
         * @return 选中图形的位置列表；没有选中时为空
         */
        [[nodiscard]] virtual const std::vector<AnnotationSelection> &selections() const noexcept = 0;

        /**
         * @brief 当前选中图形的包围盒
         * @return 画布逻辑坐标下的矩形；多选时是各条的并集；没有选中项时返回空
         * @note 只给界面画选框用：exportImage 不烘选框，否则选中的那条一保存就带上虚线
         */
        [[nodiscard]] virtual std::optional<QRect> selectionBounds() const = 0;

        /**
         * @brief 选中框的八枚手柄位置
         * @return 手柄点（画布逻辑坐标），次序与 SelectionHandle 的八个方向逐位对应；旋转过的图形跟着一起转
         * @details 只在恰好选中一条时给出：多选的手柄该按哪条算没有共识，界面对多选只画联合虚线框。
         */
        [[nodiscard]] virtual std::optional<std::array<QPointF, 8>> selectionFrame() const = 0;

        /**
         * @brief 开场一次手柄拖动（按下枚手柄那一刻叫）
         * @param handle 抓住的手柄；Rotate 表示旋转那一路
         * @param point 按下点的画布逻辑坐标
         * @return 成功返回空载荷；没抓住手柄、选中的不是恰好一条、或那条已不在册时返回 InvalidArgument
         * @details 开场时记下这一步改动之前的整条图形：缩放与旋转都从那一份重新算，
         *          中途的取整不会一格格攒成漂移，撤销也只需要这一份原样放回去。
         */
        virtual Base::Expected<void> beginSelectionDrag(SelectionHandle handle, const QPoint &point) = 0;

        /**
         * @brief 手柄拖动中
         * @param point 当前画布逻辑坐标
         * @param modifiers 此刻按住的修饰键（Ctrl 中心点缩放、Shift 等比例缩放；旋转档不吃这两键）
         * @return 成功返回空载荷；没有开场过或那条已不在册时返回 InvalidArgument
         * @details 一趟拖动并成一步撤销：起点固定在开场时那一份图形上，逐个中间点覆盖命令的终点。
         */
        virtual Base::Expected<void> updateSelectionDrag(const QPoint &point, const SelectionDragModifiers &modifiers) = 0;

        /**
         * @brief 结束手柄拖动（松手那一刻叫）
         * @note 没开场过也叫得：重复的收场不该报错
         */
        virtual void endSelectionDrag() = 0;

        /**
         * @brief 把选中的图形往上挪一层
         * @return 成功返回空载荷；没有选中项时返回 InvalidArgument
         * @note 已经在最上层的再挪是空动作：不进撤销栈
         */
        virtual Base::Expected<void> raiseSelection() = 0;

        /**
         * @brief 把选中的图形往下挪一层
         * @return 成功返回空载荷；没有选中项时返回 InvalidArgument
         */
        virtual Base::Expected<void> lowerSelection() = 0;

        /**
         * @brief 把选中的图形挪到最上层
         * @return 成功返回空载荷；没有选中项时返回 InvalidArgument
         */
        virtual Base::Expected<void> bringSelectionToFront() = 0;

        /**
         * @brief 把选中的图形挪到最下层
         * @return 成功返回空载荷；没有选中项时返回 InvalidArgument
         */
        virtual Base::Expected<void> sendSelectionToBack() = 0;

        /**
         * @brief 把选中的图形复制进引擎内的剪贴板
         * @return 成功返回空载荷；没有选中项时返回 InvalidArgument
         * @details 剪贴板只是一份图形副本，不碰系统剪贴板：系统剪贴板上放的是成品图，
         *          两者混用会让「复制」这个词一会儿指标注一会儿指整张图。
         */
        virtual Base::Expected<void> copySelection() = 0;

        /**
         * @brief 粘贴剪贴板里的图形
         * @return 成功返回空载荷；剪贴板为空时返回 InvalidArgument
         * @details 粘出来的每条带一点右下偏移（连续粘贴逐次加一个），压在最上层，并成为新的选中集；
         *          一次粘贴是一步撤销，Ctrl+Z 一下收走整批。
         */
        virtual Base::Expected<void> pasteClipboard() = 0;

        /**
         * @brief 把全部图形画给界面
         * @param painter 目标画家，坐标系必须是画布逻辑坐标（缩放由调用方设好）
         * @details 与 exportImage 分工：导出只烘已落定的图形，屏显还要带上正在拖的那一条，
         *          所以这里画的是「当前看到的样子」，不许被拿去保存。
         */
        virtual void paintShapes(QPainter &painter) const = 0;

        /**
         * @brief 移动选中项
         * @param delta 逻辑坐标位移
         * @return 成功返回空载荷；没有选中项或索引失效时返回 InvalidArgument
         * @details 多选时整组一起走。同一条图形的连续位移并成一步撤销：拖一趟要发几十次位移，
         *          方向键也能连按好几格，并成一步之后撤销一次就回到这一趟开始之前。
         */
        virtual Base::Expected<void> moveSelectionBy(const QPoint &delta) = 0;

        /**
         * @brief 删除选中项，进撤销栈
         * @return 成功返回空载荷；没有选中项时返回 InvalidArgument
         * @note 多选时整组一次删掉，也是一步撤销
         */
        virtual Base::Expected<void> removeSelection() = 0;

        /**
         * @brief 能否撤销
         * @return true 栈里还有可撤的动作
         */
        [[nodiscard]] virtual bool canUndo() const = 0;

        /**
         * @brief 能否重做
         * @return true 栈里还有可重做的动作
         */
        [[nodiscard]] virtual bool canRedo() const = 0;

        /**
         * @brief 撤销一步
         * @return 成功返回空载荷；没得撤时返回 InvalidArgument，界面对此只需把按钮置灰
         */
        virtual Base::Expected<void> undo() = 0;

        /**
         * @brief 重做一步
         * @return 成功返回空载荷；没得重做时返回 InvalidArgument
         */
        virtual Base::Expected<void> redo() = 0;

    signals:
        /**
         * @brief 画布上的图形集合变了
         * @note 界面据此重画预览，导出与保存不订阅它（它们自己取当前状态）
         */
        void canvasChanged();
    };

} // namespace GyanisScribe::Core
