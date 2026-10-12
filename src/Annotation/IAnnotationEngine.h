/**
 * @file IAnnotationEngine.h
 * @brief 标注引擎的对外契约：画布、工具、选择与撤销
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/AnnotationTool.h"
#include "Core/Types.h"

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>

#include <optional>

class QImage;
class QPainter;

namespace GyanisScribe::Annotation
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
        virtual void loadCanvas(Core::CapturedImage image) = 0;

        /**
         * @brief 当前画布
         * @return 载入的那一帧；没载入过时返回空帧（image 为 null）
         */
        [[nodiscard]] virtual const Core::CapturedImage &canvas() const noexcept = 0;

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
        virtual Core::Expected<void> setActiveTool(ToolKind kind) = 0;

        /**
         * @brief 当前工具种类
         * @return 生效中的那一个
         */
        [[nodiscard]] virtual ToolKind activeTool() const noexcept = 0;

        /**
         * @brief 下发画笔默认参数
         * @param color 颜色
         * @param lineWidth 线宽，逻辑像素，必须为正
         * @param fontSize 字号，必须为正
         * @return 成功返回空载荷；线宽或字号不是正数时返回 InvalidArgument，说明收哪些值
         * @note 参数同时下发给所有工具：切工具不该看到笔又变回默认值
         * @note 有选中图形时这条图形也按新参数改样式，并记进撤销栈（同一条图形的连续改动合成一步）：
         *       样式是每条图形自己的属性，工具上的值只给之后画出的图形用。三项与选中项全等时不进步骤。
         */
        virtual Core::Expected<void> setPenParameters(const QColor &color, int lineWidth, int fontSize, int fillStyle) = 0;

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
        virtual Core::Expected<AnnotationSelection> endStroke() = 0;

        /**
         * @brief 给在途的文字图形设定内容
         * @param text 文本
         * @return 成功返回空载荷；当前工具不是文字时返回 InvalidArgument
         */
        virtual Core::Expected<void> setPendingText(const QString &text) = 0;

        /**
         * @brief 点名序号工具的下一个号
         * @param number 下一个号，小于 1 会被夹到 1
         * @return 成功返回空载荷；序号工具不在册时返回 InternalError
         * @details 点名之后编号从那个号开始连排（5、6、7…），不再跟着现有条数走；
         *          回到自动口径用 resetNumbering()。
         */
        [[nodiscard]] virtual Core::Expected<void> setNextNumber(int number) = 0;

        /**
         * @brief 序号工具回到「按现有条数 +1」的自动编号
         */
        virtual void resetNumbering() = 0;

        /**
         * @brief 下一个序号会是多少
         * @return 点名过的号，或现有条数 +1
         */
        [[nodiscard]] virtual int nextNumber() const = 0;

        /**
         * @brief 改已放下那条文字图形的内容
         * @param text 新的文字，不许为空
         * @return 成功返回空载荷；没选中、选中的不是文字图形、文本为空、或那条已经不在了都返回 InvalidArgument
         * @details 走撤销栈：改一次文字是一步操作，Ctrl+Z 要回到改之前那一份，而不是把整条抹掉。
         */
        virtual Core::Expected<void> setTextOfSelection(const QString &text) = 0;

        /**
         * @brief 取选中那条文字图形的内容
         * @return 选中且是文字图形时返回它当前的文字；其余情况返回空
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
         * @brief 选中一条图形
         * @param selection 目标图形
         * @return 成功返回空载荷；工具或索引不在册时返回 InvalidArgument
         */
        virtual Core::Expected<void> select(const AnnotationSelection &selection) = 0;

        /**
         * @brief 当前选中项
         * @return 有选中时返回那一条，否则空
         */
        [[nodiscard]] virtual const std::optional<AnnotationSelection> &selection() const noexcept = 0;

        /**
         * @brief 当前选中图形的包围盒
         * @return 画布逻辑坐标下的矩形；没有选中项时返回空
         * @note 只给界面画选框用：exportImage 不烘选框，否则选中的那条一保存就带上虚线
         */
        [[nodiscard]] virtual std::optional<QRect> selectionBounds() const = 0;

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
         * @details 同一条图形的连续位移并成一步撤销：拖一趟要发几十次位移，方向键也能连按好几格，
         *          并成一步之后撤销一次就回到这一趟开始之前。
         */
        virtual Core::Expected<void> moveSelectionBy(const QPoint &delta) = 0;

        /**
         * @brief 删除选中项，进撤销栈
         * @return 成功返回空载荷；没有选中项时返回 InvalidArgument
         */
        virtual Core::Expected<void> removeSelection() = 0;

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
        virtual Core::Expected<void> undo() = 0;

        /**
         * @brief 重做一步
         * @return 成功返回空载荷；没得重做时返回 InvalidArgument
         */
        virtual Core::Expected<void> redo() = 0;

    signals:
        /**
         * @brief 画布上的图形集合变了
         * @note 界面据此重画预览，导出与保存不订阅它（它们自己取当前状态）
         */
        void canvasChanged();
    };

} // namespace GyanisScribe::Annotation
