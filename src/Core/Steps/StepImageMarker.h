/**
 * @file StepImageMarker.h
 * @brief 在步骤截图上标出点击位置与序号
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QImage>
#include <QPoint>

namespace GyanisScribe::Core
{
    /**
     * @brief 在整屏截图上画点击圆环与步骤序号
     * @param screen 整屏截图，物理像素
     * @param logicalPointInScreen 点击位置，该屏内的逻辑坐标
     * @param devicePixelRatio 该屏缩放因子，物理像素 = 逻辑坐标乘以它；非正数按 1 处理
     * @param stepNumber 显示的步骤序号，从 1 开始
     * @return 画好标记的新图，尺寸与格式同入参；入参为空或点不在图内时返回入参副本，原图不动
     * @details 圆环半径 14 逻辑像素、线宽 3 逻辑像素，序号 18 逻辑像素粗体放在圆环右上方，
     *          三个尺寸都先乘 dpr 再落像素。画字要求 QGuiApplication 就位，且只能在图形界面线程调用；
     *          没有应用对象时只画圆环，不画序号。
     */
    [[nodiscard]] QImage annotateStepClick(const QImage &screen, const QPoint &logicalPointInScreen, qreal devicePixelRatio, int stepNumber);

} // namespace GyanisScribe::Core
