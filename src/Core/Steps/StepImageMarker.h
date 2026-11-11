/**
 * @file StepImageMarker.h
 * @brief 在步骤截图上标出点击位置与序号
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Steps/StepTypes.h"

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
     *          三个尺寸都先乘 dpr 再落像素。序号圆牌贴到上边或右边时整块夹回画面内，
     *          圆环本身留在真实点击点上，允许被边缘裁掉一部分。画字要求 QGuiApplication 就位，
     *          且只能在图形界面线程调用；没有应用对象时只画圆环，不画序号。
     */
    [[nodiscard]] QImage annotateStepClick(const QImage &screen, const QPoint &logicalPointInScreen, qreal devicePixelRatio, int stepNumber);

    /**
     * @brief 按显示顺序读出一张步骤截图，点击步骤顺手画上标记
     * @param step 要导出的一步
     * @param displayNumber 圆牌上的最终序号，从 1 开始；不是 RecordedStep::index
     * @return 点击步骤且点击点有效时为画好圆环与序号的新图，其它来源返回读出来的原图；
     *         路径为空或读不出来时返回空图，由调用方决定占位
     * @details 磁盘上存的是未标记的原图，标记在这里按最终序号现画，删除或调序后序号自动跟上。
     *          点击点是否有效按横纵坐标都不为负判断，落在图外的情况由 annotateStepClick 兜住。
     */
    [[nodiscard]] QImage annotatedStepImage(const RecordedStep &step, int displayNumber);

} // namespace GyanisScribe::Core
