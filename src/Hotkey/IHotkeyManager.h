/**
 * @file IHotkeyManager.h
 * @brief 全局热键管理器的对外接口
 * @author Gyanis
 * @date 2026-10-09
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QObject>
#include <QString>

#include <vector>

namespace GyanisScribe::Hotkey
{

    /**
     * @brief 已支持的全局动作
     * @details 编号按「枚举序号 + 1」直接交给后端当热键 ID，所以新成员只能追加在末尾：
     *          中间插入会让已有登记与用户设置里的组合错位到一个动作上。
     */
    enum class HotkeyAction
    {
        Capture,      ///< 截图
        Record,       ///< 录屏
        Ocr,          ///< 识别文字
        Pick,         ///< 屏幕取色
        CaptureLong,  ///< 滚动长截图；追加在末尾，编号排到 5
        TogglePins,   ///< 收起或摆回全部贴图；追加在末尾，编号排到 6
        NextPinGroup, ///< 切到下一组贴图；追加在末尾，编号排到 7
        QuickSave,    ///< 快速保存：截图后直接存文件并复制；追加在末尾，编号排到 8
    };

    /**
     * @brief 一次登记没成功的那一条
     */
    struct HotkeyFailure
    {
        HotkeyAction action;      ///< 哪个动作没登记上
        QString      combination; ///< 当时用的组合文本，便于用户直接看懂
        Core::Error  error;       ///< 原因，含中文与替代做法
    };

    /**
     * @brief 一次自定义绑定的输入：功能标识与要绑的组合
     * @details 标识（token）是调用方自己的词表——热键这一层不认识「截图」「贴图组管理」这些功能名，
     *          它只负责「这一串组合按下时，把这个标识报回去」。
     */
    struct CustomHotkeyBinding
    {
        QString token;       ///< 功能标识，注册方自己认领
        QString combination; ///< 组合文本，例如 Ctrl+Alt+F
    };

    /**
     * @brief 一条自定义绑定没登记上
     */
    struct CustomHotkeyFailure
    {
        QString     token;       ///< 哪个功能没登记上
        QString     combination; ///< 当时用的组合文本
        Core::Error error;       ///< 原因，含中文与替代做法
    };

    /**
     * @brief 一轮登记的结果
     */
    struct HotkeyReloadReport
    {
        int                              registered = 0; ///< 成功登记的条数（固定的与自定义的一起算）
        std::vector<HotkeyFailure>       failures;       ///< 固定动作里没登记上的，一条不落全在这里
        std::vector<CustomHotkeyFailure> customFailures; ///< 自定义绑定里没登记上的；与上面那组分开记，界面按各自的名字报
    };

    /**
     * @brief 热键管理器契约
     */
    class IHotkeyManager : public QObject
    {
        Q_OBJECT

    public:
        using QObject::QObject;

        /**
         * @brief 析构管理器，实现方要注销自己登记的全部热键
         */
        ~IHotkeyManager() override = default;

        /**
         * @brief 按当前设置重新登记全部热键
         * @return 成功返回本轮结果（一条都没成也返回结果，不返回失败）
         * @return 后端本身不可用（例如本平台没有全局热键）时仍返回结果，把每一项都记进 failures
         * @note 先全注销再重登：设置改过一次就留下旧登记的话，旧组合会继续被占用
         */
        [[nodiscard]] virtual Core::Expected<HotkeyReloadReport> reload() = 0;

        /**
         * @brief 取某个动作当前生效的组合文本
         * @param action 动作
         * @return 设置里的文本；没设过则返回默认组合的文本
         */
        [[nodiscard]] virtual QString bindingText(HotkeyAction action) const = 0;

        /**
         * @brief 换一批自定义绑定（给「固定动作之外的功能」绑全局热键）
         * @param bindings 要登记的绑定；组合为空的那些由调用方自己滤掉，这里不再判「算不算没绑」
         * @details 只存下这一批，不顺手 reload：设置页一次改好几格时，逐格重登会把中间态报到用户脸上。
         *          调用方存完再调 reload()（或 registerHotkeys()）那一条路。
         */
        virtual void setCustomBindings(std::vector<CustomHotkeyBinding> bindings) = 0;

    signals:
        /**
         * @brief 某个已登记的热键被按下
         * @param action 对应的动作
         */
        void actionTriggered(GyanisScribe::Hotkey::HotkeyAction action);

        /**
         * @brief 一条自定义绑定被按下
         * @param token 登记时那个功能标识，原样报回
         * @details 与 actionTriggered 分两条信号：固定动作的枚举住在热键层，其余功能的词表住在界面层，
         *          硬并成一个枚举就会让热键层去认识「贴图组管理」这类它不该知道的东西。
         */
        void customActionTriggered(const QString &token);
    };

} // namespace GyanisScribe::Hotkey
