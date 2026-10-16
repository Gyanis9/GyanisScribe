#include "Base/Paths.h"

#include "Base/Logger.h"
#include "Base/Text.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QStandardPaths>
#include <QString>

#include <algorithm>
#include <array>
#include <format>
#include <string_view>
#include <system_error>
#include <utility>

namespace GyanisScribe::Base
{
    namespace
    {

        /// 落在用户目录下时统一套这一层，避免与别的应用混在同一个 Pictures 里
        constexpr std::string_view kApplicationFolder = "GyanisScribe";

        /// 文件名模板的变量表：变量名 → 时刻格式。表里没有的 %xxx% 一律拒
        constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kNameTemplateTokens{
                std::pair{"date", "yyyyMMdd"},
                std::pair{"time", "HHmmss"},
                std::pair{"ms", "zzz"},
        };

        /// 名字里不许出现的字符：前两个会把文件写到别的目录，其余是 Windows 的保留字符
        constexpr std::string_view kForbiddenNameCharacters = "/\\:*?\"<>|";

        /// 模板长度上限：太长的名字在别的程序里也常常打不开
        constexpr qsizetype kMaximumNameTemplateLength = 120;

        std::filesystem::path fromQt(const QString &location)
        {
            // Qt 的位置为空说明该用途在当前平台没有定义，回落到可写数据目录而不是抛出，
            // 因为调用方拿到空路径会在下一句 create_directories 上失败得更难读
            if (location.isEmpty())
            {
                return {QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdWString()};
            }
            return {location.toStdWString()};
        }

    } // namespace

    std::filesystem::path Paths::applicationDataDirectory()
    {
        return fromQt(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    }

    std::filesystem::path Paths::applicationDirectory()
    {
        // 走 wstring：程序装在中文路径下时，窄串按本地码页解释会装不下而抛
        return std::filesystem::path{QCoreApplication::applicationDirPath().toStdWString()};
    }

    std::filesystem::path Paths::logDirectory()
    {
        return applicationDataDirectory() / "logs";
    }

    std::filesystem::path Paths::cacheDirectory()
    {
        return fromQt(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    }

    std::filesystem::path Paths::unsavedCaptureDirectory()
    {
        return applicationDataDirectory() / "Unsaved";
    }

    std::filesystem::path Paths::defaultScreenshotDirectory()
    {
        return fromQt(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)) / std::string(kApplicationFolder);
    }

    std::filesystem::path Paths::defaultRecordingDirectory()
    {
        return fromQt(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)) / std::string(kApplicationFolder);
    }

    QString Paths::toDisplayText(const std::filesystem::path &path)
    {
        return QString::fromStdWString(path.wstring());
    }

    std::filesystem::path Paths::fromDisplayText(const QString &text)
    {
        return std::filesystem::path{text.toStdWString()};
    }

