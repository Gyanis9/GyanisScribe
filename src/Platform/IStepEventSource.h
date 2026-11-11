/**
 * @file IStepEventSource.h
 * @brief 步骤记录用的平台输入事件源：全局鼠标点击与前台窗口变化
 * @author Gyanis
 * @date 2026-11-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QDateTime>
#include <QMetaType>
#include <QObject>
#include <QPoint>

#include <memory>

namespace GyanisScribe::Platform
{

    /**
     * @brief 事件源交回的一次输入事件
     * @details 坐标一律是 Qt 的逻辑全局坐标；平台钩子拿到的是物理屏幕坐标，换算在实现里完成。
     */
    struct StepInputEvent
    {
        /**
         * @brief 事件种类
         */
        enum class Kind
        {
            MouseClick,             ///< 用户在自己以外的窗口上点了一下
            ForegroundWindowChanged ///< 前台窗口换了一扇
        };

        Kind      kind = Kind::MouseClick; ///< 事件种类
        QPoint    globalLogicalPoint;      ///< 全局逻辑坐标；点击时是抬起点的光标位置（与按下点相差不超过 6 个逻辑像素），窗口切换时只作占位
        quintptr  windowHandle = 0;        ///< 事件落点窗口句柄；窗口切换时是新的前台窗口
        bool      rightButton  = false;    ///< 是不是右键；只有点击事件用得上
        QDateTime at;                      ///< 事件发生的本地时刻
    };

    /**
     * @brief 平台输入事件源
     * @details 只负责「发生了什么」：把点击与前台窗口切换排队投递出来，抓屏、界面元素识别与
     *          步骤整理都由消费方在 GUI 线程自己做。
     * @note 只在 GUI 线程 start/stop；实现里的钩子回调不做抓屏、不调 UI Automation，也不阻塞。
     */
    class IStepEventSource : public QObject
    {
        Q_OBJECT

    public:
        using QObject::QObject;

        /**
         * @brief 析构事件源，实现方必须保证钩子已摘除
         */
        ~IStepEventSource() override = default;

        /**
         * @brief 开始监听全局输入
         * @return 成功返回空载荷；本平台没有该能力时返回 PlatformUnsupported，系统拒绝安装钩子时返回 InternalError，文案写明原因与替代做法
         * @note 重复调用按幂等处理；同一线程里已经有一份运行中的事件源时，实现应返回 Busy 而不是各收一半事件
         */
        [[nodiscard]] virtual Base::Expected<void> start() = 0;

        /**
         * @brief 停止监听并摘除钩子
         * @note 幂等；没启动过或已经停过都算成功
         */
        virtual void stop() = 0;

        /**
         * @brief 是否正在监听
         * @return 正在监听返回 true
         */
        [[nodiscard]] virtual bool isRunning() const noexcept = 0;

    signals:
        /**
         * @brief 收到一次可记录的输入事件
         * @param event 事件内容；坐标已是逻辑全局坐标
         * @details 从钩子回调排队投递到 GUI 线程发出，订阅者可以直接做抓屏与 UI Automation
         */
        void stepEvent(const StepInputEvent &event);

        /**
         * @brief 运行期故障
         * @param error 中文原因与替代做法
         * @details 启动失败走 start() 的返回值；本信号留给监听期间才发现的故障
         */
        void failed(const Base::Error &error);
    };

    /**
     * @brief 按编译目标创建事件源
     * @return 已就绪的事件源，调用方持有所有权；不支持该能力的平台交回一启动即报 PlatformUnsupported 的桩
     * @details 判据是编译器而不是运行环境：Win32 钩子只编进 Windows 版，
     *          消费方只认识 IStepEventSource，不因此出现条件编译。
     */
    [[nodiscard]] std::unique_ptr<IStepEventSource> createStepEventSource();

} // namespace GyanisScribe::Platform

Q_DECLARE_METATYPE(GyanisScribe::Platform::StepInputEvent)
