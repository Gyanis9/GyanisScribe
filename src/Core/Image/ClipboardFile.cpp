#include "Core/Image/ClipboardFile.h"

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

#include <algorithm>
#include <format>
#include <functional>
#include <vector>

namespace GyanisScribe::Core
{
    namespace
    {
        /// 中转目录最多留多少份 PNG：再多只是占盘，最近复制的一批覆盖日常粘贴
        constexpr std::size_t kClipboardCacheLimit = 64;

        /**
         * @brief 清掉中转目录里超量的旧 PNG
         * @param directory 中转目录
         * @param keepPath 刚写入的那一份，永远不删
         * @details 按最后写入时间保留最新的一批；删除失败只记 warning，已经交出的文件不受影响。
         */
        void pruneClipboardDirectory(const std::filesystem::path &directory, const std::filesystem::path &keepPath)
        {
            struct Candidate
            {
                std::filesystem::path           path;
                std::filesystem::file_time_type writeTime;
            };

            std::error_code             iterationError;
            std::vector<Candidate>      candidates;
            const std::filesystem::path keepName = keepPath.filename();
            for (std::filesystem::directory_iterator iterator{directory, iterationError}, end; !iterationError && iterator != end; iterator.increment(iterationError))
            {
                std::error_code typeError;
                if (!iterator->is_regular_file(typeError) || typeError || iterator->path().extension() != ".png")
                {
                    continue;
                }
                std::error_code timeError;
                const auto      writeTime = std::filesystem::last_write_time(iterator->path(), timeError);
                if (timeError)
                {
                    // 读不出时间就无法判断新旧：留着不动，宁可多一份也不误删
                    continue;
                }
                candidates.push_back(Candidate{iterator->path(), writeTime});
            }
            if (iterationError)
            {
                Base::Logger::warning(std::format("剪贴板中转目录读不完，这次没有清理：{}（{}）", Base::Paths::toDisplayText(directory).toStdString(), iterationError.message()));
                return;
            }

            std::ranges::sort(candidates, std::greater{}, &Candidate::writeTime);
            std::size_t kept = 0;
            for (const auto &candidate: candidates)
            {
                // 刚写的那份单独放行：即使时间戳异常落到队尾，也不能把它从剪贴板路径下删掉
                const bool isJustWritten = candidate.path.filename() == keepName;
                if (isJustWritten || kept < kClipboardCacheLimit)
                {
                    ++kept;
                    continue;
                }
                std::error_code removeError;
                std::filesystem::remove(candidate.path, removeError);
                if (removeError)
                {
                    Base::Logger::warning(std::format("剪贴板中转文件删不掉，先留着：{}（{}）", Base::Paths::toDisplayText(candidate.path).toStdString(), removeError.message()));
                }
            }
        }

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
        auto filePath = directory / Base::Paths::fromDisplayText(contentStemOf(image) + QStringLiteral(".png"));
        if (!image.save(Base::Paths::toDisplayText(filePath), "PNG"))
        {
            return std::unexpected(Base::Error{Base::ErrorCode::FileWriteFailed,
                                               GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("复制为文件失败：{} 写不下去。请检查这个目录是否可写"),
                                                                            Base::Paths::toDisplayText(filePath).toStdString())});
        }
        // 写盘成功后才清理：旧图留在中转目录只增不减，会把应用数据目录撑大
        pruneClipboardDirectory(directory, filePath);

        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(Base::Paths::toDisplayText(filePath))});
        // 交给剪贴板之后载荷归它持有：Qt 在 Windows 上会把本地文件 URL 发布成 CF_HDROP，
        // 资源管理器与聊天窗口由此能直接吃下这一份
        QGuiApplication::clipboard()->setMimeData(mime);

        Base::Logger::info(std::format("复制为文件：{}", Base::Paths::toDisplayText(filePath).toStdString()));
        return filePath;
    }

} // namespace GyanisScribe::Core
