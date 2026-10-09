/**
 * @file ImageFit.h
 * @brief 图片等比适配：把一张图摆进一个控件，再把图上的方框换算回控件坐标
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QRect>
#include <QSize>

namespace GyanisScribe::Core
{

    /**
     * @brief 算图片在控件里的显示区
     * @param widgetSize 控件尺寸，逻辑像素
     * @param imageSize 图片尺寸，像素
     * @return 等比缩放后居中的矩形；任一尺寸不成立时返回空矩形
     * @details 保比例居中，不拉伸：图被拉变形之后，图上方框与屏上位置的换算就不再是同一个比例。
     */
    [[nodiscard]] QRect displayRectFor(const QSize &widgetSize, const QSize &imageSize);

    /**
     * @brief 把图上方框换算到控件坐标
     * @param imageRect 图片坐标系里的方框
     * @param imageSize 图片尺寸，像素
     * @param displayRect displayRectFor 的结果
     * @return 控件坐标下的方框；尺寸不成立时返回空矩形
     * @note 两套换算必须同源：预览按 displayRectFor 摆图、高亮按这里画框，
     *       分开各算一次就会点着第 3 行却圈住第 2 行。
     */
    [[nodiscard]] QRect imageRectToDisplay(const QRect &imageRect, const QSize &imageSize, const QRect &displayRect);

} // namespace GyanisScribe::Core
