/**
 * @file AnnotationToolbarGeometry.h
 * @brief 标注工具栏的摆放几何：贴着选区放，上下都放不下时夹进屏幕
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QRect>
#include <QSize>

namespace GyanisScribe::Annotation
{

    /// 工具栏落在选区的哪一侧
    enum class ToolbarSide
    {
        Above, ///< 选区上方
        Below  ///< 选区下方
    };

    /**
     * @brief 一次摆放的结果
     */
    struct ToolbarPlacement
    {
        QRect       frame; ///< 工具栏的屏幕几何，已在可用区内完整可见
        ToolbarSide side;  ///< 相对选区落在哪一侧，界面据此决定阴影与箭头朝向
    };

    /// 选区边缘与工具栏之间的留白，取自 4/8/12/16/24/32 的间距阶梯
    inline constexpr int kToolbarGap = 8;

    /**
     * @brief 把工具栏摆到选区旁边
     * @param screenAvailable 目标屏幕的可用区（不含任务栏），逻辑坐标
     * @param toolbarSize 工具栏自身的尺寸，逻辑像素
     * @param selection 选区，逻辑坐标
     * @return 摆放结果；尺寸或选区不成立时返回 InvalidArgument，可用区为空返回 ScreenUnavailable
     * @details 优先放选区下方（右手持鼠标时不挡视线），下方放不下翻上方，两侧都放不下就贴着
     *          空隙更大的一侧并把纵坐标夹回可用区——工具栏不许出现「只露出一半」的情况。
     *          横向永远以选区中心对齐，超出可用区时夹住，宽度按可用区收缩。
     */
    [[nodiscard]] Core::Expected<ToolbarPlacement> placeToolbar(const QRect &screenAvailable, const QSize &toolbarSize, const QRect &selection);

} // namespace GyanisScribe::Annotation
