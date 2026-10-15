/**
 * @file DxgiCaptureSource.h
 * @brief Windows 上基于 DXGI 桌面重复的抓帧来源
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Capture/IScreenCaptureSource.h"

#include <memory>

namespace GyanisScribe::Plugins
{

#ifdef Q_OS_WIN
    class DxgiScreenDuplicator;
    class QScreenCaptureSource;

    /**
     * @brief DXGI 桌面重复抓帧
     * @details 与 Qt 的 grabWindow（GDI 位图往返）相比，这一路直接从显示适配器取桌面合成后的画面：
     *          抓一帧的中位耗时见 README 性能实测，那是录屏帧率那一格欠账的根因所在。
     *          桌面重复只提交「变过的帧」——等待超时说明这一格桌面没变，交回上一次拿到的那一张，
     *          所以它天然是「每次都有一帧可写」的供帧形状。
     * @note 抓窗口与取指针这两格仍走 Qt：桌面重复给的是整块桌面，「某个窗口」不在它的语义里；
     *       拿不到 DXGI 的机器（远程会话、驱动不认、桌面格式不是 BGRA）在帧这一层回落到 Qt 并记日志。
     */
    class DxgiCaptureSource final : public Core::IScreenCaptureSource
    {
    public:
        /**
         * @brief 建 D3D11 设备与桌面重复层
         * @details 设备建不起来时对象仍然可用，只是 isUsable() 为假——工厂据此回落 Qt 那一份，
         *          而不是让构造函数抛异常（这一条的可用性只是「本机能不能用这套原语」，不是错误）。
         */
        DxgiCaptureSource();

        /**
         * @brief 析构：放掉每块屏的重复接口与暂存纹理
         */
        ~DxgiCaptureSource() override;

        DxgiCaptureSource(const DxgiCaptureSource &)            = delete;
        DxgiCaptureSource &operator=(const DxgiCaptureSource &) = delete;

        /**
         * @brief 这套原语在这台机器上建起来了没有
         * @return true 表示 D3D11 设备可用
         */
        [[nodiscard]] bool isUsable() const noexcept;

        /**
         * @brief 真正从 DXGI 取到过几帧
         * @return 累计帧数；一次都没取到时回落路径会把它留在 0
         * @details 回落是静默的（同一份来源在拿不到 DXGI 时就交 Qt 的结果），
         *          没有这个读数，用例会把「回落也抓到了图」当成「DXGI 在工作」。
         */
        [[nodiscard]] int framesFromDxgi() const noexcept;

        /**
         * @brief 抓一块屏幕，见 Core::IScreenCaptureSource::grabScreen()
         * @param screenName 目标屏幕名；空串表示光标所在那块
         * @return 成功返回整帧；拿不到 DXGI 帧时回落 Qt 那一份并把原因记进日志
         */
        [[nodiscard]] Base::Expected<Base::CapturedImage> grabScreen(const QString &screenName) const override;

        /**
         * @brief 抓一个窗口，见 Core::IScreenCaptureSource::grabWindow()
         * @param windowHandle 平台窗口句柄
         * @return 转发给 Qt 那一份的结果
         */
        [[nodiscard]] Base::Expected<Base::CapturedImage> grabWindow(quintptr windowHandle) const override;

        /**
         * @brief 取鼠标指针形状，见 Core::IScreenCaptureSource::grabCursor()
         * @param screenName 与同一次抓屏传的同一个屏名
         * @return 转发给 Qt 那一份的结果
         */
        [[nodiscard]] std::optional<Core::CursorSprite> grabCursor(const QString &screenName) const override;

    private:
        std::unique_ptr<DxgiScreenDuplicator> m_duplicator; ///< 设备与每块屏的重复接口
        std::unique_ptr<QScreenCaptureSource> m_qtFallback; ///< 窗口抓取、指针与降级都走它
    };
#endif

} // namespace GyanisScribe::Plugins
