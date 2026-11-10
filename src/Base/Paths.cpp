#include "Base/Paths.h"

#include "Base/Logger.h"
#include "Base/Text.h"

#include <QCoreApplication>
#include <QChar>
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

        /// 文件名模板的时刻变量表：变量名 → 时刻格式
        constexpr std::array<std::pair<std::string_view, std::string_view>, 3> kNameTemplateTokens{
                std::pair{"date", "yyyyMMdd"},
                std::pair{"time", "HHmmss"},
                std::pair{"ms", "zzz"},
        };

        /// 文件名模板的截图上下文变量：变量名与取值为空时的回落
        struct NameTemplateContextToken
        {
            std::string_view name;     ///< 模板里的变量名
            std::string_view fallback; ///< 上下文值为空时的回落
        };

        /// 上下文变量表：每项的回落写在第二列，避免解析与拼名两处各抄一份
        constexpr std::array<NameTemplateContextToken, 3> kNameTemplateContextTokens{
                NameTemplateContextToken{"app", "app"},
                NameTemplateContextToken{"title", "window"},
                NameTemplateContextToken{"monitor", "screen"},
        };

        /// 单个上下文值进入文件名后的长度上限：太长的窗口标题会把整个文件名撑到别的程序打不开
        constexpr qsizetype kMaximumNamePartLength = 40;

        /// 名字里不许出现的字符：前两个会把文件写到别的目录，其余是 Windows 的保留字符
        constexpr std::string_view kForbiddenNameCharacters = "/\\:*?\"<>|";

        /// 模板长度上限：太长的名字在别的程序里也常常打不开
        constexpr qsizetype kMaximumNameTemplateLength = 120;

        /// 撞名时依次尝试的编号上限：再往下只是把编号拉长，换时刻后缀更有用
        constexpr int kMaximumUniqueAttempts = 999;

        /// Windows 的保留设备名：带不带扩展名都会被当成设备，写盘直接失败
        constexpr std::array<std::string_view, 22> kReservedDeviceNames{
                "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
                "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
        };

        /// 首个扩展名之前的那一段是不是保留设备名；比较不分大小写，也不看有没有扩展名
        bool isReservedDeviceName(const QString &fileName)
        {
            const auto dot  = fileName.indexOf(QLatin1Char('.'));
            const auto stem = (dot >= 0 ? fileName.left(dot) : fileName).trimmed();
            return std::ranges::any_of(kReservedDeviceNames,
                                       [&stem](const std::string_view reserved)
                                       {
                                           return stem.compare(QString::fromLatin1(reserved.data(), static_cast<qsizetype>(reserved.size())), Qt::CaseInsensitive) == 0;
                                       });
        }

        /// 取上下文变量对应的原始值；只对 kNameTemplateContextTokens 里的三个名字调用
        QString contextValueFor(const std::string_view token, const NameTemplateContext &context)
        {
            if (token == "app")
            {
                return context.application;
            }
            if (token == "title")
            {
                return context.windowTitle;
            }
            return context.monitor;
        }

        /// 把上下文值清洗成文件名可用的片段：控制字符、保留字符与空白折成下划线，首尾的 _、空格与点去掉
        QString sanitizeNamePart(const QString &value, const std::string_view fallback)
        {
            QString    sanitized;
            const auto appendCharacter = [&sanitized](const QChar character)
            {
                // 连续下划线压成一个：用户写下的 _ 与替换产生的 _ 按同一条规则合并
                if (character == QLatin1Char('_') && sanitized.endsWith(QLatin1Char('_')))
                {
                    return;
                }
                sanitized.append(character);
            };
            sanitized.reserve(value.size());

            for (const QChar character: value)
            {
                const auto isControl   = character.category() == QChar::Other_Control;
                const auto isForbidden = character.unicode() < 0x80 &&
                                         kForbiddenNameCharacters.find(static_cast<char>(character.unicode())) != std::string_view::npos;
                if (isControl || isForbidden || character.isSpace())
                {
                    // 三类字符都落成同一个分隔符，连写的保留字符与空白串因此不会留下连续下划线
                    appendCharacter(QLatin1Char('_'));
                    continue;
                }
                appendCharacter(character);
            }

            // 首尾的 _、空格与点都不能留在名字里：末尾的点会被 Windows 当成扩展名分隔，下划线只是难看
            const auto isTrimmable = [](const QChar character)
            {
                return character == QLatin1Char('_') || character == QLatin1Char(' ') || character == QLatin1Char('.');
            };
            while (!sanitized.isEmpty() && isTrimmable(sanitized.front()))
            {
                sanitized.remove(0, 1);
            }
            while (!sanitized.isEmpty() && isTrimmable(sanitized.back()))
            {
                sanitized.chop(1);
            }

            if (sanitized.isEmpty())
            {
                return QString::fromLatin1(fallback.data(), static_cast<qsizetype>(fallback.size()));
            }
            return sanitized.left(kMaximumNamePartLength);
        }

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
        return imageFileName(moment, extension, pattern, NameTemplateContext{});
    }

    QString Paths::imageFileName(const QDateTime &moment, const std::string_view extension, const QString &pattern, const NameTemplateContext &context)
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

        // 上下文值洗出来的名字可能正好是 CON 这类设备名；先补下划线，别等写盘时才报系统错误
        auto productName = formatProductName(moment, effective, context);
        if (isReservedDeviceName(productName))
        {
            productName.prepend(QLatin1Char('_'));
        }

        return productName + QStringLiteral(".") + QString::fromUtf8(extension.data(), static_cast<qsizetype>(extension.size()));
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
        // Windows 把 CON、AUX、COM1 这类名字当设备，带扩展名也写不出文件；模板这一层就拒掉，别等保存时才报系统错误
        if (isReservedDeviceName(trimmed))
        {
            return std::unexpected(Error{ErrorCode::InvalidArgument,
                                         GyanisScribe::Base::uiTextUtf8("文件名模板用到了 Windows 的保留设备名（CON、PRN、AUX、NUL、COM1 到 COM9、LPT1 到 LPT9）："
                                                                        "这类名字写不出文件，请在名字里加几个字或换成别的写法")});
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
                const auto matchesToken = [&token](const std::string_view name)
                { return token == QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size())); };
                const auto knownTimeToken = std::ranges::any_of(kNameTemplateTokens, [&matchesToken](const auto &entry) { return matchesToken(entry.first); });
                const auto knownContextToken =
                        std::ranges::any_of(kNameTemplateContextTokens, [&matchesToken](const auto &entry) { return matchesToken(entry.name); });
                if (!knownTimeToken && !knownContextToken)
                {
                    return std::unexpected(Error{ErrorCode::InvalidArgument,
                                                 GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8(
                                                                                      "文件名模板里的变量「%{}%」不认得：可用的只有 %date%（日期）、%time%（时刻）、%ms%（毫秒）、"
                                                                                      "%app%（应用名）、%title%（窗口标题）、%monitor%（屏幕名）"),
                                                                              token.toStdString())});
                }
            }
            index = closing;
        }
        return trimmed;
    }

    QString Paths::formatProductName(const QDateTime &moment, const QString &pattern)
    {
        return formatProductName(moment, pattern, NameTemplateContext{});
    }

    QString Paths::formatProductName(const QDateTime &moment, const QString &pattern, const NameTemplateContext &context)
    {
        // 逐格替换而不是把整条模板喂给 QDateTime::toString：模板里的字面量（含中文与下划线）
        // 不必再按时刻格式的记号去转义，用户写什么就写什么
        auto name = pattern;
        for (const auto &[token, timeFormat]: kNameTemplateTokens)
        {
            const auto placeholder = QStringLiteral("%") + QString::fromLatin1(token.data(), static_cast<qsizetype>(token.size())) + QStringLiteral("%");
            name.replace(placeholder, moment.toString(QString::fromLatin1(timeFormat.data(), static_cast<qsizetype>(timeFormat.size()))));
        }

        // 上下文变量在时刻变量之后替换：清洗后的窗口标题里若出现 %date% 字样，也不会被当成模板变量再展开一次
        for (const auto &[token, fallback]: kNameTemplateContextTokens)
        {
            const auto placeholder = QStringLiteral("%") + QString::fromLatin1(token.data(), static_cast<qsizetype>(token.size())) + QStringLiteral("%");
            name.replace(placeholder, sanitizeNamePart(contextValueFor(token, context), fallback));
        }
        return name;
    }

    Expected<QString> Paths::previewProductName(const QString &pattern, const QDateTime &moment, const NameTemplateContext &context)
    {
        // 空模板按默认档预览：设置页清空输入框时显示的应与实际保存出来的名字一致
        const auto effective = pattern.isEmpty() ? defaultNameTemplate() : pattern;
        const auto checked   = parseNameTemplate(effective);
        if (!checked)
        {
            return std::unexpected(checked.error());
        }

        // 预览与实际保存同解：保留设备名同样补下划线，用户看到的就是将要落盘的主名
        auto productName = formatProductName(moment, *checked, context);
        if (isReservedDeviceName(productName))
        {
            productName.prepend(QLatin1Char('_'));
        }
        return productName;
    }

    std::filesystem::path Paths::uniqueFilePath(const std::filesystem::path &directory, const QString &fileName)
    {
        // 保留设备名先加一个下划线：CON.png 这类名字在 Windows 上写不出去，换一个能落盘的名字比失败有用
        auto effectiveName = fileName;
        if (isReservedDeviceName(effectiveName))
        {
            effectiveName.prepend(QLatin1Char('_'));
        }

        // -2 加在扩展名之前：加在后面会得到 name.png-2，双击打不开
        const auto dot    = effectiveName.lastIndexOf(QLatin1Char('.'));
        const auto stem   = dot > 0 ? effectiveName.left(dot) : effectiveName;
        const auto suffix = dot > 0 ? effectiveName.mid(dot) : QString{};

        auto candidate = directory / fromDisplayText(effectiveName);
        // attempt 记的是「这一次看的是第几个候选」：1 是原名，之后每次撞名换下一个编号。
        // 上限写进循环条件里，-999 这一格必须真的被试到，不能建出来就丢掉
        for (int attempt = 1; attempt <= kMaximumUniqueAttempts; ++attempt)
        {
            std::error_code ignored;
            if (!std::filesystem::exists(candidate, ignored))
            {
                return candidate;
            }
            candidate = directory / fromDisplayText(QStringLiteral("%1-%2%3").arg(stem).arg(attempt + 1).arg(suffix));
        }

        // 编号试到上限仍撞名：改用时刻后缀，名字长一点也好过交回一个已经存在的文件
        const auto stamp    = QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
        auto       fallback = directory / fromDisplayText(QStringLiteral("%1-%2%3").arg(stem, stamp, suffix));
        std::error_code ignored;
        for (int attempt = 2; std::filesystem::exists(fallback, ignored); ++attempt)
        {
            fallback = directory / fromDisplayText(QStringLiteral("%1-%2-%3%4").arg(stem, stamp).arg(attempt).arg(suffix));
        }
        return fallback;
    }

} // namespace GyanisScribe::Base
