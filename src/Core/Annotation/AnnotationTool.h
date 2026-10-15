/**
 * @file AnnotationTool.h
 * @brief 标注工具基类：交互生命周期、图形集合、绘制与命中测试
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Annotation/AnnotationShape.h"
#include "Core/Annotation/PenParameters.h"

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QVector>

#include <array>
#include <memory>
#include <optional>

class QImage;
class QPainter;

namespace GyanisScribe::Core
{

    /**
     * @brief 工具种类
     * @details 引擎按这个枚举寻址工具，界面不必持有具体子类指针。新工具只能追加在末尾：
     *          取值可能被写进配置与历史，中间插入会让既有编号整体位移。
     */
    enum class ToolKind
    {
        Rectangle, ///< 矩形
        Ellipse,   ///< 椭圆
        Arrow,     ///< 箭头
        Pen,       ///< 画笔
        Highlight, ///< 高亮（半透明粗笔）
        Mosaic,    ///< 马赛克
        Text,      ///< 文字
        Number,    ///< 序号
        Line,      ///< 直线（新增一档只能追加在末尾：工具栏登记表按 ToolKind 的顺序逐个核标签）
        Blur,      ///< 高斯模糊
        Eraser,    ///< 橡皮擦（沿笔迹擦回底图）
        Polyline,  ///< 折线（点一下落一个折点，双击收笔）
        Magnifier, ///< 放大镜（拖出源框，框里那块放大成旁边的插图）
    };

    /**
     * @brief 绘制现场
     * @details 马赛克要取源图在被盖那一块的像素，其余工具用不上这两个字段。
     * @note source 与 painter 画的必须是同一块画布：pixelScale 是「物理像素 ÷ 逻辑坐标」的比值，
     *       高 DPI 屏上不等于 1，标错就会让马赛克偏到别处去。
     */
    struct PaintContext
    {
        const QImage &source;           ///< 标注底图（物理像素）
        qreal         pixelScale = 1.0; ///< 物理像素相对逻辑坐标的倍数
    };

    /**
     * @brief 标注工具抽象基类
     * @details 一个工具管自己那一类的全部图形：交互（begin/update/end）、绘制、命中测试、
     *          以及被撤销栈调用的插入/取回/平移。各档工具之间互不认识；
     *          层叠顺序不在这里——每条图形带着引擎在落定时发的全局层叠号（AnnotationShape::zOrder），
     *          绘制与命中都由引擎按那个号排序后进行，后画的自然压在先画的上面，
     *          置顶/置底也只是换号。旋转同样在基类统一处理：paintShapeAt / hitTestShapeAt
     *          把画家与事件点先按图形自己的旋转角摆正，再走各工具那一条原判据。
     */
    class AnnotationTool
    {
    public:
        explicit AnnotationTool(ToolKind kind);
        virtual ~AnnotationTool() = default;

        AnnotationTool(const AnnotationTool &)            = delete;
        AnnotationTool &operator=(const AnnotationTool &) = delete;

        /**
         * @brief 工具种类
         * @return 构造时那个枚举值
         */
        [[nodiscard]] ToolKind kind() const noexcept;

        /**
         * @brief 是否当前工具
         * @return true 正在接收鼠标事件
         */
        [[nodiscard]] bool isActive() const noexcept;

        /**
         * @brief 设为当前工具
         * @param active 是否接收事件
         * @note 切换工具时若还有在途图形，会被丢弃：半途的拖拽不该留下一条谁都没看见的图形
         */
        void setActive(bool active) noexcept;

        /**
         * @brief 显示名，用于工具提示与无障碍名称
         * @return 中文名，如「矩形」
         */
        [[nodiscard]] virtual QString displayName() const = 0;

        /**
         * @brief 起笔
         * @param point 画布逻辑坐标
         */
        virtual void begin(const QPoint &point);

        /**
         * @brief 行进
         * @param point 当前逻辑坐标
         */
        virtual void update(const QPoint &point);

        /**
         * @brief 收笔：交回在途图形
         * @return 落定成功时返回那条图形；图形退化（零尺寸、空文本）时返回空
         * @details 基类只负责把在途图形交回，存进哪、能不能撤由引擎决定——撤销栈要拿到
         *          「插入前索引 + 图形内容」这对信息，工具自己插进去就丢掉了索引。
         */
        virtual std::optional<AnnotationShape> end();

        /**
         * @brief 丢弃在途图形
         */
        void cancel() noexcept;

        /**
         * @brief 已有图形条数
         * @return 条数
         */
        [[nodiscard]] int shapeCount() const noexcept;

        /**
         * @brief 取某一条图形
         * @param index 索引
         * @return 图形副本；索引越界时返回默认构造的图形，调用方应先用 shapeCount() 判过界
         */
        [[nodiscard]] AnnotationShape shapeAt(int index) const;

        /**
         * @brief 插入一条图形
         * @param index 插入位置，等于 shapeCount() 时追加到末尾
         * @param shape 图形内容
         * @note 撤销「删除」命令用的就是这个入口，索引语义与 takeShapeAt 成对
         */
        void insertShape(int index, const AnnotationShape &shape);

        /**
         * @brief 取走一条图形（删除）
         * @param index 索引
         * @return 被取走的图形；索引越界时返回默认构造的图形
         */
        [[nodiscard]] AnnotationShape takeShapeAt(int index);

        /**
         * @brief 平移一条图形
         * @param index 索引
         * @param delta 逻辑坐标位移
         * @return 成功返回 true；索引越界返回 false
         */
        bool translateShapeBy(int index, const QPoint &delta);

        /**
         * @brief 改一条图形自己的样式
         * @param index 索引
         * @param parameters 一整套样式参数
         * @return 成功返回 true；索引越界返回 false
         * @details 整包样式一起写，与 begin() 给在途图形上样的口径一致：只改其一会让图形
         *          带着一个谁都没设过的混合样式，撤销时也说不清要回退到哪一份。位置与内容不动。
         */
        bool restyleShapeAt(int index, const PenParameters &parameters);

        /**
         * @brief 整条替换一条图形
         * @param index 索引
         * @param shape 新的那一份（几何、样式、旋转、层叠号一起写）
         * @return 成功返回 true；索引越界返回 false
         * @details 缩放与旋转命令用的就是这一格：它们要按旧图形整体算出新图形，
         *          逐字段写会漏字段——漏掉的那个字段在撤销时就成了「回不去」。
         */
        bool replaceShapeAt(int index, const AnnotationShape &shape);

        /**
         * @brief 改一条图形的文字内容
         * @param index 索引
         * @param text 新的文字；空串也收下（撤销与重做要能回到改之前的那一份）
         * @return 成功返回 true；索引越界返回 false
         * @details 这一层不判「哪一档工具带文字」：序号图形的 text 是从 number 派生的，
         *          让它被随手改掉就等于把编号与显示分家——那道判断在引擎的「改选中文字」里。
         */
        bool setTextAt(int index, const QString &text);

        /**
         * @brief 手里还有在途图形吗
         * @return true 表示有一条还没落定的图形（多步工具在半途时会为真）
         * @details 引擎用它区分两种「收笔交回空」：多步工具还在等下一段，与真的没画成。
         */
        [[nodiscard]] bool hasPending() const noexcept;

        /**
         * @brief 收笔在即：多步工具（折线）在这里把「下一段」当成最后一段
         * @details 单步工具不重写它：它们松手即落定，收到这个请求也没有别的事要做。
         */
        virtual void requestFinish()
        {
        }
        /**
         * @brief 命中测试
         * @param point 画布逻辑坐标
         * @return 命中的图形索引（本工具内后画的优先），没命中返回 -1
         * @note 引擎按全局层叠号逐条问 hitTestShapeAt；这一格是给工具自己的用例与单工具场景留的整包入口
         */
        [[nodiscard]] int hitTest(const QPoint &point) const;

        /**
         * @brief 命中测试一条图形
         * @param index 索引
         * @param point 画布逻辑坐标
         * @return true 命中
         * @details 事件点先按图形自己的旋转角映回局部坐标，再走各工具的原始判据；索引越界返回 false。
         */
        [[nodiscard]] bool hitTestShapeAt(int index, const QPoint &point) const;

        /**
         * @brief 画一条已落定的图形
         * @param index 索引
         * @param painter 目标画家，已由调用方设好逻辑坐标
         * @param context 绘制现场
         * @details 旋转在这里统一处理：画家绕外接框中心转好，paintShape 照旧按未旋转的坐标画。
         */
        void paintShapeAt(int index, QPainter &painter, const PaintContext &context) const;

        /**
         * @brief 画在途图形
         * @param painter 目标画家，已由调用方设好逻辑坐标
         * @param context 绘制现场
         * @note 只有本工具还在用时才画：在途图形在任何一次切换里都会被丢掉
         */
        void paintPending(QPainter &painter, const PaintContext &context) const;

        /**
         * @brief 图形的外接框（含旋转），供选中框与移动判据
         * @param shape 图形
         * @return 逻辑坐标下的矩形；旋转过的图形返回包住它的轴对齐框
         * @details 转正的话是 localBoundingBoxOf 的结果，转过的话是它四个角转完后的轴对齐框。
         */
        [[nodiscard]] QRect boundingBoxOf(const AnnotationShape &shape) const;

        /**
         * @brief 图形的局部外接框（不含旋转）
         * @param shape 图形
         * @return 未旋转时的矩形
         * @details 基类把 rect、采样点与文本三种来源并起来算；锚点型工具（序号）的重写要返回
         *          它真正画出来的那一圈，否则选中框与图形错开半个身位。
         */
        [[nodiscard]] virtual QRect localBoundingBoxOf(const AnnotationShape &shape) const;

        /**
         * @brief 选中框的四个角点（含旋转）
         * @param shape 图形
         * @return 次序：左上、右上、右下、左下；坐标是画布逻辑坐标
         * @details 局部框的四角绕中心转过图形自己的旋转角，界面的手柄与旋转柄都摆在这四个点上。
         */
        [[nodiscard]] std::array<QPointF, 4> frameCornersOf(const AnnotationShape &shape) const;

        /**
         * @brief 设画笔默认颜色
         * @param color 颜色
         */
        void setPenColor(const QColor &color) noexcept;

        /**
         * @brief 设线宽
         * @param width 逻辑像素，必须为正
         */
        void setLineWidth(int width) noexcept;

        /**
         * @brief 设字号
         * @param size 磅值，必须为正
         */
        void setFontSize(int size) noexcept;


        /**
         * @brief 设填充档
         * @param style 0 无、1 半透明、2 实心；越界按最近的一档夹取
         * @details 只有矩形与椭圆用得上；档随在途图形的外接框一起画，选中的那条改档走改样式命令。
         */
        void setFillStyle(int style) noexcept;

        /**
         * @brief 当前填充档
         * @return 0、1 或 2
         */
        [[nodiscard]] int fillStyle() const noexcept;

        /**
         * @brief 开关圆角矩形
         * @param enabled true 表示四角按短边的比例切圆
         * @details 只有矩形工具用得上；开关随新画的图形带走，选中的那条改开关走改样式命令。
         */
        void setRoundedRect(bool enabled) noexcept;

        /**
         * @brief 设双向箭头
         * @param enabled true 表示两端各画一个箭头
         * @details 只有箭头工具用得上；开关随新画的图形带走，选中的那条改开关走改样式命令。
         */
        void setDoubleHeaded(bool enabled) noexcept;

        /**
         * @brief 设箭头方向
         * @param enabled true 表示箭头画在起点那一头（拖的方向与指的方向相反）
         * @details 只有箭头工具用得上；与双向同时为真时两端都有头，这一档就看不出来——
         *          两个开关各自独立记着，不做互斥回弹，因为用户先开哪个都不该被偷偷关掉。
         */
        void setArrowReversed(bool enabled) noexcept;

        /**
         * @brief 开关椭圆档
         * @param enabled true 表示圆的那一档：马赛克/高斯模糊把作用区收进内切椭圆，高亮/橡皮擦换圆头圆角笔尖
         * @details 四件工具（马赛克、高斯模糊、高亮、橡皮擦）用得上；开关随新画的图形带走，
         *          选中的那条改开关走改样式命令。
         */
        void setElliptical(bool enabled) noexcept;

        /**
         * @brief 设文字描边色档
         * @param step 0 自动（与字色反差）、1 白、2 黑；越界按最近的一档夹取
         * @details 只有文字工具用得上；档随新画的图形带走，选中的那条改档走改样式命令。
         */
        void setTextStrokeColorStep(int step) noexcept;

        /**
         * @brief 设文字描边粗细档
         * @param step 0 细、1 中、2 粗，取值表 kTextOutlineWidths；越界按最近的一档夹取
         */
        void setTextOutlineWidthStep(int step) noexcept;

        /**
         * @brief 设文字底板颜色档
         * @param step 0 自动（与字色反差）、1 白、2 黑、3 用这一形状自己的色；越界按最近的一档夹取
         */
        void setTextPlateColorStep(int step) noexcept;

        /**
         * @brief 设马赛克粒度
         * @param blockSize 方块边长，逻辑像素；越界按 1..64 夹取
         * @details 只有马赛克工具用得上；当前粒度随新画的图形带走，选中的那条改粒度走改样式命令。
         */
        void setMosaicBlockSize(int blockSize) noexcept;

        /**
         * @brief 设高亮透明度
         * @param alpha 0-255 的不透明度；越界按 0..255 夹取
         * @details 只有高亮工具用得上；当前透明度随新画的图形带走，选中的那条改透明度走改样式命令。
         */
        void setHighlightAlpha(int alpha) noexcept;

        /**
         * @brief 设线型档
         * @param style 0 实线、1 虚线、2 点线；越界按最近的一档夹取
         * @details 描边类工具（矩形、椭圆、直线、箭头、画笔、折线）用得上；档随新画的图形带走，
         *          选中的那条改线型走改样式命令。
         */
        void setLineStyle(int style) noexcept;

        /// 设默认高亮加宽倍数（仅高亮工具用），取值夹在 1 到 16 之间
        void setHighlightWidthFactor(int factor) noexcept;

        /**
         * @brief 设箭头头部款式
         * @param style 0 实心、1 空心、2 开口两翼；越界按最近的一档夹取
         * @details 只有箭头工具用得上；款式随新画的箭头带走，选中的那条改款式走改样式命令。
         */
        void setArrowHeadStyle(int style) noexcept;

        /**
         * @brief 设箭头头部大小
         * @param scale 头长相对线宽的倍数，取值夹在 1 到 10 之间
         * @details 只有箭头工具用得上；大小随新画的箭头带走，选中的那条改大小走改样式命令。
         */
        void setArrowHeadScale(qreal scale) noexcept;

        /**
         * @brief 设文字字体族
         * @param family 家族名；空串表示跟随应用默认字体
         * @details 只有文字工具用得上；画字与量字共用 TextFont.h 那一处拼装，选中框不会与字形错开。
         */
        void setFontFamily(const QString &family);

        /**
         * @brief 设文字粗体
         * @param enabled true 表示粗体
         * @details 只有文字工具用得上；与斜体、字体族一样随新画的图形带走。
         */
        void setTextBold(bool enabled) noexcept;

        /**
         * @brief 设文字斜体
         * @param enabled true 表示斜体
         * @details 只有文字工具用得上。
         */
        void setTextItalic(bool enabled) noexcept;

        /**
         * @brief 设文字反色描边
         * @param enabled true 表示字形围一圈与字色反差的轮廓
         * @details 只有文字工具用得上；与底板一样随新画的图形带走，选中的那条走改样式命令。
         */
        void setTextOutline(bool enabled) noexcept;

        /**
         * @brief 设文字反色底板
         * @param enabled true 表示字形后面垫一块与字色反差的半透明圆角板
         * @details 只有文字工具用得上。
         */
        void setTextBackground(bool enabled) noexcept;

        /**
         * @brief 当前默认线宽
         * @return 逻辑像素
         */
        [[nodiscard]] int lineWidth() const noexcept;

        /**
         * @brief 当前默认字号
         * @return 磅值
         */
        [[nodiscard]] int fontSize() const noexcept;

    protected:
        /**
         * @brief 画一条图形——每个工具唯一必须自己实现的东西
         * @param painter 目标画家
         * @param shape 图形
         * @param context 绘制现场，只有马赛克用得上
         */
        virtual void paintShape(QPainter &painter, const AnnotationShape &shape, const PaintContext &context) const = 0;

        /**
         * @brief 命中判据
         * @param shape 待判图形
         * @param point 局部坐标下的点（旋转过的图形在进来之前已由基类映回）
         * @param tolerance 允许的偏差，逻辑像素
         * @return true 命中
         * @details 默认按局部外接框判；轨迹类工具必须重写，否则一条绕圈的画笔在其空心区域里也会被选中。
         */
        virtual bool hitTestShape(const AnnotationShape &shape, const QPoint &point, int tolerance) const;

        /**
         * @brief 落定前的可用性判据
         * @param shape 在途图形
         * @return true 可以落定
         */
        virtual bool shapeIsUsable(const AnnotationShape &shape) const;

        /// 起笔点，拖拽类图形用它算外接框
        QPoint          m_startPoint;
        AnnotationShape m_pending;                                                                   ///< 在途图形
        QColor          m_color;                                                                     ///< 默认画笔颜色
        int             m_lineWidth;                                                                 ///< 默认线宽
        int             m_fillStyle            = 0;                                                  ///< 默认填充档（0 无、1 半透明、2 实心）
        bool            m_roundedRect          = false;                                              ///< 默认圆角矩形（仅矩形工具用）
        bool            m_doubleHeaded         = false;                                              ///< 默认双向箭头（仅箭头工具用）
        bool            m_arrowReversed        = false;                                              ///< 默认箭头在起点那一头（仅箭头工具用）
        bool            m_elliptical           = false;                                              ///< 默认椭圆档（马赛克/高斯模糊/高亮/橡皮擦用）
        int             m_mosaicBlockSize      = kMosaicBlockSizes[kDefaultMosaicBlockStep];         ///< 默认马赛克粒度（仅马赛克工具用）
        int             m_highlightAlpha       = kHighlightAlphas[kDefaultHighlightAlphaStep];       ///< 默认高亮透明度（仅高亮工具用）
        int             m_lineStyle            = static_cast<int>(kDefaultLineStyleStep);            ///< 默认线型档（描边类工具用）
        int             m_highlightWidthFactor = kHighlightWidthFactors[kDefaultHighlightWidthStep]; ///< 默认高亮加宽倍数（仅高亮工具用）
        int             m_arrowHeadStyle       = kArrowHeadFilled;                                   ///< 默认箭头头部款式（仅箭头工具用）
        qreal           m_arrowHeadScale       = kArrowHeadScales[kDefaultArrowHeadScaleStep];       ///< 默认箭头头部大小：头长相对线宽的倍数（仅箭头工具用）
        QString         m_fontFamily;                                                                ///< 默认文字字体族，空串=跟随应用默认（仅文字工具用）
        bool            m_textBold             = false;                                              ///< 默认文字粗体（仅文字工具用）
        bool            m_textItalic           = false;                                              ///< 默认文字斜体（仅文字工具用）
        bool            m_textOutline          = false;                                              ///< 默认反色描边（仅文字工具用）
        bool            m_textBackground       = false;                                              ///< 默认反色底板（仅文字工具用）
        int             m_textStrokeColorStep  = static_cast<int>(kDefaultTextStrokeColorStep);      ///< 默认文字描边色档（仅文字工具用）
        int             m_textOutlineWidthStep = static_cast<int>(kDefaultTextOutlineWidthStep);     ///< 默认文字描边粗细档（仅文字工具用）
        int             m_textPlateColorStep   = static_cast<int>(kDefaultTextPlateColorStep);       ///< 默认文字底板色档（仅文字工具用）
        int             m_fontSize;                                                                  ///< 默认字号

    private:
        ToolKind                 m_kind;             ///< 工具种类
        bool                     m_isActive = false; ///< 是否当前工具
        QVector<AnnotationShape> m_shapes;           ///< 已落定的图形，按登记顺序绘制
    };

} // namespace GyanisScribe::Core
