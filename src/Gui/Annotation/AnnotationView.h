/**
 * @file AnnotationView.h
 * @brief 标注视图：显示画布与图形，并把鼠标的画与拖转给引擎
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/IAnnotationEngine.h"
#include "Core/Capture/ScreenUtils.h"
#include "Core/Pick/PickGeometry.h"

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QSizeF>
#include <QWidget>

#include <array>

class QTextEdit;

namespace GyanisScribe::Gui
{

    /// 缩放手柄的边长，控件像素：太小点不着、太大盖住图形本身
    inline constexpr int kHandleSizeWidget = 7;

    /// 旋转手柄离选中框上边中点的距离，控件像素
    inline constexpr int kRotateHandleOffsetWidget = 24;

    /// 抓住手柄的判定半径，控件像素：手柄很小，不给余量就抓不住
    inline constexpr int kHandleHitRadiusWidget = 8;

    /**
     * @brief 重裁拖边时放大镜的取样源
     * @details 冻帧是框选那一刻的整屏画面：只从当前画布取样的话，拖边新露出的区域恰好不在画布里，
     *          放大镜边上会缺一圈正是用户要看的像素。
     */
    struct CropMagnifierSource
    {
        QImage frame;                  ///< 整屏冻帧；空表示这一轮没有取样源
        QRect  frameGeometry;          ///< 冻帧在桌面上的全局矩形
        qreal  devicePixelRatio = 1.0; ///< 冻帧物理像素与逻辑像素之比
    };

    /**
     * @brief 控件坐标换算到画布逻辑坐标
     * @param widgetPoint 控件内坐标
     * @param displayRect Core::displayRectFor 的结果
     * @param canvasLogicalSize 画布逻辑尺寸
     * @return 画布逻辑坐标；落在画布外的部分夹到边缘
     * @note 夹住而不是拒绝：拖出画布边缘的意思是画到边界，报「参数不合法」只会让画笔在边缘卡住
     */
    [[nodiscard]] QPoint toCanvasPoint(const QPoint &widgetPoint, const QRect &displayRect, const QSize &canvasLogicalSize);

    /**
     * @brief 画布逻辑坐标换算回控件坐标
     * @param canvasPoint 画布逻辑坐标
     * @param displayRect Core::displayRectFor 的结果
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
     * @note 按下的优先顺序：选中框的手柄 → 命中已有图形（`Ctrl` 反选、`Shift` 加选、裸按单选并拖）
     *        → 空白处起笔。点空白会先清掉选中：选框不该留着指向「刚才那条」。
     *        因此「在已有图形内部再画一个」做不到——需求列的那几件工具里没有独立的选择工具，
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
        explicit AnnotationView(Core::IAnnotationEngine &engine, QWidget *parent = nullptr);

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
         * @brief 双击一条已放下的文字：原地把它重新打开来改
         * @param event 双击事件
         * @details 判据是「命中的那一条是文字图形」且此刻没有在途笔画或输入框：文字工具正等着
         *          输入新文字时，第二次按下不该被当成重编，否则会把刚要落定的那条改掉。
         */
        void mouseDoubleClickEvent(QMouseEvent *event) override;

        /**
         * @brief 拦行内输入框的 Esc、Ctrl+回车与失焦
         * @param watched 被观察对象
         * @param event 事件
         * @return 处理掉了返回 true
         * @details 三者都不是 QTextEdit 的信号，只能靠事件过滤器接，且都要抢在它自己处理之前：
         *          多行输入里回车只负责换行，提交改由 Ctrl+回车承担（按晚了就先插进一个换行）；
         *          点到画布、工具栏或别的窗口算「就这些字」，与单行时代 QLineEdit 的
         *          editingFinished 同解。不接的话按 Esc 会穿到窗口去关掉整轮标注。
         */
        bool eventFilter(QObject *watched, QEvent *event) override;

    signals:
        /**
         * @brief 「选择」档拖完窗口边框，交出新的一圈全局几何
         * @param globalGeometry 新的窗口几何（全局逻辑坐标）
         * @details 视图只改窗口几何做预览；重抓与换画布由宿主动手。
         */
        void captureResized(const QRect &globalGeometry);

        /// 拖边框途中每次几何变化都报一次：宿主据此把冻帧裁成实时预览
        void captureResizeMoved(const QRect &globalGeometry);

    public:
        /**
         * @brief 拖边框途中用冻帧裁出来的预览顶替旧画布
         * @param image 按新范围裁好的冻帧
         * @param shapeOrigin 旧画布原点在预览里的位置，预览逻辑坐标
         * @param previewLogicalSize 预览图对应的逻辑尺寸；空尺寸表示不缩放，图形按逻辑坐标直接落
         */
        void setCropPreview(const QImage &image, const QPointF &shapeOrigin, const QSizeF &previewLogicalSize);

        /// 收起重裁预览，回到引擎画布
        void clearCropPreview();

        /**
         * @brief 交给重裁放大镜一份整屏冻帧
         * @param source 冻帧、它在桌面上的全局矩形与缩放比；空冻帧表示这一轮没有像素取样源
         * @details 冻帧随本轮换画布一起换（开窗、刷新、重裁）；只换几何不换源会让放大镜停在上一张画面上。
         */
        void setCropMagnifierSource(const CropMagnifierSource &source);

        /**
         * @brief 装上这一轮的放大镜视野档
         * @param view 视野档；off 只收像素插图，读数条仍在
         */
        void setMagnifierView(Core::MagnifierView view);

        /// 重裁拖边时放大镜像素插图的矩形（控件坐标）；不画时为空
        [[nodiscard]] QRect cropMagnifierRectForTesting() const;

        /// 重裁拖边时放大镜读数条的矩形（控件坐标）；不画时为空
        [[nodiscard]] QRect cropMagnifierReadoutRectForTesting() const;

    private:
        /**
         * @brief 文字的行内输入框：按下处在首行基线上摆一个多行的
         * @param canvasPoint 画布逻辑坐标，文字首行基线落在这条线上；输入框上沿按 ascent 往上退
         * @param initial 输入框的初始内容；新画的那条传空串
         * @param reEditing true 表示改的是已放下的那一条（提交走 setTextOfSelection），false 是新画一条
         * @note 框只在 \n 处断行（不软换行）：屏上看到的断行就是成品里的断行
         */
        void openTextEditor(const QPoint &canvasPoint, const QString &initial, bool reEditing);

        /**
         * @brief 提交输入框里的内容并收起输入框
         * @details 重编走 setTextOfSelection（进撤销栈，界面不碰图形本体），新画一条走
         *          setPendingText + 收笔；尾巴上的空行在提交前削掉，不留在图形里当空白行
         */
        void commitTextEditor();

        /**
         * @brief 收起输入框但不提交：Esc 取消这一条时用
         * @details 重编那一格原内容一个字没动，直接收起；新画那一格还要把在途图形喂给引擎
         *          收掉（空文本会被判退化而丢弃，正是「取消成功」的样子）
         */
        void cancelTextEditor();

        /// 收起输入框：销毁补刀与清成员只此一处，免得两条收场路各漏一半
        void closeTextEditor();

        /**
         * @brief 按内容行数调输入框的高度
         * @details 敲一行回车框就长一行，封顶在显示区高度之内——再多就盖住底图，
         *          看不见自己在给哪块写字（超出部分靠光标自带滚动）
         */
        void resizeTextEditorToContent();

        /// 当前显示区，绘制与坐标换算共用同一个算法
        [[nodiscard]] QRect currentDisplayRect() const;

        /**
         * @brief 旋转手柄在控件坐标里的落点
         * @param frame 引擎给的八枚手柄点（画布逻辑坐标）
         * @return 控件内的点：上边中点往外挪一个固定偏移；那个位置出画布时翻到下边
         * @details 翻边是因为贴着上边缘的图形，柄按常规摆法会落到窗口外面，根本点不着。
         */
        [[nodiscard]] QPointF rotateHandlePointWidget(const std::array<QPointF, 8> &frame) const;

        /**
         * @brief 这个控件坐标点抓住了哪枚手柄
         * @param widgetPoint 控件内坐标
         * @return 抓住的手柄；没抓住任何一枚时返回 None
         * @details 在控件坐标里按距离判：容差是「手指能命中的距离」，不该随画布缩放变粗变细。
         */
        [[nodiscard]] Core::SelectionHandle handleAt(const QPoint &widgetPoint) const;

        Core::IAnnotationEngine &m_engine;           ///< 标注引擎，不拥有
        bool                     m_stroking = false; ///< 正在画一条新图形

        bool       m_ignoreNextRelease = false; ///< 双击收笔之后吞掉那一次松手
        bool       m_dragging          = false; ///< 正在拖动选中项
        bool       m_handleDragging    = false; ///< 正在拖选中框上的手柄（缩放或旋转）
        bool       m_awaitingText      = false; ///< 文字在途：这条图形由输入框提交，而不是由抬笔提交
        bool       m_reEditingText     = false; ///< 输入框此刻改的是已放下的那一条，而不是新画的一条
        QPoint     m_lastCanvasPoint;           ///< 上一次事件换算到的画布坐标
        QTextEdit *m_textEditor = nullptr;      ///< 行内输入框（多行），用完即销毁

        QImage  m_cropPreviewImage;       ///< 重裁途中的冻帧预览
        QPointF m_cropPreviewShapeOrigin; ///< 旧画布原点在预览里的位置，预览逻辑坐标
        QSizeF  m_cropPreviewLogicalSize; ///< 预览图对应的逻辑尺寸
        bool    m_hasCropPreview = false; ///< 是否正在显示预览

        /// 画屏幕边框与选择档抓点：正常出图与重裁预览共用同一笔
        void paintCaptureFrame(QPainter &painter);

        /**
         * @brief 重裁放大镜这一刻的几何
         * @details 像素插图、读数条与取样范围出自同一次布局：画与判据各算一遍就会「看着在框里、实际缺一块」。
         */
        struct CropMagnifierGeometry
        {
            QRect view;           ///< 像素插图，控件坐标；视野档为 off 或没有冻帧时为空
            QRect readout;        ///< 读数条，控件坐标
            QRect source;         ///< 取样范围，全局坐标
            int   sampleEdge = 0; ///< 取样边长，逻辑像素
            int   cellSize   = 1; ///< 一格在插图里的边长
        };

        /// 算重裁放大镜的几何；没在拖抓点时三个矩形都为空
        [[nodiscard]] CropMagnifierGeometry cropMagnifierGeometry() const;

        /// 读数条上的文字：当前范围尺寸与光标全局坐标
        [[nodiscard]] QString cropReadoutText() const;

        /// 画重裁放大镜与读数条
        void paintCropMagnifier(QPainter &painter);

        bool                m_captureResizing   = false;                     ///< 「选择」档正在拖窗口边框
        Core::SelectionGrip m_captureResizeGrip = Core::SelectionGrip::None; ///< 拖的是哪一条边或哪一个角
        QPoint              m_captureResizeStartGlobal;                      ///< 按下时的全局坐标
        QRect               m_captureResizeOriginalGeometry;                 ///< 按下前的窗口几何
        bool                m_captureMovePending = false;                    ///< 图内空白按住、还没超过手抖阈值
        bool                m_captureMoving      = false;                    ///< 正在整体挪动截图范围
        QPoint              m_captureMoveStartGlobal;                        ///< 整体挪动按下时的全局坐标
        QRect               m_captureMoveOriginalGeometry;                   ///< 整体挪动按下前的窗口几何
        QPoint              m_mousePointWidget;                              ///< 最近一次鼠标位置，控件坐标
        CropMagnifierSource m_cropMagnifierSource;                           ///< 重裁放大镜的整屏冻帧
        Core::MagnifierView m_magnifierView = Core::MagnifierView::Narrow;   ///< 这一轮的放大镜视野档
    };

} // namespace GyanisScribe::Gui