    Expected<void> Paths::ensureDirectory(const std::filesystem::path &directory)
    {
        std::error_code error;
        std::filesystem::create_directories(directory, error);

        // create_directories 在目录已存在时不报错也不返回 true，所以判据只能是「现在是不是一个目录」
        if (error || !std::filesystem::is_directory(directory))
        {
            return std::unexpected(
                    Error{ErrorCode::FileWriteFailed,
                          GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("目录创建失败：{}（原因 {}）。请检查该路径是否可写，或在设置里换一个目录"),
                                                       toDisplayText(directory).toStdString(), error ? error.message() : GyanisScribe::Base::uiTextUtf8("路径存在但不是目录"))});
        }
        return {};
    }

    QString Paths::portableConfigFileIn(const QString &directory)
    {
        if (directory.trimmed().isEmpty())
        {
            return {};
        }

        const auto marker = QDir{directory}.filePath(QString::fromLatin1(kPortableMarkerFileName.data(), static_cast<qsizetype>(kPortableMarkerFileName.size())));
        if (!QFileInfo::exists(marker))
        {
            return {};
        }
        return QDir{directory}.filePath(QStringLiteral("GyanisScribe.ini"));
    }

    QString Paths::portableConfigFile()
    {
        return portableConfigFileIn(QCoreApplication::applicationDirPath());
    }

    QString Paths::defaultNameTemplate()
    {
        return QString::fromLatin1(kDefaultNameTemplate.data(), static_cast<qsizetype>(kDefaultNameTemplate.size()));
    }

    QString Paths::imageFileName(const QDateTime &moment, const std::string_view extension, const QString &pattern)
    {
        // 模板留空表示默认档：与「目录留空表示默认目录」同一约定，调用方不必各抄一份默认值
        auto effective = pattern.isEmpty() ? defaultNameTemplate() : pattern;
        if (const auto checked = parseNameTemplate(effective); !checked)
        {
            // 走到这里说明调用方没有先校验（设置页那一路会校验）：按默认档写下去并留下原因，
            // 不拿一个带分隔符的名字去写盘——那会写到模板里那个分隔符指去的地方
            Logger::warning(std::format("文件名模板不可用：{}，本次按默认档命名", checked.error().message));
            effective = defaultNameTemplate();
        }

        return formatProductName(moment, effective) + QStringLiteral(".") + QString::fromUtf8(extension.data(), static_cast<qsizetype>(extension.size()));
    }

    Expected<QString> Paths::parseNameTemplate(const QString &text)
    {
        auto trimmed = text.trimmed();
        if (trimmed.isEmpty())
        {
            return std::unexpected(Error{ErrorCode::InvalidArgument, GyanisScribe::Base::uiTextUtf8("文件名模板不能为空：可以填 %date%_%time%，或留空用默认档")});
        }
        if (trimmed.size() > kMaximumNameTemplateLength)
        {
            return std::unexpected(Error{ErrorCode::InvalidArgument,
                                         GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("文件名模板太长（{} 个字，上限 {}）：太长的名字在别的程序里也常常打不开"),
                                                                      trimmed.size(), static_cast<int>(kMaximumNameTemplateLength))});
        }
        for (const auto character: kForbiddenNameCharacters)
        {
            const auto forbidden = QChar::fromLatin1(character);
            if (trimmed.contains(forbidden))
            {
                return std::unexpected(Error{ErrorCode::InvalidArgument,
                                             GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("文件名模板里不能有「{}」：它会把文件写到别的目录，或让系统直接拒绝写盘"),
                                                                          QString{forbidden}.toStdString())});
            }
        }

        // 认变量：一对 % 夹着的若是纯小写字母就当成变量名，表里没有就拒。
        // 不成对的 % 与夹着别的字符的 %…% 都按字面量看——「进度 50%」这种名字是合法的
        for (qsizetype index = 0; index < trimmed.size(); ++index)
        {
            if (trimmed.at(index) != QLatin1Char('%'))
            {
                continue;
            }
            const auto closing = trimmed.indexOf(QLatin1Char('%'), index + 1);
            if (closing < 0)
            {
                break;
            }

            const auto token          = trimmed.mid(index + 1, closing - index - 1);
            const auto looksLikeToken = !token.isEmpty() && std::ranges::all_of(token, [](const QChar character) { return character.isLower(); });
            if (looksLikeToken)
            {
                const auto known = std::ranges::any_of(kNameTemplateTokens, [&token](const auto &entry)
                                                       { return token == QString::fromLatin1(entry.first.data(), static_cast<qsizetype>(entry.first.size())); });
                if (!known)
                {
                    return std::unexpected(Error{ErrorCode::InvalidArgument,
                                                 GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8(
                                                                                      "文件名模板里的变量「%{}%」不认得：可用的只有 %date%（日期）、%time%（时刻）、%ms%（毫秒）"),
                                                                              token.toStdString())});
                }
            }
            index = closing;
        }
        return trimmed;
    }

    QString Paths::formatProductName(const QDateTime &moment, const QString &pattern)
    {
        // 逐格替换而不是把整条模板喂给 QDateTime::toString：模板里的字面量（含中文与下划线）
        // 不必再按时刻格式的记号去转义，用户写什么就写什么
        auto name = pattern;
        for (const auto &[token, timeFormat]: kNameTemplateTokens)
        {
            const auto placeholder = QStringLiteral("%") + QString::fromLatin1(token.data(), static_cast<qsizetype>(token.size())) + QStringLiteral("%");
            name.replace(placeholder, moment.toString(QString::fromLatin1(timeFormat.data(), static_cast<qsizetype>(timeFormat.size()))));
        }
        return name;
    }

    std::filesystem::path Paths::uniqueFilePath(const std::filesystem::path &directory, const QString &fileName)
    {
        // -2 加在扩展名之前：加在后面会得到 name.png-2，双击打不开
        const auto dot    = fileName.lastIndexOf(QLatin1Char('.'));
        const auto stem   = dot > 0 ? fileName.left(dot) : fileName;
        const auto suffix = dot > 0 ? fileName.mid(dot) : QString{};

        auto candidate = directory / fromDisplayText(fileName);
        for (int attempt = 2; attempt <= 99; ++attempt)
        {
            std::error_code ignored;
            if (!std::filesystem::exists(candidate, ignored))
            {
                break;
            }
            candidate = directory / fromDisplayText(QStringLiteral("%1-%2%3").arg(stem).arg(attempt).arg(suffix));
        }
        return candidate;
    }

} // namespace GyanisScribe::Base
