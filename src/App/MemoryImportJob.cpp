#include "App/MemoryImportJob.h"

#include "Core/Memory/MemoryIndex.h"

#include <QDateTime>
#include <QDirIterator>
#include <QFileInfo>
#include <QImageReader>
#include <QSize>
#include <QTimer>

#include <system_error>
#include <utility>

namespace GyanisScribe::App
{
    namespace
    {
        /// 一块处理的文件数：够摊薄事件循环往返，又不让界面一次卡太久
        constexpr int kChunkSize = 25;

        /// 每扫描这么多个文件发一次进度
        constexpr int kProgressInterval = 25;

        /// 单文件上限：超过的只看大小，不读内容
        constexpr qint64 kMaxImportFileBytes = 100LL * 1024 * 1024;

        /// 路径转界面文本：只走宽字符，避免本地码页装不下的字
        QString displayPath(const std::filesystem::path &path)
        {
            return QString::fromStdWString(path.wstring());
        }

        /// 扩展名是否在导入范围内，比较不区分大小写
        bool hasImportSuffix(const QString &filePath)
        {
            static const QStringList suffixes{
                    QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("bmp"), QStringLiteral("webp"),
            };
            return suffixes.contains(QFileInfo{filePath}.suffix().toLower());
        }
    } // namespace

    MemoryImportJob::MemoryImportJob(Core::MemoryIndex &index, QObject *parent) : QObject(parent), m_index(index)
    {
    }

    MemoryImportJob::~MemoryImportJob() = default;

    void MemoryImportJob::start(const std::filesystem::path &directory)
    {
        if (m_running || m_finished)
        {
            return;
        }

        m_progress      = {};
        m_sinceProgress = 0;
        m_cancelled     = false;
        m_running       = true;

        const QFileInfo directoryInfo{displayPath(directory)};
        if (!directoryInfo.exists() || !directoryInfo.isDir())
        {
            // 目录不可读时以零读数收尾，调用方负责给用户一句可操作的提示
            QTimer::singleShot(0, this, &MemoryImportJob::finish);
            return;
        }

        m_iterator = std::make_unique<QDirIterator>(directoryInfo.absoluteFilePath(), QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        QTimer::singleShot(0, this, &MemoryImportJob::processChunk);
    }

    void MemoryImportJob::cancel()
    {
        if (m_running)
        {
            m_cancelled = true;
        }
    }

    void MemoryImportJob::processChunk()
    {
        if (!m_running)
        {
            return;
        }

        int processed = 0;
        while (!m_cancelled && processed < kChunkSize && m_iterator != nullptr && m_iterator->hasNext())
        {
            const QString filePath = m_iterator->next();
            ++processed;
            if (!hasImportSuffix(filePath))
            {
                // 不是支持的图片就完全忽略：既不计数，也不读大小
                continue;
            }

            ++m_progress.scanned;
            ++m_sinceProgress;
            processFile(filePath);

            if (m_sinceProgress >= kProgressInterval)
            {
                m_sinceProgress = 0;
                emit progress(m_progress);
            }
        }

        if (m_cancelled || m_iterator == nullptr || !m_iterator->hasNext())
        {
            finish();
            return;
        }
        QTimer::singleShot(0, this, &MemoryImportJob::processChunk);
    }

    void MemoryImportJob::processFile(const QString &filePath)
    {
        const QFileInfo info{filePath};
        if (!info.isFile() || info.size() < 0 || info.size() > kMaxImportFileBytes)
        {
            ++m_progress.skipped;
            return;
        }

        // 只读尺寸，不解码整张图；解压炸弹与坏文件都在这里被挡住
        QImageReader reader{filePath};
        const QSize  size = reader.size();
        if (!size.isValid() || size.isEmpty())
        {
            ++m_progress.skipped;
            return;
        }

        const QString               canonical = info.canonicalFilePath();
        const std::filesystem::path path{(canonical.isEmpty() ? info.absoluteFilePath() : canonical).toStdWString()};

        const auto alreadyIndexed = m_index.containsPath(path);
        if (!alreadyIndexed)
        {
            ++m_progress.failed;
            return;
        }
        if (*alreadyIndexed)
        {
            ++m_progress.skipped;
            return;
        }

        Core::MemoryUpsert entry;
        entry.filePath   = path;
        entry.source     = Core::MemorySource::Import;
        entry.capturedAt = info.lastModified();
        if (!entry.capturedAt.isValid())
        {
            entry.capturedAt = QDateTime::currentDateTime();
        }
        entry.size = size;

        const auto identifier = m_index.upsertPending(entry);
        if (!identifier)
        {
            ++m_progress.failed;
            return;
        }

        ++m_progress.inserted;
        emit inserted(*identifier);
    }

    void MemoryImportJob::finish()
    {
        if (m_finished)
        {
            return;
        }
        m_finished = true;
        m_running  = false;
        m_iterator.reset();

        m_progress.cancelled = m_cancelled;
        emit progress(m_progress);

        Core::MemoryImportReport report;
        report.scanned   = m_progress.scanned;
        report.inserted  = m_progress.inserted;
        report.skipped   = m_progress.skipped;
        report.failed    = m_progress.failed;
        report.cancelled = m_cancelled;
        emit finished(report);
    }
} // namespace GyanisScribe::App
