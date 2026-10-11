/**
 * @file PinTransform.h
 * @brief 贴图的缩放、旋转、透明度与手柄几何（纯计算，不依赖窗口）
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>

#include <optional>
#include <vector>

namespace GyanisScribe::Pin
{

    /// 缩放下限：再小就成一个点，恢复原大小时用户会找不到它
    constexpr double kMinimumScale = 0.05;

    /// 缩放上限：贴到 20 倍已经远超屏幕，再大只是把内存吃掉
    constexpr double kMaximumScale = 20.0;

    /// 透明度下限：完全不透明的穿透模式会看不见，留 5% 至少有个影子
    constexpr double kMinimumOpacity = 0.05;

    /**
     * @brief 缩放手柄的八个方位
     * @details 取值可能被写进布局文件，新成员只能追加在末尾。
     */
    enum class HandleKind
    {
        TopLeft,     ///< 左上
        Top,         ///< 上边中点
        TopRight,    ///< 右上
        Right,       ///< 右边中点
        BottomRight, ///< 右下
        Bottom,      ///< 下边中点
        BottomLeft,  ///< 左下
        Left,        ///< 左边中点
    };

    /**
     * @brief 把缩放因子夹进合法区间
     * @param scale 待夹的值，允许非正（视为太小）
     * @return 夹到 [kMinimumScale, kMaximumScale] 之间的值
     */
    [[nodiscard]] double clampScale(double scale) noexcept;

    /**
     * @brief 把透明度夹进合法区间
     * @param opacity 待夹的值
     * @return 夹到 [kMinimumOpacity, 1.0] 之间的值
     */
    [[nodiscard]] double clampOpacity(double opacity) noexcept;

    /**
     * @brief 按缩放因子算贴图的窗口尺寸
     * @param logicalSize 图片的逻辑尺寸
     * @param scale 缩放因子
     * @return 窗口尺寸，至少 1×1，避免零尺寸窗口画不出也点不到
     */
    [[nodiscard]] QSizeF windowSizeFor(const QSizeF &logicalSize, double scale) noexcept;

    /**
     * @brief 旋转后的外接框
     * @param center 旋转中心（窗口中心）
     * @param size 未旋转时的尺寸
     * @param degrees 旋转角度，顺时针
     * @return 旋转后的外接矩形（浮点），旋转 0° 时等价于以 center 为中心的 size 框
     */
    [[nodiscard]] QRectF rotatedBoundingBox(const QPointF &center, const QSizeF &size, double degrees);

    /**
     * @brief 八个手柄的占位框
     * @param target 贴图当前的矩形
     * @param handleSize 单个手柄的边长，逻辑像素
     * @return 按 HandleKind 顺序排好的八个框，中心落在 target 的角与边中点上
     */
    [[nodiscard]] std::vector<QRectF> handleRects(const QRectF &target, double handleSize);

    /**
     * @brief 按拖拽距离算新的缩放因子
     * @param initialScale 起拖时的缩放
     * @param initialDistance 起拖时指针到中心的距离
     * @param currentDistance 当前指针到中心的距离
     * @return 夹进合法区间的新缩放
     * @note 起拖距离为 0（在中心点上按下）时返回原缩放：没有比值可算，
     *       按 0 处理会把贴图瞬间缩成不可见，那比「拖不动」更糟
     */
    [[nodiscard]] double scaleFromDrag(double initialScale, double initialDistance, double currentDistance) noexcept;

    /**
     * @brief 把角度折进 [0, 360)
     * @param degrees 任意角度
     * @return 折算后的角度，供显示与布局存储
     */
    [[nodiscard]] double normalizeDegrees(double degrees) noexcept;

    /**
     * @brief 窗口坐标落在图片的哪一个像素上
     * @param windowPoint 窗口内的坐标
     * @param content 画布矩形（未旋转、已缩放），即 PinWindow::contentRect() 给的那个
     * @param imagePixelSize 图片的物理像素尺寸
     * @param degrees 旋转角度，顺时针，与绘制时用的是同一个
     * @return 命中的像素坐标（左上角为原点）；落在图外或几何退化时返回空
     * @details 与绘制正好相反的一步：先把点按 -degrees 绕画布中心转回来，再按画布矩形等比折进像素格。
     *          缩略图态不旋转，调用方按绘制时的口径传 0 度即可。
     */
    [[nodiscard]] std::optional<QPoint> imagePixelAt(const QPointF &windowPoint, const QRectF &content, const QSize &imagePixelSize, double degrees);

} // namespace GyanisScribe::Pin
