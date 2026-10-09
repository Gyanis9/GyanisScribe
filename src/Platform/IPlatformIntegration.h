/**
 * @file IPlatformIntegration.h
 * @brief 平台集成入口：能力表、平台名，以及各平台后端的创建
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Platform/IHotkeyBackend.h"
#include "Platform/PlatformCapabilities.h"

#include <QObject>
#include <QString>

#include <memory>

namespace GyanisScribe::Platform
{

    /**
     * @brief 平台集成抽象
     * @details 界面与业务模块只认识本接口；Windows API 一律关在各平台实现的文件里，
     *          这样容器里编的是同一套头文件，能验出「核心逻辑是不是真的没依赖 Windows」。
     * @note 新增能力时同时补三份：能力表的一位、接口一个方法、以及各平台实现各一份重写。
     *       只加接口不补 Stub 实现会让 Linux 侧编不过，那正是本层要挡住的错法。
     */
    class IPlatformIntegration : public QObject
    {
        Q_OBJECT

    public:
        using QObject::QObject;

        /**
         * @brief 析构集成对象
         */
        ~IPlatformIntegration() override = default;

        /**
         * @brief 本平台的能力表
         * @return 各项能力是否给得出；与「本仓现在实现了没有」无关，界面据此标灰入口并给原因
         */
        [[nodiscard]] virtual PlatformCapabilities capabilities() const noexcept = 0;

        /**
         * @brief 平台显示名
         * @return 面向用户的中文名，出现在关于页与降级提示里
         */
        [[nodiscard]] virtual QString displayName() const = 0;

        /**
         * @brief 创建一个全局热键后端
         * @return 调用方接管所有权；不支持该能力的平台交回 Stub 实现，其登记调用一律返回 PlatformUnsupported
         * @note 每次调用给一个新对象，不共享：重载配置要「先全注销再重登」，共享实例会把上一次的登记带过来
         */
        [[nodiscard]] virtual std::unique_ptr<IHotkeyBackend> createHotkeyBackend() = 0;
    };

    /**
     * @brief 按编译目标挑选平台实现
     * @return 已就绪的集成对象，调用方持有所有权
     * @details 判据是编译器而不是运行环境：容器里编到 Linux 的 Stub 实现，这正是跨平台隔离要的验证。
     *          macOS 暂时没有自己的实现，落到 Stub 那一档，能力表全为假并在界面给出「本平台尚未适配」提示。
     */
    [[nodiscard]] std::unique_ptr<IPlatformIntegration> createPlatformIntegration();

} // namespace GyanisScribe::Platform
