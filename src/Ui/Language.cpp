#include "Ui/Language.h"

#include "Base/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QTranslator>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>

namespace
{
    /**
     * @brief 按位置对齐的取值表
     * @details 词与档位必须同序：parseAppLanguage 按下标回档位、appLanguageText 按下标取词，
     *          两处共用这一张表才不会各说各话（与 Ui/ThemeKind.cpp 同一形状）。
     */
    constexpr std::array<std::string_view, 2> kLanguageTokens = {"zh", "en"};

    /**
     * @brief 各档位的语言包文件名，与档位同序
     * @details 中文档留空：源码文案就是中文，装一份同文的包只会多一次失败机会。
     */
    constexpr std::array<std::string_view, 2> kTranslationFiles = {"", "GyanisScribe_en.qm"};
} // namespace

namespace GyanisScribe::Ui
{
    Base::Expected<AppLanguage> parseAppLanguage(const std::string_view text)
    {
        // 大小写与首尾空格都归一后再比：手改配置文件的人不该因为写了 ZH 就被判坏值
        std::string normalized{text};
        std::erase_if(normalized, [](const char character) { return character == ' ' || character == '\t' || character == '\r' || character == '\n'; });
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](const unsigned char character) { return std::tolower(character); });

        for (std::size_t index = 0; index < kLanguageTokens.size(); ++index)
        {
            if (kLanguageTokens[index] == normalized)
            {
                return static_cast<AppLanguage>(index);
            }
        }

        // 逐项列出可用取值：只报「取值不对」用户不知道该填什么
        return Base::Expected<AppLanguage>{std::unexpected{Base::Error{
                .code    = Base::ErrorCode::InvalidArgument,
                .message = std::format("界面语言取值 \"{}\" 不在可用档位里：配置里只能是 zh 或 en（大小写不敏感），"
                                       "也可以在设置的通用页从「中文」「English」两项里选",
                                       text),
        }}};
    }

    std::string_view appLanguageText(const AppLanguage language)
    {
        const auto index = static_cast<std::size_t>(language);
        // 越界档位属调用方 bug：回落到中文会把「新加一档忘了补词表」藏起来，先记一条再回落
        if (index >= kLanguageTokens.size())
        {
            Base::Logger::warning(std::format("未知的界面语言档位（枚举值 {}），按中文处理", static_cast<int>(language)));
            return kLanguageTokens[0];
        }
        return kLanguageTokens[index];
    }

    QString translationFileName(const AppLanguage language)
    {
        const auto index = static_cast<std::size_t>(language);
        if (index >= kTranslationFiles.size())
        {
            return {};
        }
        const auto name = kTranslationFiles[index];
        // 空文本是「这一档不需要语言包」的显式表达，不能当成「找文件名失败」
        return name.empty() ? QString{} : QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
    }

    QStringList findTranslationFiles(const AppLanguage language, const QStringList &directories)
    {
        const auto fileName = translationFileName(language);
        if (fileName.isEmpty())
        {
            return {};
        }

        QStringList found;
        for (const auto &directory: directories)
        {
            const QFileInfo candidate{QDir{directory}.filePath(fileName)};
            // 判的是「这是个可读的文件」：QFile::exists 对同名目录也为真，那样会把目录当语言包交下去
            if (candidate.isFile())
            {
                found.append(candidate.filePath());
            }
        }
        return found;
    }

    QStringList defaultTranslationDirectories()
    {
        // 随包分发的语言包就在程序目录的 translations 子目录里，优先级高于 Qt 自带的翻译目录
        const auto  applicationDirectory = QCoreApplication::applicationDirPath();
        QStringList directories{QDir{applicationDirectory}.filePath(QStringLiteral("translations"))};
        if (const auto systemDirectory = QLibraryInfo::path(QLibraryInfo::TranslationsPath); !systemDirectory.isEmpty())
        {
            directories.append(systemDirectory);
        }
        return directories;
    }

    Base::Expected<void> applyLanguage(const AppLanguage language, const QStringList &directories)
    {
        const auto candidates = findTranslationFiles(language, directories);
        if (candidates.isEmpty())
        {
            const auto fileName = translationFileName(language);
            if (fileName.isEmpty())
            {
                // 中文档没有语言包这一步：源码文案就是最终文案，直接成功
                return {};
            }

            // 报出找过的目录，否则用户只能猜「到底去哪儿找的」
            const auto searched = directories.join(QStringLiteral("；"));
            return Base::Expected<void>{std::unexpected{Base::Error{
                    .code    = Base::ErrorCode::DependencyMissing,
                    .message = std::format("找不到英文语言包 {}：请把它放在程序目录的 translations 子目录里（构建时由 lrelease 生成在 "
                                           "build/<预设>/translations/）。本次按中文继续，找过的目录：{}",
                                           fileName.toStdString(), searched.toStdString()),
            }}};
        }

        auto *translator = new QTranslator{QCoreApplication::instance()};
        // 所有权交给应用对象：translator 必须活得比所有界面对象久，随本函数返回就销毁会让翻译在半路失效
        if (!translator->load(candidates.first()))
        {
            // 读不开就别留在应用对象上：它已挂进 parent 的子对象表，留着就是一份永远不会命中的翻译器
            delete translator;
            return Base::Expected<void>{std::unexpected{Base::Error{
                    .code    = Base::ErrorCode::DependencyMissing,
                    .message = std::format("语言包 {} 读不开：它可能是旧版 lrelease 生成的、或者被截断了。本次按中文继续", candidates.first().toStdString()),
            }}};
        }

        QCoreApplication::installTranslator(translator);
        Base::Logger::info(std::format("界面语言装载完成：{}", candidates.first().toStdString()));
        return {};
    }

} // namespace GyanisScribe::Ui
