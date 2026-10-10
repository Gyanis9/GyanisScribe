/**
 * @file ScreenUtils.h
 * @brief 屏幕与选区的几何换算，以及选区文本的存取格式
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QPoint>
#include <QRect>
#include <QString>

class QScreen;

namespace GyanisScribe::Capture
{

    /**
     * @brief 把某块屏幕内的逻辑矩形换成同一块屏幕上的物理像素矩形
     * @param logicalRect 该屏幕坐标系下的逻辑矩形（Qt 的高 DPI 坐标）
     * @param devicePixelRatio 该屏的缩放因子，必须大于 0
     * @return 取整后的物理矩形；四边按「起点向下取整、终点向上取整」扩，保证用户框到的边不会被切掉半像素
     * @note 判据是「两套换算互指同一个点」：cropToLogicalRect() 用同一份取整规则，
     *       两边不一致时标注层与最终像素会错位半格。缩放因子非正一律拒，不悄悄按 1 处理。
     */
    [[nodiscard]] Core::Expected<QRect> toPhysicalRect(const QRect &logicalRect, qreal devicePixelRatio);

    /**
     * @brief 从一帧抓屏结果里裁出选区
     * @param source 抓到的整帧（物理像素）
     * @param logicalRegion source 所属屏幕内的逻辑矩形
     * @return 成功返回裁剪后的帧，其 logicalSize 与 devicePixelRatio 同步改好
     * @return 失败返回 ScreenUnavailable，文案指出选区超出哪一条边
     */
    [[nodiscard]] Core::Expected<Core::CapturedImage> cropToLogicalRect(const Core::CapturedImage &source, const QRect &logicalRegion);

    /**
     * @brief 按屏幕名取屏幕
     * @param screenName QScreen::name() 的那个名字
     * @return 找到返回该屏幕指针，所有权仍在 Qt 手里
     * @return 找不到返回空指针：多显示器被拔掉或名字写错时由调用方给出降级提示
     */
    [[nodiscard]] QScreen *screenByName(const QString &screenName);

    /**
     * @brief 取覆盖某个全局逻辑点的屏幕
     * @param globalPoint 全局逻辑坐标
     * @return 命中的屏幕指针；一点正落在两屏接缝上时取声明顺序里的那一块
     * @return 该点不在任何屏幕上（拔屏后的陈旧坐标）时返回空指针
     */
    [[nodiscard]] QScreen *screenAtGlobalPoint(const QPoint &globalPoint);


    /**
     * @brief 选区提示文本
     * @param logicalRegion 选区的逻辑矩形
     * @return 形如「1024 × 640    (128, 96)」的文本，字号小、只用于遮罩上的读数
     */
    [[nodiscard]] QString formatSelectionHint(const QRect &logicalRegion);

    /**
     * @brief 选区的存取文本形式
     * @param logicalRegion 选区
     * @return 「x,y,width,height」四个十进制数，供写进配置
     */
    [[nodiscard]] QString regionToText(const QRect &logicalRegion);

    /**
     * @brief 解析选区的存取文本
     * @param text 来自配置的文本
     * @return 成功返回选区
     * @return 失败返回 InvalidArgument，文案写明期望的形状与实际收到的内容
     */
    [[nodiscard]] Core::Expected<QRect> regionFromText(std::string_view text);

} // namespace GyanisScribe::Capture
