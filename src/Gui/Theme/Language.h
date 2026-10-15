/**
 * @file Language.h
 * @brief 界面语言档位、配置取值词表与翻译器装载
 * @author Gyanis
 * @date 2026-10-10
 * @version 0.0.1
 * @copyright Copyright (c) . All rights reserved.
 */

#pragma once

#include "Base/Types.h"

#include <QObject>
#include <QStringList>

#include <string_view>

namespace GyanisScribe::Gui
{
    /**
     * @brief 界面语言档位
     * @details 界面源码文案是中文，因此 Chinese 这一档不需要语言包：它就是「不装 translator」，
     *          而不是去找一份和源码同文的 zh 包。
     */
    enum class AppLanguage
    {
        Chinese, ///< 中文，源码语言，不装载翻译器
        English, ///< 英文，需要 GyanisScribe_en.qm
    };

    /**
     * @brief 解析配置里存的语言取值
     * @param text 配置文本，只认词表里的 zh 与 en（大小写与首尾空格忽略）
     * @return 解析出的档位；文本不在词表里时返回带可用取值清单的原因
     */
    [[nodiscard]] Base::Expected<AppLanguage> parseAppLanguage(std::string_view text);

    /**
     * @brief 取档位对应的配置文本
     * @param language 档位
     * @return 写进配置文件用的词
     */
    [[nodiscard]] std::string_view appLanguageText(AppLanguage language);

    /**
     * @brief 取档位对应的语言包文件名
     * @param language 档位
     * @return 文件名；Chinese 档返回空文本，表示这一档不需要语言包
     */
    [[nodiscard]] QString translationFileName(AppLanguage language);

    /**
     * @brief 在给定目录里找语言包，返回按搜索顺序排好的已存在路径
     * @param language 档位
     * @param directories 搜索目录，顺序即优先级
     * @return 找到的文件路径；这一档不需要语言包、或哪儿都没找到时为空
     */
    [[nodiscard]] QStringList findTranslationFiles(AppLanguage language, const QStringList &directories);

    /**
     * @brief 界面默认的语言包搜索目录
     * @return 程序目录下的 translations 子目录，排在系统翻译目录之前
     */
    [[nodiscard]] QStringList defaultTranslationDirectories();

    /**
     * @brief 按档位装载翻译器
     * @param language 档位
     * @param directories 语言包搜索目录，默认取 defaultTranslationDirectories()
     * @return 成功返回空载荷；English 档找不到语言包、或语言包读不开时返回原因
     * @details 必须在任何界面对象构造之前调用，否则已经建好的控件仍拿源码文案。
     */
    [[nodiscard]] Base::Expected<void> applyLanguage(AppLanguage language, const QStringList &directories = defaultTranslationDirectories());

} // namespace GyanisScribe::Gui
