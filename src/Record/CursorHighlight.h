/**
 * @file CursorHighlight.h
 * @brief 把光标位置画成一圈，叠在已经抓好的录屏帧上
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QImage>
#include <QPoint>

#include <QtGlobal>

namespace GyanisScribe::Record
{

    /**
     * @brief 在帧上圈出光标所在的位置
     * @param frame 目标帧，就地改；按物理像素计
     * @param cursorGlobalLogical 光标的屏幕全局逻辑坐标
     * @param frameGlobalOriginLogical 这一帧左上角在屏幕全局逻辑坐标里的位置
     * @param devicePixelRatio 这一块屏的缩放因子，物理像素 = 逻辑偏移乘以它
     * @return 画了返回 true；光标不在这一帧里返回 false（不算失败）
     * @return 缩放因子不是正数、或帧是空图时返回 InvalidArgument 并点名坏在哪一项
     * @details 光标本身通常不在抓下来的画面里（QScreen::grabWindow 不含指针），所以「这一帧里鼠标在哪」
     *          只能靠位置叠一圈来回答。半径、宽度与颜色全部取 Ui::DesignTokens，界面层不许硬编码。
     * @note 帧上原来标的 devicePixelRatio 会被临时清成 1 再画：本函数一律按物理像素算，
     *       而整屏帧不带标记、选区裁出的帧带标记，两种都得给同一个结果。
     */
    [[nodiscard]] Core::Expected<bool> paintCursorHighlight(QImage &frame, const QPoint &cursorGlobalLogical, const QPoint &frameGlobalOriginLogical, qreal devicePixelRatio);

} // namespace GyanisScribe::Record
