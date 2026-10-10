/**
 * @file KeyStep.h
 * @brief 键盘到「一格位移」的翻译：方向键对应的逻辑像素步长
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QPoint>

#include <optional>

namespace GyanisScribe::Ui
{

    /**
     * @brief 方向键对应的那一格位移
     * @param key 按键码（Qt::Key_*），不带修饰键
     * @return 左/右/上/下分别是 (-1,0)、(1,0)、(0,-1)、(0,1)；别的键返回空
     * @details 一格固定是一个逻辑像素：界面要的是「按一次看得见动一下」，步长再大就成了跳跃而不是微调。
     *          坐标系由调用方定（遮罩用该屏内的局部坐标，标注画布用画布坐标），这里只管方向与大小。
     */
    [[nodiscard]] std::optional<QPoint> arrowStepFor(int key);

} // namespace GyanisScribe::Ui
