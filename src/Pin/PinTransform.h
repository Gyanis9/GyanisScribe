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

    /// 缩略图方块的默认边长，逻辑像素
    constexpr double kDefaultThumbnailEdge = 48.0;

    /// 缩略图方块的可拖下限：再小就只剩一枚图标的大小，看不出上面是什么
    constexpr double kMinimumThumbnailEdge = 24.0;

    /// 缩略图方块的可拖上限：方块的用途是「先让开地方」，大到盖住半屏就不是让地方了
    constexpr double kMaximumThumbnailEdge = 240.0;

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
     * @brief 把缩略图方块的边长夹进合法区间
     * @param edge 待夹的边长，允许非正（视为太小）
     * @return 夹到 [kMinimumThumbnailEdge, kMaximumThumbnailEdge] 之间的值
     */
    [[nodiscard]] double clampThumbnailEdge(double edge) noexcept;

    /**
     * @brief 按拖拽距离算缩略图方块的新边长
     * @param initialEdge 起拖时的边长
     * @param initialDistance 起拖时指针到中心的距离
     * @param currentDistance 当前指针到中心的距离
     * @return 夹进合法区间的新边长
     * @details 与 scaleFromDrag 同一条距离口径：方块与整张图共用「拖边按到中心的距离给倍率」这一只手感，
     *          只是落到的量不一样——方块改的是那枚方块的边长，不碰用户调好的缩放。
     */
    [[nodiscard]] double thumbnailEdgeFromDrag(double initialEdge, double initialDistance, double currentDistance) noexcept;

    /**
     * @brief 把角度折进 [0, 360)
     * @param degrees 任意角度
     * @return 折算后的角度，供显示与布局存储
     */
    [[nodiscard]] double normalizeDegrees(double degrees) noexcept;

    /**
     * @brief 把窗口坐标落在图片的哪一个像素上
     * @param windowPoint 窗口内的坐标
     * @param content 画布矩形（未旋转、已缩放），即 PinWindow::contentRect() 给的那个
     * @param imagePixelSize 图片的物理像素尺寸
     * @param degrees 旋转角度，顺时针，与绘制时用的是同一个
     * @param mirroredHorizontally 左右翻转，与绘制时用的是同一个
     * @param mirroredVertically 上下翻转，与绘制时用的是同一个
     * @return 命中的像素坐标（左上角为原点）；落在图外或几何退化时返回空
     * @details 与绘制正好相反的一步：先把点按 -degrees 绕画布中心转回来、再按同一中心翻回来，
     *          最后按画布矩形等比折进像素格。绘制那边是「先翻转、后旋转」（转的是翻好的那一份），
     *          所以反变换的顺序是「先反旋转、后反翻转」——两处必须成对改。
     *          缩略图态不旋转，调用方按绘制时的口径传 0 度即可（翻转不改外框，照常传）。
     */
    [[nodiscard]] std::optional<QPoint> imagePixelAt(const QPointF &windowPoint, const QRectF &content, const QSize &imagePixelSize, double degrees,
                                                     bool mirroredHorizontally = false, bool mirroredVertically = false);

    /**
     * @brief 把窗口坐标里的一个矩形折成图片像素的裁剪框
     * @param windowRect 窗口里的矩形（轴对齐：调用方保证此刻没有旋转与翻转）
     * @param content 画布矩形
     * @param imagePixelSize 图片的物理像素尺寸
     * @return 覆盖到的像素范围（闭区间取整）；几何退化或框完全落在画布外时返回空
     * @details 与 imagePixelAt 是同一套折算的「按块」版本：点命中是下取整，块换算要左闭右开——
     *          直接拿两个角去问点命中，右边界那一格会被多算一列（框拖到 20..30 会裁出 21 像素宽）。
     *          框先夹回画布：拖出画布的部分不参与，贴边的那一列/行照样算进去。
     */
    [[nodiscard]] std::optional<QRect> imagePixelRect(const QRectF &windowRect, const QRectF &content, const QSize &imagePixelSize);

    /// 拖到离屏幕边多近就算「要贴上去」，逻辑像素
    inline constexpr int kEdgeSnapTolerance = 12;

    /// 贴边收起后留在屏上的那条边，逻辑像素：全藏进去用户就再也找不回它了
    inline constexpr int kEdgeStripWidth = 6;

    /**
     * @brief 贴的是屏的哪条边
     * @details 取值可能被写进布局与菜单，新成员只能追加在末尾。
     */
    enum class ScreenEdge
    {
        None,   ///< 没贴边
        Left,   ///< 左边
        Right,  ///< 右边
        Top,    ///< 上边
        Bottom, ///< 下边
    };

    /**
     * @brief 贴边换算的结果
     */
    struct EdgeSnap
    {
        QRect      rect;                    ///< 夹进可用区并吸好边的窗口矩形
        ScreenEdge edge = ScreenEdge::None; ///< 贴上的那条边
    };

    /**
     * @brief 把拖到的窗口矩形吸进屏幕：越界的先夹回来，挨着哪条边就贴哪条
     * @param draggedRect 拖到的位置（窗口矩形）
     * @param available 这块屏的可用区（不含任务栏）
     * @param tolerance 判「够近」的容差，逻辑像素
     * @return 吸好之后的矩形与贴的是哪条边
     * @details 夹进可用区不是顺手加的：夹取之前可以把一张贴图整个拖出屏幕，从此再也点不到它。
     *          两条边同时够近（小窗口拖到角上）时取左/上，与录屏控制条那边同一口径。
     */
    [[nodiscard]] EdgeSnap snapToScreenEdges(const QRect &draggedRect, const QRect &available, int tolerance = kEdgeSnapTolerance);

    /**
     * @brief 把拖动的矩形吸向候选矩形的边线（每轴最多吸一条）
     * @param draggedRect 拖到的位置（窗口矩形）
     * @param candidates 参照矩形：桌上其他贴图与屏上别的程序窗口
     * @param tolerance 判「够近」的容差，逻辑像素
     * @return 吸好的矩形；两轴都没有够近的线时原样返回
     * @details 一条轴上取「位移最小的那一条线」，六条线一起比（左/中/右 与 上/中/下）：
     *          只比外边线时，窄条贴图对宽贴图永远差半格，而那正是「两张图并排摆齐」最常要做的事。
     */
    [[nodiscard]] QRect snapToCandidateEdges(const QRect &draggedRect, const std::vector<QRect> &candidates, int tolerance = kEdgeSnapTolerance);

    /**
     * @brief 贴边之后的窗口矩形：露着还是收成一条
     * @param revealed 贴边后完全露出的矩形
     * @param available 那块屏的可用区
     * @param edge 贴的哪条边；None 时原样返回 revealed
     * @param collapsed true 表示收成一条，false 表示全露出
     * @param stripWidth 收起来时留在屏上的那条边的宽度
     * @return 贴边后的窗口矩形（收起时大部分在屏外）
     */
    [[nodiscard]] QRect dockedRect(const QRect &revealed, const QRect &available, ScreenEdge edge, bool collapsed, int stripWidth = kEdgeStripWidth);

} // namespace GyanisScribe::Pin
