/**
 * @file ThumbnailPixels.h
 * @brief 缩略图像素解码：在调用线程上读图并裁出 1x/2x/3x 三档
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QImage>
#include <QString>

namespace GyanisScribe::Gui
{
    /**
     * @brief 缩略图最高的设备像素比档位
     * @details 再高的缩放比按 3 档放大，1x/2x/3x 覆盖现有屏幕；三档一律生成，
     *          不做按屏裁剪，每张的位图合计约 0.5 MiB。
     */
    inline constexpr int kLargestThumbnailDevicePixelRatio = 3;

    /// 一次缩略图解码的产物；界面线程只拿它组装图标与写缓存
    struct DecodedThumbnail
    {
        QImage oneX;                        ///< 96 见方
        QImage twoX;                        ///< 192 见方
        QImage threeX;                      ///< 288 见方
        bool   readFailed          = false; ///< 像素没读出来，界面线程改摆兜底图标
        bool   decodedOffGuiThread = false; ///< 解码确实发生在界面线程之外
    };

    /**
     * @brief 读一张成品并在当前线程上裁成 1x/2x/3x 三档
     * @param displayPath 成品路径的显示用文本
     * @return 三档像素，外加读失败与线程判断两枚读数；不碰图标、缓存与列表
     * @details 交给工作线程的 lambda 只按值捕获路径：调用方在解码途中析构时任务照样跑完，
     *          不会碰到已经不在的对象。
     */
    [[nodiscard]] DecodedThumbnail decodeThumbnailPixels(const QString &displayPath);

    /**
     * @brief 缩略图缓存键：路径、修改时间（毫秒）与文件大小
     * @param displayPath 成品路径的显示用文本
     * @return 三段以竖线分隔的键
     * @details 修改时间与大小各管一路：只换内容、保留时间戳的覆盖只能靠大小看出来。
     */
    [[nodiscard]] QString thumbnailCacheKey(const QString &displayPath);

} // namespace GyanisScribe::Gui
