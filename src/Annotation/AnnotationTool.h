/**
 * @file AnnotationTool.h
 * @brief 标注工具基类：交互生命周期、图形集合、绘制与命中测试
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Annotation/AnnotationShape.h"

#include <QColor>
#include <QPoint>
#include <QRect>
#include <QVector>

#include <memory>
#include <optional>

class QImage;
class QPainter;

namespace GyanisScribe::Annotation
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
     *          以及被撤销栈调用的插入/取回/平移。八种工具之间互不认识，层叠顺序按种类登记顺序，
     *          这是一个有意的简化——真要做到「后画的压在先画的之上」，得把图形收进一张全局表，
     *          标注语义才允许跨类重排。
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
         * @param color 颜色
         * @param lineWidth 线宽，逻辑像素，必须为正
         * @param fontSize 字号，必须为正
         * @return 成功返回 true；索引越界返回 false
         * @details 三个字段一起写，与 begin() 给在途图形上色的口径一致：只改其一会让图形
         *          带着一个谁都没设过的混合样式，撤销时也说不清要回退到哪一份。位置与内容不动。
         */
        bool restyleShapeAt(int index, const QColor &color, int lineWidth, int fontSize);

        /**
         * @brief 命中测试
         * @param point 画布逻辑坐标
         * @return 命中的图形索引（后画的优先），没命中返回 -1
         */
        [[nodiscard]] int hitTest(const QPoint &point) const;

        /**
         * @brief 画到画布上：全部已落定图形，外加在途的那一条
         * @param painter 目标画家，已由调用方设好逻辑坐标
         * @param context 绘制现场
         */
        virtual void paint(QPainter &painter, const PaintContext &context) const;

        /**
         * @brief 烘焙进图像：只落定图形，在途的那条不算
         * @param image 目标图像（物理像素），就地修改
         * @param context 绘制现场
         */
        virtual void applyToImage(QImage &image, const PaintContext &context) const;

        /**
         * @brief 图形的外接框，供选中框、命中测试与移动判据
         * @param shape 图形
         * @return 逻辑坐标下的矩形
         * @details 基类把 rect、采样点与文本三种来源并起来算；锚点型工具（序号）的重写要返回
         *          它真正画出来的那一圈，否则选中框与图形错开半个身位。
         */
        [[nodiscard]] virtual QRect boundingBoxOf(const AnnotationShape &shape) const;

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
         * @param point 画布逻辑坐标
         * @param tolerance 允许的偏差，逻辑像素
         * @return true 命中
         * @details 默认按外接框判；轨迹类工具必须重写，否则一条绕圈的画笔在其空心区域里也会被选中。
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
        AnnotationShape m_pending;   ///< 在途图形
        QColor          m_color;     ///< 默认画笔颜色
        int             m_lineWidth; ///< 默认线宽
        int             m_fontSize;  ///< 默认字号

    private:
        /**
         * @brief 有没有在途图形
         * @return true 有内容等待落定，预览该把它画出来
         */
        [[nodiscard]] bool hasPending() const noexcept;

        ToolKind                 m_kind;             ///< 工具种类
        bool                     m_isActive = false; ///< 是否当前工具
        QVector<AnnotationShape> m_shapes;           ///< 已落定的图形，按登记顺序绘制
    };

} // namespace GyanisScribe::Annotation
