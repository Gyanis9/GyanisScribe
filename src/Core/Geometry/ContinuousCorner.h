/**
 * @file ContinuousCorner.h
 * @brief 连续曲率圆角的路径、掩膜与描边
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QBrush>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QSizeF>

namespace GyanisScribe::Core
{

    /**
     * @brief 一个圆角的几何参数
     * @details radius 是名义半径，smoothing 是 0–1 的平滑度；0 等价于普通圆角。
     */
    struct CornerShape
    {
        qreal radius    = 0.0; ///< 名义圆角半径，逻辑像素
        qreal smoothing = 0.0; ///< 0–1 平滑度，0 等价于普通圆角
    };

    /// 按尺寸分档的角色；具体数值由 cornerShapeFor 给出
    enum class CornerRole
    {
        Card,       ///< 阅读面：20px / 0.80
        Panel,      ///< 浮层与容器：16px / 0.70
        Control,    ///< 按钮与输入：12px / 0.50
        Compact,    ///< 紧凑控件与列表项：8px / 0.35
        Thumbnail,  ///< 缩略图与微调按钮悬停底：6px / 0.30
        Indicator,  ///< 勾选框指示器：5px / 0.25
        Hairline,   ///< 细槽与滚动条：4px / 0.20
        Handle,     ///< 正圆/胶囊：半尺寸 / 0.00
        ContentTiny ///< 色块与读数框：3px / 0.20
    };

    /// 供用例核对的 Figma 参数
    struct CornerPathParameters
    {
        qreal a                = 0.0; ///< 第一段贝塞尔的控制点步长
        qreal b                = 0.0; ///< a 的一半
        qreal c                = 0.0; ///< 圆弧后第一段控制点的横向分量
        qreal d                = 0.0; ///< 圆弧后第一段控制点的纵向分量
        qreal p                = 0.0; ///< 圆角在边上的总占位
        qreal radius           = 0.0; ///< 夹取后的半径
        qreal arcSectionLength = 0.0; ///< 圆弧在横纵轴上的投影长度
        qreal arcMeasure       = 0.0; ///< 圆弧角度，度
    };

    /**
     * @brief 按角色取名义半径与平滑度
     * @param role 尺寸分档角色
     * @return 对应形状；Handle 返回半径 0，调用方用带 size 的重载
     */
    [[nodiscard]] CornerShape cornerShapeFor(CornerRole role);

    /**
     * @brief 按角色与目标尺寸取形状
     * @param role 尺寸分档角色
     * @param size 目标矩形尺寸
     * @return Handle 返回短边一半且平滑度为 0，其余与 cornerShapeFor(role) 相同
     */
    [[nodiscard]] CornerShape cornerShapeFor(CornerRole role, const QSizeF &size);

    /**
     * @brief 内容层按半径取形状
     * @param radius 内容层半径，逻辑像素
     * @return 半径原样保留，平滑度按 ≤6/≤12/≤20/≤32/>32 分档
     */
    [[nodiscard]] CornerShape cornerShapeForRadius(qreal radius);

    /**
     * @brief 计算 Figma corner smoothing 的中间参数
     * @param size 目标矩形尺寸
     * @param shape 名义形状
     * @return 夹取后的参数；半径为 0 时全部为 0
     */
    [[nodiscard]] CornerPathParameters cornerPathParameters(const QSizeF &size, const CornerShape &shape);

    /**
     * @brief 生成连续曲率圆角路径
     * @param rect 目标矩形
     * @param shape 圆角形状
     * @return 顺时针闭合路径；平滑度为 0 时与 addRoundedRect 等价
     * @note 路径在 rect 内，不额外外扩；描边由 paintContinuousCorner 负责内缩
     */
    [[nodiscard]] QPainterPath continuousCornerPath(const QRectF &rect, const CornerShape &shape);

    /**
     * @brief 画一个连续曲率的填充与描边
     * @param painter 目标画家
     * @param rect 目标矩形
     * @param shape 圆角形状
     * @param fill 填充画刷；NoBrush 时只画描边
     * @param pen 描边画笔；NoPen 时只画填充
     * @note 描边路径按笔宽一半内缩，1px/2px 笔画不会落到 rect 外
     */
    void paintContinuousCorner(QPainter &painter, const QRectF &rect, const CornerShape &shape, const QBrush &fill, const QPen &pen);

    /**
     * @brief 把一张图裁成连续曲率圆角
     * @param image 目标图，必须是 Format_ARGB32_Premultiplied
     * @param shape 圆角形状
     * @note 用 DestinationIn 合成整幅掩膜，圆角外像素 alpha 归零
     */
    void applyCornerMask(QImage &image, const CornerShape &shape);

} // namespace GyanisScribe::Core
