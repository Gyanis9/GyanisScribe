/**
 * @file BlankCanvas.h
 * @brief 空白画布：不抓屏，直接造一张能标注的底图
 * @author Gyanis
 * @date 2026-10-13
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QSize>
#include <QString>

namespace GyanisScribe::Annotation
{

    /**
     * @brief 空白画布的默认逻辑尺寸
     * @details 够写一段带图的说明，又不至于比多数笔记本屏还宽（摆得下才谈得上居中）。
     */
    inline constexpr QSize kBlankCanvasSize{1280, 720};

    /**
     * @brief 造一张空白画布
     * @param logicalSize 画布的逻辑尺寸；任一维非正时退回默认档
     * @param devicePixelRatio 目标屏的缩放因子，必须为正，非正时按 1.0 处理
     * @param screenName 这块屏的名字，原样写回帧里（标注窗与工具栏摆放都按它找屏）
     * @param transparent 真空白（透明底）还是白底
     * @return 一份自洽的 CapturedImage：物理像素 = 逻辑尺寸乘以缩放因子，且帧上标的 dpr 与之一致
     * @details 「来源区域」留空：这一张不是从屏上哪一块抓来的，留着空矩形也让刷新截图那条路自己拒掉。
     *          透明底带的是 alpha：存 PNG/WebP 留得住，存 JPG 时保存那条路会先垫一层白（见 `Core::imageFormatHasAlpha` 的消费方），
     *          不会像直接压 JPG 那样把透明区涂成黑色。
     */
    [[nodiscard]] Core::CapturedImage makeBlankCanvas(const QSize &logicalSize, qreal devicePixelRatio, const QString &screenName, bool transparent);

    /**
     * @brief 把画布摆在可用区中央
     * @param available 这块屏的可用区（全局逻辑坐标）
     * @param logicalSize 画布的逻辑尺寸
     * @return 居中的矩形；可用区比画布小时退回可用区本身（宁可顶满也不摆到屏外）
     */
    [[nodiscard]] QRect blankCanvasRegion(const QRect &available, const QSize &logicalSize);

} // namespace GyanisScribe::Annotation
