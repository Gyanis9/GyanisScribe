/**
 * @file Text.h
 * @brief 面向界面的文案取值口：把源码里的中文交给翻译目录查表
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include <QCoreApplication>
#include <QString>

#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

namespace GyanisScribe::Base
{
    /**
     * @brief 翻译目录的 context
     * @details 全工程只用这一个：取值口在 uiText 里，手写 .ts 时也按它归组，
     *          分成几个 context 就得在每个界面对象里各查一遍表。
     */
    inline constexpr const char *kTextContext = "GyanisScribe";

    /**
     * @brief 取一条界面文案
     * @param text 源码里的中文文案，NUL 结尾
     * @return 当前语言下应该显示的文本；没有翻译目录、或这条没进目录时原样返回中文
     * @details 走运行期查表而不是 tr()：显示用的标签存在按位置对齐的 constexpr 表里
     *          （档位—标签的对应只能有一个真源），tr() 要求字面量实参，
     *          把表改成函数会让每个消费点都跟着改。代价是 lupdate 认不出这些串，
     *          所以目录由 scripts/check-translation-catalog.py 从源码里穷举生成，
     *          并由同一条脚本作为门禁（ctest 用例 Style.TranslationCatalog）断言「源码用到的都在目录里、且译文非空」。
     * @note 字面量走这一档。从表里取出的 std::string_view 走下面那个重载——不要取 .data() 再传进来：
     *       查表接口只认过 0 结尾的指针，表项哪天真改成切片（子串），读越界是静默的
     *       （clang-tidy 的 bugprone-suspicious-stringview-data-usage 会当场报出来）。
     */
    inline QString uiText(const char *text)
    {
        return QCoreApplication::translate(kTextContext, text);
    }

    /**
     * @brief 取一条界面文案（文案以 std::string_view 形态拿到时走这一档）
     * @param text 源码里的中文文案，按长度收，不要求过 0 结尾
     * @return 与 const char* 那一档同解
     * @details 查表接口只吃 NUL 结尾的指针，所以先按长度收成一份自有串再查；
     *          字面量与字符串常量重叠的来路不必担心多这一次拷贝——它们走上面那一档。
     */
    inline QString uiText(const std::string_view text)
    {
        const std::string owned{text};
        return QCoreApplication::translate(kTextContext, owned.c_str());
    }

    /**
     * @brief 取一条界面文案的 std::string 形态
     * @param text 源码里的中文文案，NUL 结尾
     * @return 当前语言下的文本（UTF-8）
     * @details 错误文案落在 Base::Error.message（std::string）里，界面显示时才转 QString；
     *          在构造点就查表，才能让英文档下报错也跟着变。
     */
    inline std::string uiTextUtf8(const char *text)
    {
        return uiText(text).toStdString();
    }

    /**
     * @brief 取一条界面文案的 std::string 形态（文案以 std::string_view 形态拿到时走这一档）
     * @param text 源码里的中文文案，按长度收，不要求过 0 结尾
     * @return 与 const char* 那一档同解
     */
    inline std::string uiTextUtf8(const std::string_view text)
    {
        return uiText(text).toStdString();
    }

    /**
     * @brief 用运行期取到的文案做格式化
     * @tparam Args 与 pattern 里 {} 同序的参数类型
     * @param pattern 已经查过表的文案（可能来自英文目录）
     * @param args 依次填进 {} 的值
     * @return 填好的文本
     * @details 不能直接写 std::format(uiTextUtf8("…"), args)：format_string 的构造是 consteval 的，
     *          MSVC 19.51 对运行期格式串直接报 C7595，而 std::runtime_format 这一版 STL 还没有。
     *          这里走 vformat，等于把「格式串与参数是否对得上」从编译期挪到运行期——
     *          代价是译文里出现不成对的花括号时会抛 std::format_error，
     *          那种情况原样返回文案（少填一个参数比整条链路抛出去好），目录门禁负责把它找出来。
     */
    template<typename... Args>
    [[nodiscard]] std::string uiFormat(const std::string_view pattern, const Args &...args)
    {
        try
        {
            return std::vformat(pattern, std::make_format_args(args...));
        } catch (const std::format_error &)
        {
            return std::string{pattern};
        }
    }

} // namespace GyanisScribe::Base
