/**
 * @file IPlatformIntegration.h
 * @brief 平台集成入口：能力表、平台名，以及各平台后端的创建
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"
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
     * @details 界面与业务模块只认识本接口；Windows API 一律关在 Platform/Windows 的文件里，
     *          将来接别的平台时，核心逻辑不该为它改一行。
     * @note 新增能力时同时补三份：能力表的一位、接口一个方法、以及各平台实现各一份重写。
     *       只加接口不补实现会让那一档编不过，那正是本层要挡住的错法。
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
        [[nodiscard]] virtual Base::Expected<ForegroundWindow> foregroundWindow() const = 0;

        /**
         * @brief 本程序现在有没有登记开机启动
         * @return 成功返回状态；本平台没有这一能力时返回 PlatformUnsupported 并写明替代做法
         * @note 状态的真源是系统里那一条启动项本身，不在配置里另存一份——两份真源迟早对不上
         */
        [[nodiscard]] virtual Base::Expected<bool> autostartEnabled() const = 0;

        /**
         * @brief 开/关开机启动
         * @param enabled true 表示开机时自动起本程序
         * @return 成功返回空载荷；写不进去或本平台没有这一能力时返回带中文原因的错误
         * @details 入口只从设置页来，且按 capabilities().autostart 决定给不给点
         */
        [[nodiscard]] virtual Base::Expected<void> setAutostart(bool enabled) = 0;

        /**
         * @brief 桌面上看得见的顶层窗口，按 z 序从最上面排到最下面
         * @return 成功返回一份份快照（逻辑全局坐标）；本平台给不出时返回 PlatformUnsupported，并指一条替代做法
         * @details 截图遮罩用它做「悬停认出整扇窗口」：候选顺序就是 z 序，调用方取第一个含住光标的矩形。
         *          本程序自己的窗口也留在名单里——遮罩那一层盖住全屏，调用方按句柄把自己那几层剔掉，
         *          平台层不替界面决定「谁该出现在候选里」
         */
        [[nodiscard]] virtual Base::Expected<std::vector<ScreenWindow>> screenWindows() const = 0;

        /**
         * @brief 光标处一摞界面元素，从最深一级往上排到所在窗口的根
         * @param globalLogicalPoint 全局逻辑坐标
         * @param excludeWindowHandles 命中到这几位里的窗口直接跳过：截图时把自家的遮罩窗交进来，
         *        免得全屏置顶的遮罩自己把底下真正的界面挡在名单外面
         * @return 成功返回元素矩形（逻辑全局），从最深一级排到窗口根；空一份表示光标下这块没有可切分的界面元素
         * @return 本平台给不出时返回 PlatformUnsupported，并指一条替代做法
         * @details 截图遮罩用它做「悬停细分到元素一级」，比整扇窗口更贴目标。矩形按包含关系逐级套住光标点；
         *          零宽高的中间层由实现剔除，而相邻同矩形的包装层（惰性树里常见）如实交回——消费方按需归并，
         *          自己决定几层算一档。实现在 Windows 上先认「光标下第一扇不在排除名单里的可见窗」再问 UI Automation：
         *          UIA 的命中测试穿不过置顶窗（本机探针实测，工具窗也不例外），所以不能直接问「点下的元素」。
         */
        [[nodiscard]] virtual Base::Expected<std::vector<QRect>> elementChainAtPoint(const QPoint &globalLogicalPoint, const std::vector<quintptr> &excludeWindowHandles) const = 0;
    };

    /**
     * @brief 按编译目标挑选平台实现
     * @return 已就绪的集成对象，调用方持有所有权
     * @details 判据是编译器而不是运行环境：现在只有 Windows 实现，
     *          将来接别的平台时在这一层挂各自的分支，这正是跨平台隔离要的验证。
     */
    [[nodiscard]] std::unique_ptr<IPlatformIntegration> createPlatformIntegration();

} // namespace GyanisScribe::Platform
