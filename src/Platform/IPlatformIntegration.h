/**
 * @file IPlatformIntegration.h
 * @brief 平台集成入口：能力表、平台名，以及各平台后端的创建
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"
#include "Platform/IHotkeyBackend.h"
#include "Platform/PlatformCapabilities.h"
#include "Platform/WindowGeometry.h"

#include <QObject>
#include <QString>

#include <memory>
#include <vector>

namespace GyanisScribe::Platform
{

    /**
     * @brief 一个前台窗口的快照
     */
    struct ForegroundWindow
    {
        quintptr handle = 0; ///< 平台窗口句柄，与 Qt 的 WId 同一口径，可直接交给抓帧那一步
        QString  title;      ///< 标题栏文字；有些窗口不给标题，空串是正常的
    };

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

        /**
         * @brief 取当前前台窗口
         * @return 成功返回句柄与标题；本平台给不出时返回 PlatformUnsupported，并在文案里指一条替代做法
         * @note 只给句柄不给几何：抓哪一块由抓帧那一步按句柄去问，这里再读一次系统就是两处读数，
         *       窗口正在动的时候两边会不一致
         */
        [[nodiscard]] virtual Core::Expected<ForegroundWindow> foregroundWindow() const = 0;

        /**
         * @brief 桌面上看得见的顶层窗口，按 z 序从最上面排到最下面
         * @return 成功返回一份份快照（逻辑全局坐标）；本平台给不出时返回 PlatformUnsupported，并指一条替代做法
         * @details 截图遮罩用它做「悬停认出整扇窗口」：候选顺序就是 z 序，调用方取第一个含住光标的矩形。
         *          本程序自己的窗口也留在名单里——遮罩那一层盖住全屏，调用方按句柄把自己那几层剔掉，
         *          平台层不替界面决定「谁该出现在候选里」
         */
        [[nodiscard]] virtual Core::Expected<std::vector<ScreenWindow>> screenWindows() const = 0;
    };

    /**
     * @brief 按编译目标挑选平台实现
     * @return 已就绪的集成对象，调用方持有所有权
     * @details 判据是编译器而不是运行环境：容器里编到 Linux 的 Stub 实现，这正是跨平台隔离要的验证。
     *          macOS 暂时没有自己的实现，落到 Stub 那一档，能力表全为假并在界面给出「本平台尚未适配」提示。
     */
    [[nodiscard]] std::unique_ptr<IPlatformIntegration> createPlatformIntegration();

} // namespace GyanisScribe::Platform
