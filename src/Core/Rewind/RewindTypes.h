/**
 * @file RewindTypes.h
 * @brief 回溯缓冲的公共值类型
 * @author Gyanis
 * @date 2026-11-08
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>

namespace GyanisScribe::Core
{

    /// 回溯缓冲对外的状态
    enum class RewindStatus
    {
        Disabled,          ///< 功能关闭
        Buffering,         ///< 正常抓帧
        PausedByExclusion, ///< 排除的程序在前台
        PausedByLock,      ///< 已锁屏
        PausedByError,     ///< 连续抓帧/编码失败
    };

    /// 一条可回选的帧样本
    struct RewindFrameSample
    {
        quint64    id          = 0;        ///< 单调递增的样本号
        qint64     monotonicMs = 0;        ///< 抓帧时刻，单调时钟
        QDateTime  capturedAt;             ///< 展示用本地时间
        QByteArray encodedPixels;          ///< 编码后的整帧，未变化时与上一帧共享
        QImage     thumbnail;              ///< 选择器缩略图
        QSize      logicalSize;            ///< 该屏逻辑尺寸
        qreal      devicePixelRatio = 1.0; ///< 该屏缩放因子
        QString    screenName;             ///< 抓的是哪块屏
        QRect      screenLogicalRect;      ///< 该屏的全局逻辑矩形
        bool       isCandidate = false;    ///< 画面变化或前台窗口变化
        QString    foregroundTitle;        ///< 抓帧时的前台窗口标题，可为空
    };

    /// 选择器需要的只读视图
    struct RewindFrameView
    {
        quint64   id = 0;              ///< 样本号
        QImage    thumbnail;           ///< 缩略图
        QDateTime capturedAt;          ///< 展示时间
        bool      isCandidate = false; ///< 是否候选帧
        QString   foregroundTitle;     ///< 前台窗口标题，可为空
    };

    /// 缓冲读数
    struct RewindBufferStats
    {
        int    sampleCount         = 0;     ///< 样本数
        qint64 uniquePixelBytes    = 0;     ///< 唯一编码像素字节
        qint64 thumbnailBytes      = 0;     ///< 缩略图字节
        qint64 coveredMilliseconds = 0;     ///< 覆盖时长
        bool   overBudget          = false; ///< 是否超出字节预算
    };

    /// 编码后的帧与缩略图
    struct RewindEncodedFrame
    {
        QByteArray encodedPixels; ///< 编码后的整帧
        QImage     thumbnail;     ///< 缩略图
    };

} // namespace GyanisScribe::Core
