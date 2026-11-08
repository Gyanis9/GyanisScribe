/**
 * @file FrameChangeDetector.h
 * @brief 判断两帧是否相同、是否值得标为候选帧
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>

namespace GyanisScribe::Core
{
    /// 一帧的轻量指纹
    struct FrameFingerprint
    {
        QSize      size;            ///< 原图尺寸
        QByteArray downscaledGray;  ///< 64×36 灰度缩略
        quint64    cacheKey = 0;    ///< QImage 数据键
        QString    foregroundTitle; ///< 前台窗口标题
    };

    /**
     * @brief 生成指纹
     * @param image 整屏帧
     * @param foregroundTitle 前台窗口标题，可为空
     * @return 指纹；空图返回空尺寸与空灰度
     */
    [[nodiscard]] FrameFingerprint makeFrameFingerprint(const QImage &image, const QString &foregroundTitle);

    /**
     * @brief 两帧画面是否相同
     * @param left 左指纹
     * @param right 右指纹
     * @return 尺寸与灰度都相同返回 true；任一为空返回 false
     */
    [[nodiscard]] bool isSameFrame(const FrameFingerprint &left, const FrameFingerprint &right) noexcept;

    /**
     * @brief 是否应标为候选帧
     * @param current 当前帧
     * @param previous 上一帧
     * @return 画面变化或前台标题变化返回 true
     */
    [[nodiscard]] bool isCandidateChange(const FrameFingerprint &current, const FrameFingerprint &previous) noexcept;
} // namespace GyanisScribe::Core
