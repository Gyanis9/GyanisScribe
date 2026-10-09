/**
 * @file AnnotationView.h
 * @brief 标注视图：显示画布与图形，并把鼠标的画与拖转给引擎
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/IAnnotationEngine.h"

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QWidget>

class QLineEdit;

namespace GyanisScribe::Annotation
{

    /**
     * @brief 算画布在控件里的显示区
     * @param widgetSize 控件尺寸，逻辑像素
     * @param canvasLogicalSize 画布逻辑尺寸
     * @return 等比缩放后居中的矩形；任一尺寸不成立时返回空矩形
     * @details 保比例居中，不拉伸：截图被拉变形之后，标注的坐标换算就不再是同一个比例。
     */
    [[nodiscard]] QRect displayRectFor(const QSize &widgetSize, const QSize &canvasLogicalSize);

    /**
     * @brief 控件坐标换算到画布逻辑坐标
     * @param widgetPoint 控件内坐标
     * @param displayRect displayRectFor 的结果
     * @param canvasLogicalSize 画布逻辑尺寸
     * @return 画布逻辑坐标；落在画布外的部分夹到边缘
     * @note 夹住而不是拒绝：拖出画布边缘的意思是画到边界，报「参数不合法」只会让画笔在边缘卡住
     */
    [[nodiscard]] QPoint toCanvasPoint(const QPoint &widgetPoint, const QRect &displayRect, const QSize &canvasLogicalSize);

    /**
     * @brief 画布逻辑坐标换算回控件坐标
     * @param canvasPoint 画布逻辑坐标
     * @param displayRect displayRectFor 的结果
     * @param canvasLogicalSize 画布逻辑尺寸
     * @return 控件内坐标，供行内输入框这类别墅控件定位
     */
    [[nodiscard]] QPoint toWidgetPoint(const QPoint &canvasPoint, const QRect &displayRect, const QSize &canvasLogicalSize);

    /**
     * @brief 标注视图
     * @details 视图不认识工具栏也不认识窗口：它只做两件事——把画布与图形画出来，
     *          把鼠标的按下/移动/抬起转成引擎的起笔/行进/收笔（或选中后的拖动）。
     *          每帧都直接画底图 + 图形，不走 exportImage：导出只烘已落定的图形，
     *          屏显要立刻看到在途那一条，两条路分开才不会让预览改变最终产物。
     * @note 按下时先做命中测试：点中已有图形就选中并进入拖动，点的是空白处才起笔画新的。
     *        因此「在已有图形内部再画一个」做不到——需求的八个工具里没有独立的选择工具，
     *        真要给就得先补一档工具种类。
     */
    class AnnotationView : public QWidget
    {
        Q_OBJECT

    public:
        /**
         * @brief 构造视图并接上引擎的变更通知
         * @param engine 标注引擎，所有权在宿主，必须活过本视图
         * @param parent Qt 父对象
         */
        explicit AnnotationView(IAnnotationEngine &engine, QWidget *parent = nullptr);

        AnnotationView(const AnnotationView &)            = delete;
        AnnotationView &operator=(const AnnotationView &) = delete;

        /**
         * @brief 画布的逻辑尺寸
         * @return 引擎当前画布的 logicalSize
         */
        [[nodiscard]] QSize sizeHint() const override;

    protected:
        /**
         * @brief 绘制底图、全部图形与选框
         * @param event Qt 绘制事件
         */
        void paintEvent(QPaintEvent *event) override;

        /**
         * @brief 鼠标按下：命中已有图形就选中，否则起笔
         * @param event 按下事件
         */
        void mousePressEvent(QMouseEvent *event) override;

        /**
         * @brief 鼠标移动：行进或拖动选中项
         * @param event 移动事件
         */
        void mouseMoveEvent(QMouseEvent *event) override;

        /**
         * @brief 鼠标抬起：收笔入栈或结束拖动
         * @param event 抬起事件
         */
        void mouseReleaseEvent(QMouseEvent *event) override;

        /**
         * @brief 拦行内输入框的 Esc：取消这一条文字标注
         * @param watched 被观察对象
         * @param event 事件
         * @return 处理掉了返回 true
         * @details QLineEdit 的 Esc 不是信号，只能靠事件过滤器接；不接的话按 Esc 会穿到窗口去关掉整轮标注
         */
        bool eventFilter(QObject *watched, QEvent *event) override;

    private:
        /**
         * @brief 文字工具的行内输入框：在按下处摆一个，提交后落定图形
         * @param canvasPoint 画布逻辑坐标
         */
        void openTextEditor(const QPoint &canvasPoint);

        /// 当前显示区，绘制与坐标换算共用同一个算法
        [[nodiscard]] QRect currentDisplayRect() const;

        IAnnotationEngine &m_engine;               ///< 标注引擎，不拥有
        bool               m_stroking     = false; ///< 正在画一条新图形
        bool               m_dragging     = false; ///< 正在拖动选中项
        bool               m_awaitingText = false; ///< 文字在途：这条图形由输入框提交，而不是由抬笔提交
        QPoint             m_lastCanvasPoint;      ///< 上一次事件换算到的画布坐标
        QLineEdit         *m_textEditor = nullptr; ///< 行内输入框，用完即销毁
    };

} // namespace GyanisScribe::Annotation
