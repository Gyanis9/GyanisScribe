#include "Core/ClipboardFile.h"

#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"

#include <QByteArray>
#include <QClipboard>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QMimeData>
#include <QString>
#include <QUrl>

namespace GyanisScribe::Core
{
    namespace
    {
        /// 文件名主干：内容 SHA-256 的前 16 位十六进制（同一张图复用同一个文件）
        QString contentStemOf(const QImage &image)
        {
            // 先归一到同一档格式：预乘与非预乘的同一张图，直接哈希原始字节会得到两个主干
            const QImage     normalized = image.convertToFormat(QImage::Format_ARGB32);
            const QByteArray bytes{reinterpret_cast<const char *>(normalized.constBits()), static_cast<qsizetype>(normalized.sizeInBytes())};
            return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().left(16));
        }
    } // namespace

    Base::Expected<std::filesystem::path> copyImageAsFile(const QImage &image, const std::filesystem::path &directory)
    {
        if (image.isNull())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, GyanisScribe::Base::uiTextUtf8("这张图是空的，没有可复制成文件的内容")});
        }

        if (const auto ensured = Base::Paths::ensureDirectory(directory); !ensured)
        {
            return std::unexpected(ensured.error());
        }

        // 输出路径交给 Paths 那一处换算：窄串与宽串的转换只在它那儿发生
        const auto filePath = directory / Base::Paths::fromDisplayText(contentStemOf(image) + QStringLiteral(".png"));
        if (!image.save(Base::Paths::toDisplayText(filePath), "PNG"))
        {
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed,
                                               GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("复制为文件失败：{} 写不下去。请检查这个目录是否可写"),
                                                                            Base::Paths::toDisplayText(filePath).toStdString())});
        }

        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(Base::Paths::toDisplayText(filePath))});
        // 交给剪贴板之后载荷归它持有：Qt 在 Windows 上会把本地文件 URL 发布成 CF_HDROP，
        // 资源管理器与聊天窗口由此能直接吃下这一份
        QGuiApplication::clipboard()->setMimeData(mime);

        Base::Logger::info(std::format("复制为文件：{}", Base::Paths::toDisplayText(filePath).toStdString()));
        return filePath;
    }

} // namespace GyanisScribe::Core
