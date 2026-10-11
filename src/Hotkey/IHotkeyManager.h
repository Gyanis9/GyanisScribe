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
     * @brief 一轮登记的结果
     */
    struct HotkeyReloadReport
    {
        int                        registered = 0; ///< 成功登记的条数
        std::vector<HotkeyFailure> failures;       ///< 没登记上的条目，一条不落全在这里
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

    signals:
        /**
         * @brief 某个已登记的热键被按下
         * @param action 对应的动作
         */
        void actionTriggered(GyanisScribe::Hotkey::HotkeyAction action);
    };

} // namespace GyanisScribe::Hotkey
