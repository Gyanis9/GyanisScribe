/**
 * @file SourceInfo.h
 * @brief 一次截图的前台来源：进程名、窗口标题、屏幕名与捕获时刻
 * @author Gyanis
 * @date 2026-11-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

namespace GyanisScribe::Core
{

    /**
     * @brief 一次截图的前台来源
     * @details 截图、历史与记忆库共用这一份形状；取不到时字段为空串或无效时刻，不拿占位串冒充
     */
    struct CaptureSourceInfo
    {
        QString   processName; ///< 进程可执行文件名（不含路径）；取不到时为空串
        QString   windowTitle; ///< 前台窗口标题；窗口不给标题时为空串
        QString   screenName;  ///< 抓帧所在屏幕的标识；来源没报屏幕时为空串
        QDateTime capturedAt;  ///< 这一帧拍下的本地时刻；无效表示来源没给时间
    };

} // namespace GyanisScribe::Core

Q_DECLARE_METATYPE(GyanisScribe::Core::CaptureSourceInfo)
