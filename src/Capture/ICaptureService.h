/**
 * @file ICaptureService.h
 * @brief 截图编排的对外契约
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QObject>
#include <QRect>
#include <QString>
#include <QtGlobal>

namespace GyanisScribe::Capture
{

    /**
     * @brief 截图服务
     * @details 所有方法都不返回结果而是发信号：抓屏在部分平台上要走一次同步取帧，
     *          把成功与失败都收进同一个通道，调用方（控制器）只订阅信号就能覆盖两条路，
     *          不会出现「返回值与信号各报各的」这种两处不同解的现场。
     */
    class ICaptureService : public QObject
    {
        Q_OBJECT

    public:
        using QObject::QObject;

        /**
         * @brief 析构服务
         */
        ~ICaptureService() override = default;

        /**
         * @brief 抓一块屏幕
         * @param screenName 目标屏幕名，空串表示光标所在那块
         */
        virtual void captureFullScreen(const QString &screenName = {}) = 0;

        /**
         * @brief 抓一块屏幕上的指定逻辑区域，并把这次选区记进配置供「上次区域」复用
         * @param logicalRegion 该屏内的逻辑矩形
         * @param screenName 目标屏幕名，空串表示光标所在那块
         */
        virtual void captureRegion(const QRect &logicalRegion, const QString &screenName = {}) = 0;

        /**
         * @brief 抓一个窗口
         * @param windowHandle 平台窗口句柄
         */
        virtual void captureWindow(quintptr windowHandle) = 0;

        /**
         * @brief 复用上一次成功截图的区域
         * @note 没记过、或记的位置已经不在任何屏上时走 captureFailed
         */
        virtual void captureLastRegion() = 0;

        /**
         * @brief 进入遮罩框选
         * @note 每块屏铺一张遮罩；取消或确认后一并收掉
         */
        virtual void startInteractive() = 0;

        /**
         * @brief 取消正在进行的框选
         */
        virtual void cancelInteractive() = 0;

    signals:
        /**
         * @brief 抓到了一帧
         * @param image 抓屏产物，物理像素与逻辑尺寸、缩放因子三者自洽
         */
        void captured(const GyanisScribe::Core::CapturedImage &image);

        /**
         * @brief 这次截图没成
         * @param error 原因，中文且带可操作的下一步
         */
        void captureFailed(const GyanisScribe::Core::Error &error);

        /**
         * @brief 用户取消了框选
         */
        void captureCancelled();
    };

} // namespace GyanisScribe::Capture
