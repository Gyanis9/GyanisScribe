/**
 * @file CaptureIntent.h
 * @brief 截完之后做什么：档位、配置里的取值与设置页标签的唯一词表
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Types.h"

#include <QString>

#include <array>
#include <string_view>

namespace GyanisScribe::App
{

    /**
     * @brief 一帧截图交出去之后的去处
     * @details 「截完弹标注窗」只是其中一档：有人要的是按下热键就落到剪贴板、文件或桌面，
     *          多出来的那一层窗就是多一跳。档位由设置页选，词表在本文件唯一。
     */
    enum class CaptureIntent
    {
        Annotate,        ///< 打开标注窗，后续动作在那一排按钮上
        CopyToClipboard, ///< 只交剪贴板，不弹任何窗
        SaveToFile,      ///< 直接按命名规则存进截图目录
        PinToDesk,       ///< 直接贴回屏幕上原那块位置
        Recognize,       ///< 直接送文字识别
    };

    /// 一档去处：枚举、存进配置的 token，与设置页显示的中文标签
    struct CaptureIntentChoice
    {
        CaptureIntent    intent; ///< 这一档走哪一步
        std::string_view token;  ///< 配置里存的值，词表在这里唯一
        std::string_view label;  ///< 设置页上的中文标签，界面按 UTF-8 取
    };

    /// 全部档位，顺序就是设置页下拉的顺序；第一档是不填配置时的回落档
    inline constexpr std::array<CaptureIntentChoice, 5> kCaptureIntentChoices{
            CaptureIntentChoice{CaptureIntent::Annotate, "Annotate", "进标注窗"},       CaptureIntentChoice{CaptureIntent::CopyToClipboard, "Copy", "复制到剪贴板"},
            CaptureIntentChoice{CaptureIntent::SaveToFile, "Save", "直接存成文件"},     CaptureIntentChoice{CaptureIntent::PinToDesk, "Pin", "直接贴到屏幕上"},
            CaptureIntentChoice{CaptureIntent::Recognize, "Recognize", "直接识别文字"},
    };

    /**
     * @brief 取一档去处存进配置里的那个值
     * @param intent 目标档位
     * @return token；词表外的枚举值返回空，由调用方决定怎么报（新加枚举值而忘了补词表就落在这里）
     */
    [[nodiscard]] std::string_view captureIntentText(CaptureIntent intent) noexcept;

    /**
     * @brief 解析配置里存的去处
     * @param stored 配置读出的文本
     * @return 对应档位；读不出时返回 InvalidArgument 并列出可取的几个值
     * @details 不替用户猜一档：悄悄回落到「进标注窗」会把一个不想弹窗的用户的每一下截图都变成一次弹窗。
     *          读不出的那一格由调用方报给用户（设置页按默认档显示并把原因落进日志）。
     */
    [[nodiscard]] Core::Expected<CaptureIntent> parseCaptureIntent(const QString &stored);

    /**
     * @brief 词表的第一档：配置里没有这一项时的回落档
     * @return Annotate
     * @details 与「不填时界面显示什么、按下热键走哪一步」共用同一个出处，两处分开写就会漂移
     */
    [[nodiscard]] CaptureIntent defaultCaptureIntent() noexcept;

} // namespace GyanisScribe::App
