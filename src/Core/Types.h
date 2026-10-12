/**
 * @file Types.h
 * @brief 跨模块共用的值类型与错误返回通道
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QImage>
#include <QString>

#include <expected>
#include <string>
#include <string_view>

namespace GyanisScribe::Core
{

    /**
     * @brief 取图方式
     * @details 取值可能被写进历史记录与配置，新成员只能追加在末尾，不得插入或重排。
     */
    enum class CaptureMode
    {
        Interactive, ///< 进入遮罩由用户框选
        FullScreen,  ///< 整块屏幕
        Region,      ///< 调用方给定矩形
        Window,      ///< 调用方给定窗口
        LastRegion,  ///< 复用上一次的选区
    };

    /**
     * @brief 故障类别，按调用方的处置方式划分而不是按抛出位置
     */
    enum class ErrorCode
    {
        Unknown,             ///< 兜底分支，配合日志定位
        CancelledByUser,     ///< 用户按 Esc 或主动取消，调用方静默回退即可
        InvalidArgument,     ///< 入参不合法，属调用方 bug，不重试
        ScreenUnavailable,   ///< 目标屏幕消失或拿不到有效几何
        PlatformUnsupported, ///< 本平台没有该能力，走降级提示而不是静默当成成功
        DependencyMissing,   ///< 外部程序或组件不在场，文案里要给出获取方式
        FileWriteFailed,     ///< 落盘失败，带路径便于用户自查权限
        Busy,                ///< 已有同类操作在跑，等它结束再试
        InternalError,       ///< 实现层不变式被破坏，必须记日志
    };

    /**
     * @brief 可恢复错误，承载中文「原因 + 替代做法」
     */
    struct Error
    {
        ErrorCode   code = ErrorCode::Unknown; ///< 故障类别
        std::string message;                   ///< 面向用户的中文文案
    };

    /**
     * @brief 结果返回类型，可恢复错误一律走这里而不是异常
     * @tparam Value 成功时的载荷类型
     */
    template<typename Value>
    using Expected = std::expected<Value, Error>;

    /// 无载荷的成功/失败，用于「做没做成」类接口
    using CommonStatus = std::expected<void, Error>;

    /**
     * @brief 一次抓屏的产物
     * @details image 的像素尺寸是物理像素，logicalSize 与 devicePixelRatio 一起决定同一帧在逻辑坐标下的
     *          大小；两者不自洽时按缺陷处理，不在下游做取整改样。
     */
    struct CapturedImage
    {
        QImage  image;                  ///< 抓到的原始像素，格式为 Format_ARGB32_Premultiplied
        QSize   logicalSize;            ///< 逻辑坐标下的尺寸，用于界面与标注摆放
        qreal   devicePixelRatio = 1.0; ///< 该屏缩放因子，物理像素 = 逻辑尺寸乘以它
        QString screenName;             ///< 来源屏幕标识，多显示器场景据此回到正确的那一块
        QRect   logicalRegion;          ///< 来自哪一块屏幕内逻辑区域；整屏抓取或来源未知时是空矩形
    };

} // namespace GyanisScribe::Core
