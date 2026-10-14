/**
 * @file AnnotationShape.h
 * @brief 一条标注图形的数据记录，全部工具共用同一份形状
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QColor>
#include <Qt>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

#include <array>
#include <cstddef>

namespace GyanisScribe::Annotation
{

    /// 马赛克粒度阶梯：方块边长，逻辑像素；越大块越粗
    inline constexpr std::array<int, 3> kMosaicBlockSizes{8, 12, 20};
    /// 高亮透明度阶梯：0-255 的不透明度；越大越实
    inline constexpr std::array<int, 3> kHighlightAlphas{60, 110, 170};
    /// 高亮加宽阶梯：高亮的实际粗细 = 线宽乘这个倍数；一支笔要能盖住一行字，倍数是这个差别的全部来源
    inline constexpr std::array<int, 3> kHighlightWidthFactors{2, 4, 8};
    /// 箭头头部大小阶梯：头长 = 线宽乘这个倍数；同一条杆上头能有多大，全看这一档
    inline constexpr std::array<qreal, 3> kArrowHeadScales{2.5, 3.5, 5.0};

    /// 线型阶梯：实线 / 虚线 / 点线（描边类工具用；高亮、马赛克、模糊、橡皮擦这些效果工具不走这一档）
    inline constexpr std::array<Qt::PenStyle, 3> kAnnotationLineStyles{Qt::SolidLine, Qt::DashLine, Qt::DotLine};
    /// 线型起点档在阶梯里的下标（「实线」，与这一格出现之前同解）
    inline constexpr std::size_t kDefaultLineStyleStep = 0;

    /// 箭头头部款式：0 实心三角、1 空心三角（只有一圈描边，里头透底）、2 开口两翼（只有两条边线，无底边）
    inline constexpr int kArrowHeadFilled     = 0;
    inline constexpr int kArrowHeadHollow     = 1;
    inline constexpr int kArrowHeadOpenWings  = 2;
    inline constexpr int kArrowHeadStyleCount = 3;

    /// 文字描边色档：0 自动（取与字色反差的一色）、1 白、2 黑
    inline constexpr int kTextStrokeColorAutomatic = 0;
    inline constexpr int kTextStrokeColorWhite     = 1;
    inline constexpr int kTextStrokeColorBlack     = 2;
    inline constexpr int kTextStrokeColorCount     = 3;

    /// 文字描边色起点档（「自动」，与这一格出现之前同解）
    inline constexpr std::size_t kDefaultTextStrokeColorStep = 0;

    /// 文字描边粗细阶梯：描边露出来的那一半 = 字号乘这个倍数；中档与这一格出现之前的写死值同解
    inline constexpr std::array<double, 3> kTextOutlineWidths{0.06, 0.125, 0.2};
    /// 文字描边粗细起点档在阶梯里的下标（「中」=0.125，老图样不受影响）
    inline constexpr std::size_t kDefaultTextOutlineWidthStep = 1;

    /// 文字底板色档：0 自动（与字色反差）、1 白、2 黑、3 用这一形状自己的色（此时字色换成反差色，否则字压在同色底上看不见）
    inline constexpr int kTextPlateColorAutomatic = 0;
    inline constexpr int kTextPlateColorWhite     = 1;
    inline constexpr int kTextPlateColorBlack     = 2;
    inline constexpr int kTextPlateColorFromShape = 3;
    inline constexpr int kTextPlateColorCount     = 4;
    /// 文字底板色起点档（「自动」，与这一格出现之前同解）
    inline constexpr std::size_t kDefaultTextPlateColorStep = 0;

    /// 计数组数量：序号可以开几组各自连排的序列（第 1..3 组，工具栏按这个数循环）
    inline constexpr int kNumberGroupCount = 3;

    /// 马赛克粒度起点档在阶梯里的下标（「中」）
    inline constexpr std::size_t kDefaultMosaicBlockStep = 1;
    /// 高亮透明度起点档在阶梯里的下标（「中」）
    inline constexpr std::size_t kDefaultHighlightAlphaStep = 1;
    /// 高亮加宽起点档在阶梯里的下标（「中」=4，与这一格出现之前的写死值同解，老图样不受影响）
    inline constexpr std::size_t kDefaultHighlightWidthStep = 1;
    /// 箭头头部大小起点档在阶梯里的下标（「中」=3.5，与这一格出现之前的写死值同解，老图样不受影响）
    inline constexpr std::size_t kDefaultArrowHeadScaleStep = 1;

    /**
     * @brief 一条图形
     * @details 各档工具共用这一个记录而不是各写一份结构：它们的差别在「怎么画」，不在「记什么」。
     *          各工具只取自己用得上的字段，用不到的字段留默认值——再加一层每工具一份的形状类型
     *          只会让命中测试、移动与序列化各写八遍。
     * @note 坐标一律是画布的逻辑坐标（与遮罩、选区同一坐标系），换算只在绘制与导出那两处发生。
     */
    struct AnnotationShape
    {
        QRect           rect;                                                           ///< 拖拽类图形的外接矩形（矩形、椭圆、马赛克）
        QVector<QPoint> points;                                                         ///< 轨迹类图形的采样点（画笔、高亮、箭头首尾两点）
        QPoint          position;                                                       ///< 锚点（文字、序号）
        QString         text;                                                           ///< 文字内容；序号工具用它存编号文本
        QColor          color                = QColor(255, 0, 0);                       ///< 画笔颜色，默认标注红
        int             lineWidth            = 3;                                       ///< 线宽，逻辑像素
        int             fontSize             = 16;                                      ///< 字号，仅文字与序号使用
        int             number               = 1;                                       ///< 序号值，仅序号工具使用
        int             numberGroup          = 1;                                       ///< 所属计数组（1..kNumberGroupCount），各组各自连排（仅序号工具使用）
        int             fillStyle            = 0;                                       ///< 填充档：0 无、1 半透明、2 实心（仅矩形与椭圆用）
        int             lineStyle            = static_cast<int>(kDefaultLineStyleStep); ///< 线型档：0 实线、1 虚线、2 点线（描边类工具用）
        bool            roundedRect          = false;                                   ///< 圆角矩形：四角按短边的比例切圆（仅矩形工具用）
        bool            doubleHeaded         = false;                                   ///< 双向箭头：两端各画一个（仅箭头工具用）
        bool            arrowReversed        = false; ///< 箭头换到起点那一头（仅箭头工具用）；与 doubleHeaded 同时为真时两端都有头，这一档就看不出来
        bool            elliptical           = false; ///< 椭圆档（马赛克/高斯模糊/高亮/橡皮擦用）：区域类收进内切椭圆，笔迹类换圆头圆角笔尖
        int             mosaicBlockSize      = kMosaicBlockSizes[kDefaultMosaicBlockStep];         ///< 马赛克方块边长，逻辑像素（仅马赛克工具用），默认为阶梯里的「中」档
        int             highlightAlpha       = kHighlightAlphas[kDefaultHighlightAlphaStep];       ///< 高亮透明度，0-255（仅高亮工具用），默认为阶梯里的「中」档
        int             highlightWidthFactor = kHighlightWidthFactors[kDefaultHighlightWidthStep]; ///< 高亮相对线宽的加宽倍数（仅高亮工具用），默认为阶梯里的「中」档
        int             arrowHeadStyle       = kArrowHeadFilled;                                   ///< 箭头头部款式：0 实心、1 空心、2 开口两翼（仅箭头工具用）
        qreal           arrowHeadScale       = kArrowHeadScales[kDefaultArrowHeadScaleStep];       ///< 箭头头部大小：头长相对线宽的倍数（仅箭头工具用），默认为阶梯里的「中」档
        QString         fontFamily;                                                                ///< 字体族，空串=跟随应用默认字体（仅文字工具用）
        bool            textBold             = false;                                              ///< 粗体（仅文字工具用）
        bool            textItalic           = false;                                              ///< 斜体（仅文字工具用）
        bool            textOutline          = false;                                              ///< 反色描边：字形围一圈与字色反差的轮廓（仅文字工具用）
        bool            textBackground       = false;                                              ///< 反色底板：字形后面垫一块半透明的反差圆角板（仅文字工具用）
        int             textStrokeColorStep  = static_cast<int>(kDefaultTextStrokeColorStep);      ///< 描边色档：0 自动（反差色）、1 白、2 黑（仅文字工具用）
        int             textOutlineWidthStep = static_cast<int>(kDefaultTextOutlineWidthStep);     ///< 描边粗细档：0 细、1 中、2 粗，取值表 kTextOutlineWidths（仅文字工具用）
        int             textPlateColorStep   = static_cast<int>(kDefaultTextPlateColorStep);       ///< 底板色档：0 自动反差、1 白、2 黑、3 用形状自己的色（仅文字工具用）
        qreal           rotationDeg          = 0.0;                                                ///< 旋转角，度，顺时针为正；绕图形自己的外接框中心转（画与命中测试同解）
        int             zOrder               = 0;                                                  ///< 全局层叠序号：越大越靠上，由引擎落定时发号，不用手工维护
    };

    /**
     * @brief 平移一条图形的几何
     * @param shape 就地修改的图形
     * @param delta 逻辑坐标位移
     * @details 矩形、采样点与锚点各走一步。工具的 translateShapeBy 与粘贴副本共用这一处：
     *          各写一份的话，漏掉某个几何字段就是「拖动会动、粘贴不动」这种半身不遂。
     */
    inline void translateShape(AnnotationShape &shape, const QPoint &delta)
    {
        shape.rect.translate(delta);
        shape.position += delta;
        for (auto &point: shape.points)
        {
            point += delta;
        }
    }

} // namespace GyanisScribe::Annotation
