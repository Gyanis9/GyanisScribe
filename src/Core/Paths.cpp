#include "Core/Paths.h"

#include "Core/Text.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QStandardPaths>
#include <QString>

#include <format>
#include <string_view>
#include <system_error>

namespace GyanisScribe::Core
{
    namespace
    {

        /// 落在用户目录下时统一套这一层，避免与别的应用混在同一个 Pictures 里
        constexpr std::string_view kApplicationFolder = "GyanisScribe";

        std::filesystem::path fromQt(const QString &location)
        {
            // Qt 的位置为空说明该用途在当前平台没有定义，回落到可写数据目录而不是抛出，
            // 因为调用方拿到空路径会在下一句 create_directories 上失败得更难读
            if (location.isEmpty())
            {
                return std::filesystem::path(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString());
            }
            return std::filesystem::path(location.toStdString());
        }

    } // namespace

    std::filesystem::path Paths::applicationDataDirectory()
    {
        return fromQt(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    }

    std::filesystem::path Paths::logDirectory()
    {
        return applicationDataDirectory() / "logs";
    }

    std::filesystem::path Paths::cacheDirectory()
    {
        return fromQt(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
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
            return std::unexpected(Error{ErrorCode::FileWriteFailed,
                                         GyanisScribe::Core::uiFormat(GyanisScribe::Core::uiTextUtf8("目录创建失败：{}（原因 {}）。请检查该路径是否可写，或在设置里换一个目录"),
                                                                      directory.string(), error ? error.message() : GyanisScribe::Core::uiTextUtf8("路径存在但不是目录"))});
        }
        return {};
    }

    QString Paths::imageFileName(const QDateTime &moment, const std::string_view extension)
    {
        // 命名格式的唯一出处：贴图保存、标注保存与以后的录屏都走这里，
        // 毫秒位保证同一秒里连续导出两次不会互相覆盖
        const auto stem = moment.toString(QStringLiteral("GyanisScribe_yyyyMMdd_HHmmss_zzz"));
        return stem + QStringLiteral(".") + QString::fromUtf8(extension.data(), static_cast<qsizetype>(extension.size()));
    }

} // namespace GyanisScribe::Core
