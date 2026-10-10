/**
 * @file Choices.h
 * @brief 设置项词表的通用两件：把可取值拼成人话、词表外的取值统一报错
 * @author Gyanis
 * @date 2026-10-11
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Core/Text.h"
#include "Core/Types.h"

#include <QString>

#include <array>
#include <string>
#include <string_view>

namespace GyanisScribe::Core
{

    /**
     * @brief 把词表里全部 token 按表序拼成「A / B / C」
     * @tparam Choice 只要有 token 成员（std::string_view）的词表项
     * @param choices 那张词表
     * @return 拼好的文本，空表返回空串
     */
    template<typename Choice, std::size_t Count>
    [[nodiscard]] std::string tokenListText(const std::array<Choice, Count> &choices)
    {
        std::string joined;
        for (const auto &choice: choices)
        {
            joined += joined.empty() ? "" : " / ";
            joined += choice.token;
        }
        return joined;
    }

    /**
     * @brief 词表外的取值统一从这一处报错：点名这一格叫什么、可取哪些值、键在哪儿、去哪一页重选
     * @tparam Choice 只要有 token 成员（std::string_view）的词表项
     * @param choices 那张词表，报错时按表序列出可取值
     * @param stored 配置里实际读出的文本
     * @param settingName 这一格的中文名（已经过翻译目录）
     * @param pageName 该去重选的那一页的中文名（已经过翻译目录）
     * @param key 配置里的键名，写进文案便于手工修
     * @return InvalidArgument 错误，文案是「原因 + 恢复办法」那一句
     * @details 同一类错法在两格上给出两种恢复办法，用户照着做完第一次，第二次就对不上；
     *          所以这句只能在一处写，页名与格名由调用方填进来。
     */
    template<typename Choice, std::size_t Count>
    [[nodiscard]] Core::Error unknownTokenError(const std::array<Choice, Count> &choices, const QString &stored, std::string_view settingName, std::string_view pageName,
                                                std::string_view key)
    {
        return Core::Error{
                .code    = Core::ErrorCode::InvalidArgument,
                .message = Core::uiFormat(Core::uiTextUtf8("{}：取值 \"{}\" 不在可选值里，可取的只有 {}。请在设置的{}重选，或删掉 {} 这一项回到默认档"), std::string{settingName},
                                          stored.toStdString(), tokenListText(choices), std::string{pageName}, std::string{key}),
        };
    }

} // namespace GyanisScribe::Core
