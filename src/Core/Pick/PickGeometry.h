/**
 * @file PickGeometry.h
 * @brief 取色的那套像素几何：放大镜取样与摆放、屏幕坐标到抓帧像素的换算
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QPoint>
#include <QRect>

#include <vector>

namespace GyanisScribe::Core
{

    /// 放大镜默认取样的边长，逻辑像素：9x9 足够看清像素格，再多就把屏幕挡完了
    inline constexpr int kMagnifierSampleEdge = 9;

    /// 放大镜默认显示边长，逻辑像素
    inline constexpr int kMagnifierViewEdge = 180;

    /// 放大镜相对光标的默认偏移，逻辑像素：贴着光标会挡住正要看的像素
    inline constexpr int kMagnifierCursorOffset = 18;

    /**
     * @brief 放大镜的视野档
     * @details 显示边长固定 180，改的是取样格数：视野越窄每格放得越大，越宽看得越多但每格越小。
     *          「不显示」只收掉那枚像素插图，读数条仍在原位——坐标与颜色是放大镜另一件有用的东西，
     *          不想看格子的用户没理由连带失去它。
     */
    enum class MagnifierView
    {
        Off,    ///< 不显示放大镜插图
        Narrow, ///< 9×9：默认档，与本仓此前的形状同值
        Medium, ///< 15×15
        Wide,   ///< 21×21
    };

    /**
     * @brief 视野档对应的取样边长
     * @param view 哪一档
     * @return 逻辑像素边长；「不显示」给 0，几何那侧按「这枚放大镜不成立」处理
     */
    [[nodiscard]] int magnifierSampleEdgeFor(MagnifierView view);

    /**
     * @brief 视野档的存储标记
     * @param view 哪一档
     * @return 写进配置的短标记（off/narrow/medium/wide），区分大小写
     */
    [[nodiscard]] std::string_view magnifierViewToken(MagnifierView view);

    /**
     * @brief 视野档的界面名
     * @param view 哪一档
     * @return 下拉框里那四个字以内的短标签
     */
    [[nodiscard]] QString magnifierViewLabel(MagnifierView view);

    /**
     * @brief 全部视野档，按下拉框顺序
     * @return 与 magnifierViewToken 同序的列表
     */
    [[nodiscard]] std::vector<MagnifierView> magnifierViews();

    /**
     * @brief 把配置里存的标记读回视野档
     * @param token 存储标记，来自设置项
     * @param complaint 读不出时的中文原因，供调用方记日志；成功时清空
     * @return 可用的档；坏值回落「窄」（= 本仓此前的形状）
     * @details 与颜色写法那一档同一口径：两处都是「读坏了不让功能停摆，但要说清按了哪一档顶上」。
     */
    [[nodiscard]] MagnifierView magnifierViewOrDefault(std::string_view token, std::string &complaint);

    /// 最近取色色块的边长与间距，逻辑像素；距屏幕底边的留白与它同一档
    inline constexpr int kRecentSwatchEdge = 28;
    inline constexpr int kRecentSwatchGap  = 6;

    /**
     * @brief 放大镜该画在哪、取哪一块
     * @details 三个量互相牵制：取样范围决定每格多大，显示位置决定会不会挡住光标或跑出屏幕。
     */
    struct MagnifierLayout
    {
        QRect sourceRect;   ///< 取样范围，屏幕坐标，边长为给定取样边长
        QRect viewRect;     ///< 放大镜在屏幕上占的位置，屏幕坐标
        int   cellSize = 1; ///< 一个像素在放大镜里占多少逻辑像素
    };

    /**
     * @brief 算放大镜的取样范围与摆放
     * @param cursor 光标位置，屏幕坐标
     * @param screen 放大镜所在屏幕的几何，屏幕坐标
     * @param available 该屏可用区（去掉任务栏），用来判翻边
     * @param sampleEdge 取样边长，像素
     * @param viewEdge 显示边长，逻辑像素
     * @return 布局；sampleEdge 或 viewEdge 不成立时三个量都不可用
     * @details 取样范围夹在屏幕内而不是回绕：回绕会把屏幕另一头的像素摆到放大镜中间，
     *          用户照着抄的颜色是错的，而放大镜看起来完全正常。
     */
    [[nodiscard]] MagnifierLayout magnifierLayoutFor(const QPoint &cursor, const QRect &screen, const QRect &available, int sampleEdge, int viewEdge);

    /**
     * @brief 屏幕坐标换算到抓帧图像的像素坐标
     * @param globalPos 光标或矩形顶边的屏幕坐标
     * @param screenGeometry 这块屏幕的几何，屏幕坐标
     * @param devicePixelRatio 该屏的缩放比
     * @param imageSize 抓帧图像尺寸，像素
     * @return 图像内的像素坐标，越界夹到边缘
     * @details 高 DPI 下抓帧比逻辑坐标大：只减不乘会在 150% 屏上整体偏到左上角，
     *          取到的颜色是光标左上几像素的那块。
     */
    [[nodiscard]] QPoint framePixelFor(const QPoint &globalPos, const QRect &screenGeometry, qreal devicePixelRatio, const QSize &imageSize);

    /**
     * @brief 最近取色的色块行占的各个矩形
     * @param screen 这块屏的几何，屏幕坐标
     * @param count 要摆几个色块；0 个返回空表
     * @return 每个色块一个矩形，从左到右按最近在前排；屏幕太窄摆不下时按可摆数量截断
     * @details 摆在屏幕底边中间：不挡上方要核对的内容，又在一个不用找的位置。
     *          底色与描边由画的一侧定，这里只管几何——命中测试与绘制的矩形必须出自同一处，
     *          各自算一遍就会「看着点在块上、实际没点中」。
     */
    [[nodiscard]] std::vector<QRect> recentSwatchRects(const QRect &screen, int count);

} // namespace GyanisScribe::Core
