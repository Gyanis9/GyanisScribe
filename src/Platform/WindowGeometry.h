/**
 * @file WindowGeometry.h
 * @brief 窗口与显示器的物理↔逻辑全局坐标换算（纯计算，不碰平台 API）
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QRect>
#include <QString>

#include <vector>

namespace GyanisScribe::Platform
{

    /**
     * @brief 一块显示器的两份读数与它的缩放因子
     * @details 物理那一份来自平台 API（Windows 的 MONITORINFO），逻辑那一份来自 Qt（QScreen::geometry()）。
     *          两边必须说的是同一块屏：配对按显示器名做，名字口径不一致时宁可配不上也不要错配，
     *          错配会把窗口画到别的屏上。
     */
    struct MonitorMapping
    {
        QString name;                   ///< 显示器名，Windows 的 szDevice 与 QScreen::name() 同一个口径
        QRect   physicalGlobal;         ///< 物理像素下的全局矩形
        QRect   logicalGlobal;          ///< Qt 逻辑坐标下的全局矩形
        qreal   devicePixelRatio = 1.0; ///< 该屏缩放因子，物理长度 = 逻辑长度乘以它
    };

    /**
     * @brief 屏上一个可见窗口的快照，坐标已换成 Qt 的逻辑全局读数
     * @details 只给几何与标识，不给「该不该截」的判断：候选筛选与命中测试在用它的界面层做。
     * @note 整窗与客户区要分开给：抓回来的那一帧是客户区的像素（本机实测：整窗 422×292，
     *       客户区 420×260，差的是标题栏与边框），拿整窗的左上角当帧原点会把光标高亮偏出去三十来格。
     */
    struct ScreenWindow
    {
        quintptr handle = 0;          ///< 平台窗口句柄，与 Qt 的 WId 同一口径
        QString  title;               ///< 标题栏文字，没有标题的窗口是空串
        QString  processName;         ///< 进程的可执行文件名（不含路径）；平台取不到时是空串——排除清单按它与进程对号
        QRect    logicalGlobal;       ///< 窗口在 Qt 逻辑全局坐标下的矩形（含标题栏与边框）
        QRect    clientLogicalGlobal; ///< 客户区的逻辑全局矩形；平台取不到客户区读数时是空矩形，消费方不许拿整窗矩形顶替
    };

    /**
     * @brief 把全局物理矩形换成同一块屏上的全局逻辑矩形
     * @param physicalGlobal 平台读数给出的物理矩形
     * @param monitor 这块屏的两份读数；矩形所属的屏由调用方按窗口中心选定
     * @return 成功返回逻辑矩形；缩放因子非正时返回 InvalidArgument
     * @details 先减掉这块屏的物理原点，按缩放因子换算，再加回它的逻辑原点：全程只用这一块屏的读数，
     *          所以多显示器与每显示器缩放（Per-Monitor）都走同一条公式。
     *          取整沿用 ScreenUtils::toPhysicalRect 那一条规则（起点向下、终点向上），两边互指同一个点。
     */
    [[nodiscard]] Base::Expected<QRect> physicalToLogicalGlobal(const QRect &physicalGlobal, const MonitorMapping &monitor);

    /**
     * @brief physicalToLogicalGlobal() 的反方向：逻辑全局矩形换成物理全局矩形
     * @param logicalGlobal 逻辑矩形
     * @param monitor 这块屏的两份读数
     * @return 成功返回物理矩形；缩放因子非正时返回 InvalidArgument
     * @note 两条换算必须成对改：只改一边就会得到「高亮框与像素不一致」这类静默错位
     */
    [[nodiscard]] Base::Expected<QRect> logicalToPhysicalGlobal(const QRect &logicalGlobal, const MonitorMapping &monitor);

    /**
     * @brief 找一个物理点落在哪块屏上
     * @param physicalPoint 平台读数给出的全局物理点
     * @param monitors 各块屏的读数
     * @return 命中的下标；一块都不含这个点时返回 monitors.size()（拔屏后的陈旧读数）
     * @details 跨屏窗口的中心只会落在一块屏上，落点由这条选屏后再做换算；接缝上的点按声明顺序取第一块，
     *          与 Capture::screenAtGlobalPoint() 同一条规则，两处不一致会让同一个窗口两次算出不同位置。
     */
    [[nodiscard]] std::size_t monitorIndexForPoint(const QPoint &physicalPoint, const std::vector<MonitorMapping> &monitors);

    /**
     * @brief 把全局逻辑点换成同一块屏上的全局物理点
     * @param logicalPoint 该屏坐标系里的全局逻辑点
     * @param monitor 这块屏的两份读数
     * @return 成功返回物理点；缩放因子非正时返回 InvalidArgument
     * @details 点按「矩形的起点」处理，取整沿用 logicalToPhysicalGlobal() 那一条（起点向下）：
     *          平台侧按物理点做命中（窗口矩形包含、UIA 命中），取整若与矩形换算不同源，
     *          边界上的那一格会两边各说各话。
     */
    [[nodiscard]] Base::Expected<QPoint> logicalPointToPhysicalGlobal(const QPoint &logicalPoint, const MonitorMapping &monitor);

} // namespace GyanisScribe::Platform
