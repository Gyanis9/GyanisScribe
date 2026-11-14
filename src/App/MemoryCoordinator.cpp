#include "App/MemoryCoordinator.h"

#include "App/MemoryImportJob.h"
#include "Base/Text.h"
#include "Core/Memory/DuplicateHash.h"
#include "Core/Memory/MemoryIndex.h"
#include "Core/Ocr/IOcrEngine.h"
#include "Core/Ocr/OcrLanguage.h"
#include "Core/Ocr/RecognitionRunner.h"
#include "Core/Settings/ISettingsStore.h"
#include "Core/Settings/SettingsKeys.h"
#include "Platform/IPlatformIntegration.h"
#include "Plugins/OcrEngines/OcrEngineFactory.h"

#include <QDateTime>
#include <QFileInfo>
#include <QStringList>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <system_error>
#include <utility>

namespace GyanisScribe::App
{
    namespace
    {
        /// 路径转界面文本：只走宽字符，避免本地码页装不下的字
        QString displayPath(const std::filesystem::path &path)
        {
            return QString::fromStdWString(path.wstring());
        }

        /// 进程名归一：取文件名、转小写、去掉结尾的 .exe；空串保持空
        QString normalizedProcessName(const QString &name)
        {
            QString normalized = QFileInfo(name).fileName().toLower();
            if (normalized.endsWith(QStringLiteral(".exe")))
            {
                normalized.chop(4);
            }
            return normalized;
        }

        /// 按 UTF-8 字节上限截断，回退到字符边界，避免留下半个多字节序列
        QString truncateOcrText(const QString &text, bool &truncated)
        {
            const QByteArray bytes = text.toUtf8();
            if (bytes.size() <= Core::kMaxOcrTextBytes)
            {
                truncated = false;
                return text;
            }

            qsizetype cut = Core::kMaxOcrTextBytes;
            while (cut > 0 && (static_cast<unsigned char>(bytes.at(cut)) & 0xC0U) == 0x80U)
            {
                --cut;
            }
            truncated = true;
            return QString::fromUtf8(bytes.constData(), cut);
        }

        /// 没有后端对象时的兜底原因
        QString fallbackUnavailableReason()
        {
            return Base::uiText("本机没有可用的文字识别后端：请检查设置中的 OCR 模型目录，或重新安装本程序后重试");
        }

        /// 数据库文件的侧车路径：-wal 与 -shm 跟着主文件换名
        std::filesystem::path sidecarPath(const std::filesystem::path &base, const wchar_t *suffix)
        {
            std::filesystem::path path = base;
            path += suffix;
            return path;
        }

        /// 选一个尚不存在的旧库留档名，避免同一秒内两次重建撞名
        std::filesystem::path uniqueCorruptPath(const std::filesystem::path &databasePath, const QString &timestamp)
        {
            const std::filesystem::path directory = databasePath.parent_path();
            const QString               stem      = QStringLiteral("memory-index.corrupt-%1").arg(timestamp);
            for (int index = 0;; ++index)
            {
                const QString   name      = index == 0 ? stem + QStringLiteral(".sqlite") : QStringLiteral("%1-%2.sqlite").arg(stem).arg(index);
                const auto      candidate = directory / std::filesystem::path{name.toStdWString()};
                std::error_code existsError;
                if (!std::filesystem::exists(candidate, existsError))
                {
                    return candidate;
                }
            }
        }

        /// 文件存在时改名；不存在按成功处理，失败把系统原因带回来
        bool moveFileIfExists(const std::filesystem::path &source, const std::filesystem::path &target, std::string &failure)
        {
            std::error_code existsError;
            const bool      exists = std::filesystem::exists(source, existsError);
            if (existsError)
            {
                failure = existsError.message();
                return false;
            }
            if (!exists)
            {
                return true;
            }

            std::error_code renameError;
            std::filesystem::rename(source, target, renameError);
            if (renameError)
            {
                failure = renameError.message();
                return false;
            }
            return true;
        }
    } // namespace

    MemoryCoordinator::MemoryCoordinator(GyanisScribe::Core::ISettingsStore &settings, GyanisScribe::Platform::IPlatformIntegration &integration,
                                         std::filesystem::path databasePath, const int maxEntries, QObject *parent) :
        QObject(parent), m_settings(settings), m_databasePath(std::move(databasePath)), m_index(m_databasePath), m_capacityLimit(std::max(maxEntries, 0))
    {
        m_engine = GyanisScribe::Plugins::createOcrEngine(m_settings, integration);
        initialize();
    }

    MemoryCoordinator::MemoryCoordinator(GyanisScribe::Core::ISettingsStore &settings, std::filesystem::path databasePath, std::unique_ptr<GyanisScribe::Core::IOcrEngine> engine,
                                         const int maxEntries, QObject *parent) :
        QObject(parent), m_settings(settings), m_databasePath(std::move(databasePath)), m_index(m_databasePath), m_engine(std::move(engine)),
        m_capacityLimit(std::max(maxEntries, 0))
    {
        initialize();
    }

    MemoryCoordinator::~MemoryCoordinator()
    {
        m_importJob.reset();

        // 先断开读图结果的回接再等：等待期间结果落地的话，finished 会打在正在析构的对象上
        QObject::disconnect(&m_prepareWatcher, nullptr, this, nullptr);
        if (m_runner != nullptr)
        {
            QObject::disconnect(m_runner.get(), nullptr, this, nullptr);
        }
        m_prepareWatcher.waitForFinished();
    }

    void MemoryCoordinator::initialize()
    {
        QObject::connect(&m_prepareWatcher, &QFutureWatcher<PreparedImage>::finished, this, &MemoryCoordinator::handlePrepared);
        if (m_engine == nullptr)
        {
            return;
        }

        // 搬运工拿着后端的引用，成员析构按声明反序：m_runner 必须先于 m_engine 回收
        m_runner = std::make_unique<GyanisScribe::Core::RecognitionRunner>(*m_engine);
        QObject::connect(m_runner.get(), &GyanisScribe::Core::RecognitionRunner::succeeded, this,
                         [this](const QImage &, const QString &, const GyanisScribe::Core::OcrResult &result) { handleOcrSucceeded(result); });
        QObject::connect(m_runner.get(), &GyanisScribe::Core::RecognitionRunner::failed, this, &MemoryCoordinator::handleOcrFailed);
    }

    void MemoryCoordinator::start()
    {
        if (m_started)
        {
            return;
        }
        m_started = true;

        if (!ensureIndexOpen())
        {
            return;
        }

        pruneMissing();
        if (isEnabled())
        {
            retryPending();
        } else
        {
            emitStats();
        }
    }

    GyanisScribe::Core::MemoryIndex &MemoryCoordinator::index()
    {
        return m_index;
    }

    bool MemoryCoordinator::isEnabled() const
    {
        return m_settings.boolValue(GyanisScribe::Core::kMemoryEnabled, false);
    }

    bool MemoryCoordinator::needsConsent() const
    {
        return !isEnabled() && !m_settings.boolValue(GyanisScribe::Core::kMemoryConsentAsked, false);
    }

    void MemoryCoordinator::resolveConsent(const bool enabled)
    {
        const auto enabledWrite = m_settings.setValue(GyanisScribe::Core::kMemoryEnabled, enabled);
        if (!enabledWrite)
        {
            emit messageForUser(QString::fromStdString(enabledWrite.error().message), Gui::ToastKind::Danger);
        }
        const auto askedWrite = m_settings.setValue(GyanisScribe::Core::kMemoryConsentAsked, true);
        if (!askedWrite)
        {
            emit messageForUser(QString::fromStdString(askedWrite.error().message), Gui::ToastKind::Danger);
        }

        if (enabled)
        {
            if (!ensureIndexOpen())
            {
                emit changed();
                emitStats();
                return;
            }

            if (m_pendingConsent)
            {
                const Core::MemoryUpsert pending = *std::move(m_pendingConsent);
                m_pendingConsent.reset();
                storeEntry(pending);
            }
            retryPending();
        } else
        {
            m_pendingConsent.reset();
        }

        emit changed();
        emitStats();
        startNextOcr();
    }

    void MemoryCoordinator::setEnabled(const bool enabled)
    {
        const auto enabledWrite = m_settings.setValue(GyanisScribe::Core::kMemoryEnabled, enabled);
        if (!enabledWrite)
        {
            emit messageForUser(QString::fromStdString(enabledWrite.error().message), Gui::ToastKind::Danger);
        }

        if (!enabled)
        {
            // 关掉记忆功能就不再接受任何新索引，正在跑的导入也停在下一块之前
            cancelImport();
            stopQueue();
            m_pendingConsent.reset();
            emit changed();
            emitStats();
            return;
        }

        const auto askedWrite = m_settings.setValue(GyanisScribe::Core::kMemoryConsentAsked, true);
        if (!askedWrite)
        {
            emit messageForUser(QString::fromStdString(askedWrite.error().message), Gui::ToastKind::Danger);
        }

        if (!ensureIndexOpen())
        {
            emit changed();
            emitStats();
            return;
        }

        if (m_pendingConsent)
        {
            const Core::MemoryUpsert pending = *std::move(m_pendingConsent);
            m_pendingConsent.reset();
            storeEntry(pending);
        }
        retryPending();
        emit changed();
        emitStats();
    }

    void MemoryCoordinator::noteScreenshot(const std::filesystem::path &filePath, const QSize &size, const GyanisScribe::Core::CaptureSourceInfo &source)
    {
        const QString processName = normalizedProcessName(source.processName);

        // 排除判断先于授权：被排除的程序既不该触发首次询问，也不该入库
        if (isExcludedProcess(processName))
        {
            if (isEnabled() && !m_excludedNoticeShown)
            {
                m_excludedNoticeShown = true;
                emit messageForUser(Base::uiText("记忆库：该程序在排除名单中，本次截图未建立索引。"), Gui::ToastKind::Information);
            }
            return;
        }

        if (!isEnabled())
        {
            // 授权前只攒一条：等待回答期间再来的截图没有意义，用户回答后只索引第一张
            if (!m_settings.boolValue(GyanisScribe::Core::kMemoryConsentAsked, false) && !m_pendingConsent)
            {
                Core::MemoryUpsert entry;
                entry.filePath    = filePath;
                entry.source      = Core::MemorySource::Capture;
                entry.capturedAt  = QDateTime::currentDateTime();
                entry.size        = size;
                entry.processName = processName;
                entry.windowTitle = source.windowTitle;
                m_pendingConsent  = std::move(entry);
                emit consentRequested();
            }
            return;
        }

        if (!m_index.isOpen())
        {
            if (!m_databaseFailureReported && !m_indexNoticeShown)
            {
                m_indexNoticeShown = true;
                emit messageForUser(Base::uiText("记忆库暂不可用：索引尚未打开，请稍后重试；若问题持续，请在记忆库页重建索引。"), Gui::ToastKind::Danger);
            }
            return;
        }

        Core::MemoryUpsert entry;
        entry.filePath    = filePath;
        entry.source      = Core::MemorySource::Capture;
        entry.capturedAt  = QDateTime::currentDateTime();
        entry.size        = size;
        entry.processName = processName;
        entry.windowTitle = source.windowTitle;
        storeEntry(entry);
    }

    void MemoryCoordinator::importFolder(const std::filesystem::path &directory)
    {
        if (!isEnabled())
        {
            // 用户可能从记忆库页点导入：先走首次授权，回答后再点一次
            emit consentRequested();
            return;
        }

        if (m_importJob)
        {
            emit messageForUser(Base::uiText("记忆库：已有导入任务正在进行，请等待其结束或先取消。"), Gui::ToastKind::Information);
            return;
        }

        if (!ensureIndexOpen())
        {
            return;
        }

        const QFileInfo directoryInfo{displayPath(directory)};
        if (!directoryInfo.exists() || !directoryInfo.isDir())
        {
            emit messageForUser(Base::uiText("记忆库：导入目录 %1 不存在或无法读取，请选择有效的文件夹。").arg(displayPath(directory)), Gui::ToastKind::Danger);
            return;
        }

        m_importEvictedCount = 0;
        m_importJob          = std::make_unique<MemoryImportJob>(m_index);
        MemoryImportJob *job = m_importJob.get();

        QObject::connect(job, &MemoryImportJob::inserted, this, &MemoryCoordinator::enqueueImported);
        QObject::connect(job, &MemoryImportJob::progress, this,
                         [this](const Core::MemoryImportProgress &progress)
                         {
                             emit importProgress(progress);
                             emit changed();
                             emitStats();
                         });
        QObject::connect(job, &MemoryImportJob::finished, this,
                         [this](const Core::MemoryImportReport &report)
                         {
                             // 结束信号是任务的最后一条：排到下一轮事件循环再删，避免在它的成员函数里销毁它
                             MemoryImportJob *finishedJob = m_importJob.release();
                             finishedJob->deleteLater();

                             const int evicted = std::exchange(m_importEvictedCount, 0);

                             emit importFinished(report);
                             emit changed();
                             emitStats();
                             if (evicted > 0)
                             {
                                 // 整个导入合并成一条，逐条提示会把界面刷屏
                                 emit messageForUser(Base::uiText("记忆库已达上限 %1 条：本次导入已移除最早的 %2 条；原图文件不受影响。").arg(m_capacityLimit).arg(evicted),
                                                     Gui::ToastKind::Information);
                             }
                         });

        job->start(directory);
    }

    void MemoryCoordinator::cancelImport()
    {
        if (m_importJob)
        {
            m_importJob->cancel();
        }
    }

    void MemoryCoordinator::setExcludedProcesses(QStringList processes)
    {
        QStringList normalized;
        for (const QString &process: processes)
        {
            const QString name = normalizedProcessName(process);
            if (!name.isEmpty() && !normalized.contains(name))
            {
                normalized.append(name);
            }
        }

        QStringList previous;
        for (const QString &process: m_settings.stringListValue(GyanisScribe::Core::kMemoryExcludeProcesses, QStringList{}))
        {
            const QString name = normalizedProcessName(process);
            if (!name.isEmpty() && !previous.contains(name))
            {
                previous.append(name);
            }
        }

        const auto written = m_settings.setValue(GyanisScribe::Core::kMemoryExcludeProcesses, normalized);
        if (!written)
        {
            emit messageForUser(QString::fromStdString(written.error().message), Gui::ToastKind::Danger);
        }

        if (m_index.isOpen())
        {
            for (const QString &name: normalized)
            {
                if (previous.contains(name))
                {
                    continue;
                }

                const auto removed = m_index.removeProcess(name);
                if (!removed)
                {
                    emit messageForUser(QString::fromStdString(removed.error().message), Gui::ToastKind::Danger);
                }
            }
        }

        dropExcludedWork(normalized);
        emit changed();
        emitStats();
    }

    QStringList MemoryCoordinator::excludedProcesses() const
    {
        QStringList normalized;
        for (const QString &process: m_settings.stringListValue(GyanisScribe::Core::kMemoryExcludeProcesses, QStringList{}))
        {
            const QString name = normalizedProcessName(process);
            if (!name.isEmpty() && !normalized.contains(name))
            {
                normalized.append(name);
            }
        }
        return normalized;
    }

    void MemoryCoordinator::retryOcr(const qint64 id)
    {
        if (!ensureIndexOpen())
        {
            return;
        }
        if (m_running && m_running->id == id)
        {
            return;
        }

        const auto known = findQueueItem(id);
        if (!known)
        {
            emit messageForUser(Base::uiText("记忆库：未找到要重新识别的条目，请刷新列表后重试。"), Gui::ToastKind::Information);
            return;
        }

        const auto reset = m_index.resetOcr(id);
        if (!reset)
        {
            emit messageForUser(QString::fromStdString(reset.error().message), Gui::ToastKind::Danger);
            return;
        }

        removeQueuedById(id);
        m_knownItems[id] = *known;
        if (known->source == Core::MemorySource::Import)
        {
            m_importQueue.push_front(*known);
        } else
        {
            m_captureQueue.push_front(*known);
        }
        emit changed();
        emitStats();
        startNextOcr();
    }

    void MemoryCoordinator::retryPending()
    {
        if (!ensureIndexOpen())
        {
            return;
        }
        refreshOcrAvailability();
        if (!isEnabled())
        {
            return;
        }

        const auto pending = m_index.pendingItems(Core::kMaxMemoryEntries);
        if (!pending)
        {
            emit messageForUser(QString::fromStdString(pending.error().message), Gui::ToastKind::Danger);
            return;
        }

        std::deque<QueueItem> captureQueue;
        std::deque<QueueItem> importQueue;
        for (const Core::MemoryResult &result: *pending)
        {
            // 正在识别的那一条不重复入队；它的结果回来后自己收尾
            if (m_running && result.id == m_running->id)
            {
                continue;
            }

            QueueItem item;
            item.id                 = result.id;
            item.filePath           = result.filePath;
            item.size               = result.size;
            item.source             = result.source;
            item.processName        = result.processName;
            m_knownItems[result.id] = item;
            if (result.source == Core::MemorySource::Import)
            {
                importQueue.push_back(item);
            } else
            {
                captureQueue.push_back(item);
            }
        }

        m_captureQueue = std::move(captureQueue);
        m_importQueue  = std::move(importQueue);
        emitStats();
        startNextOcr();
    }

    void MemoryCoordinator::removePath(const std::filesystem::path &filePath)
    {
        if (!ensureIndexOpen())
        {
            return;
        }

        const auto removed = m_index.removeByPath(filePath);
        if (!removed)
        {
            emit messageForUser(QString::fromStdString(removed.error().message), Gui::ToastKind::Danger);
            return;
        }

        const QString pathKey = Core::MemoryIndex::pathKey(filePath);
        if (m_pendingConsent && Core::MemoryIndex::pathKey(m_pendingConsent->filePath) == pathKey)
        {
            m_pendingConsent.reset();
        }
        removeQueuedByPath(pathKey);
        for (auto entry = m_knownItems.begin(); entry != m_knownItems.end();)
        {
            if (Core::MemoryIndex::pathKey(entry->second.filePath) == pathKey)
            {
                entry = m_knownItems.erase(entry);
            } else
            {
                ++entry;
            }
        }

        emit changed();
        emitStats();
    }

    void MemoryCoordinator::clearCaptureEntries()
    {
        if (!ensureIndexOpen())
        {
            return;
        }

        const auto removed = m_index.removeCaptureEntries();
        if (!removed)
        {
            emit messageForUser(QString::fromStdString(removed.error().message), Gui::ToastKind::Danger);
        }

        m_pendingConsent.reset();
        m_captureQueue.clear();
        if (m_running && m_running->source == Core::MemorySource::Capture)
        {
            m_running.reset();
            m_runningDhash.clear();
        }
        for (auto entry = m_knownItems.begin(); entry != m_knownItems.end();)
        {
            if (entry->second.source == Core::MemorySource::Capture)
            {
                entry = m_knownItems.erase(entry);
            } else
            {
                ++entry;
            }
        }

        emit changed();
        emitStats();
    }

    void MemoryCoordinator::clearAll()
    {
        if (!ensureIndexOpen())
        {
            return;
        }

        // 清空之后不该再有导入把行写回来
        cancelImport();

        const auto cleared = m_index.clear();
        if (!cleared)
        {
            emit messageForUser(QString::fromStdString(cleared.error().message), Gui::ToastKind::Danger);
        }

        m_pendingConsent.reset();
        stopQueue();
        m_knownItems.clear();
        emit changed();
        emitStats();
    }

    void MemoryCoordinator::pruneMissing()
    {
        if (!ensureIndexOpen())
        {
            return;
        }

        const auto removed = m_index.pruneMissing();
        if (!removed)
        {
            emit messageForUser(QString::fromStdString(removed.error().message), Gui::ToastKind::Danger);
            emitStats();
            return;
        }

        if (*removed > 0)
        {
            emit changed();
            emit messageForUser(Base::uiText("记忆库：已清理 %1 条原图已不存在的索引记录；导入的文件请保持原路径。").arg(*removed), Gui::ToastKind::Information);
        }
        emitStats();
    }

    void MemoryCoordinator::rebuildIndex()
    {
        const std::filesystem::path databasePath = m_databasePath;

        // 重建期间不该有导入把行写进旧库
        cancelImport();

        // 先关连接再改名：占用中的主库无法改名，-wal/-shm 也会随关闭合并或删除
        m_index.close();
        m_pendingConsent.reset();
        stopQueue();
        m_knownItems.clear();
        m_databaseFailureReported = false;
        m_indexNoticeShown        = false;

        const QString               timestamp   = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        const std::filesystem::path corruptPath = uniqueCorruptPath(databasePath, timestamp);

        std::string failure;
        if (!moveFileIfExists(databasePath, corruptPath, failure))
        {
            // 旧库还在原位，重新打开后用户仍能搜索
            static_cast<void>(ensureIndexOpen());
            const QString message = Base::uiText("记忆库：重建索引时无法移动旧数据库文件 %1（%2）。旧文件仍可找回，请关闭占用该文件的程序后重试。")
                                            .arg(displayPath(corruptPath), QString::fromStdString(failure));
            reportDatabaseFailure(Base::Error{Base::ErrorCode::FileWriteFailed, message.toStdString()});
            return;
        }

        for (const wchar_t *suffix: {L"-wal", L"-shm"})
        {
            const std::filesystem::path source = sidecarPath(databasePath, suffix);
            const std::filesystem::path target = sidecarPath(corruptPath, suffix);
            if (!moveFileIfExists(source, target, failure))
            {
                const QString message = Base::uiText("记忆库：重建索引时无法移动旧数据库文件 %1（%2）。旧文件仍可找回，请关闭占用该文件的程序后重试。")
                                                .arg(displayPath(target), QString::fromStdString(failure));
                reportDatabaseFailure(Base::Error{Base::ErrorCode::FileWriteFailed, message.toStdString()});
                return;
            }
        }

        if (!ensureIndexOpen())
        {
            return;
        }
        // 重建成功：上次留下的数据库错误已被新库取代，清掉横幅与重建入口
        emit databaseError(QString{});
        emit changed();
        emitStats();
    }

    bool MemoryCoordinator::ensureIndexOpen()
    {
        if (m_index.isOpen())
        {
            return true;
        }

        const auto opened = m_index.open();
        if (!opened)
        {
            reportDatabaseFailure(opened.error());
            return false;
        }

        // 打开成功即代表这一路恢复了：之前报过的错误在这里清掉，界面才不会一直留着重建入口
        const bool recovered      = m_databaseFailureReported;
        m_databaseFailureReported = false;
        m_indexNoticeShown        = false;
        if (recovered)
        {
            emit databaseError(QString{});
        }
        return true;
    }

    void MemoryCoordinator::reportDatabaseFailure(const Base::Error &error)
    {
        if (m_databaseFailureReported)
        {
            return;
        }
        m_databaseFailureReported = true;

        QString message = QString::fromStdString(error.message);
        if (message.isEmpty())
        {
            message = Base::uiText("记忆库的数据库操作失败，请稍后重试；若问题持续，请在记忆库页重建索引。");
        }
        emit databaseError(message);
        emit messageForUser(message, Gui::ToastKind::Danger);
    }

    void MemoryCoordinator::storeEntry(const Core::MemoryUpsert &entry)
    {
        if (!ensureIndexOpen())
        {
            return;
        }

        const auto identifier = m_index.upsertPending(entry);
        if (!identifier)
        {
            emit messageForUser(QString::fromStdString(identifier.error().message), Gui::ToastKind::Danger);
            return;
        }

        // 每次写入后都收一次容量：刚插入的这一行不会被淘汰
        enforceCapacity(*identifier, false);

        QueueItem item;
        item.id          = *identifier;
        item.filePath    = entry.filePath;
        item.size        = entry.size;
        item.source      = entry.source;
        item.processName = entry.processName;

        removeQueuedById(item.id);
        m_knownItems[item.id] = item;
        if (item.source == Core::MemorySource::Import)
        {
            m_importQueue.push_back(item);
        } else
        {
            m_captureQueue.push_back(item);
        }

        emit changed();
        emitStats();
        startNextOcr();
    }

    void MemoryCoordinator::enqueueImported(const qint64 id)
    {
        // 导入任务写完一行就算一次 upsert；淘汰计数留到导入结束合并提示
        enforceCapacity(id, true);

        const auto found = m_index.findById(id);
        if (!found || !found->has_value())
        {
            // 本批里已被容量淘汰或导入期间被删除：不再排队，也不该为它读图识别
            return;
        }

        const Core::MemoryResult &result = **found;
        QueueItem                 item;
        item.id          = result.id;
        item.filePath    = result.filePath;
        item.size        = result.size;
        item.source      = result.source;
        item.processName = result.processName;

        m_knownItems[item.id] = item;
        m_importQueue.push_back(item);
        startNextOcr();
    }

    void MemoryCoordinator::enforceCapacity(const qint64 insertedId, const bool duringImport)
    {
        if (!m_index.isOpen())
        {
            return;
        }

        const auto evicted = m_index.enforceCapacity(m_capacityLimit, insertedId);
        if (!evicted)
        {
            emit messageForUser(QString::fromStdString(evicted.error().message), Gui::ToastKind::Danger);
            return;
        }
        if (evicted->empty())
        {
            return;
        }

        // 被淘汰的行不能再读图识别：从两条队列、运行位与主键映射里一并清掉
        for (const qint64 id: *evicted)
        {
            removeQueuedById(id);
        }

        if (duringImport)
        {
            m_importEvictedCount += static_cast<int>(evicted->size());
            return;
        }

        emit changed();
        emitStats();
        emit messageForUser(Base::uiText("记忆库：条目已达上限 %1 条，已移除最早的 %2 条；原图文件不受影响。").arg(m_capacityLimit).arg(static_cast<int>(evicted->size())),
                            Gui::ToastKind::Information);
    }

    void MemoryCoordinator::dropExcludedWork(const QStringList &excluded)
    {
        if (excluded.isEmpty())
        {
            return;
        }

        const auto matches = [&excluded](const QueueItem &item) { return !item.processName.isEmpty() && excluded.contains(item.processName); };
        m_captureQueue.erase(std::remove_if(m_captureQueue.begin(), m_captureQueue.end(), matches), m_captureQueue.end());
        m_importQueue.erase(std::remove_if(m_importQueue.begin(), m_importQueue.end(), matches), m_importQueue.end());
        if (m_running && matches(*m_running))
        {
            m_running.reset();
            m_runningDhash.clear();
        }

        for (auto entry = m_knownItems.begin(); entry != m_knownItems.end();)
        {
            if (matches(entry->second))
            {
                entry = m_knownItems.erase(entry);
            } else
            {
                ++entry;
            }
        }

        if (m_pendingConsent && !m_pendingConsent->processName.isEmpty() && excluded.contains(m_pendingConsent->processName))
        {
            m_pendingConsent.reset();
        }
    }

    void MemoryCoordinator::startNextOcr()
    {
        if (m_running || m_runnerBusy || m_prepareWatcher.isRunning())
        {
            return;
        }
        if (!isEnabled() || !m_index.isOpen())
        {
            return;
        }

        refreshOcrAvailability();
        if (!m_ocrAvailable || m_runner == nullptr)
        {
            return;
        }

        QueueItem item;
        if (!m_captureQueue.empty())
        {
            item = m_captureQueue.front();
            m_captureQueue.pop_front();
        } else if (!m_importQueue.empty())
        {
            item = m_importQueue.front();
            m_importQueue.pop_front();
        } else
        {
            return;
        }

        m_running = item;
        m_runningDhash.clear();

        const std::filesystem::path filePath = item.filePath;
        m_prepareWatcher.setFuture(QtConcurrent::run(
                [filePath]
                {
                    PreparedImage prepared;
                    QImage        image;
                    if (image.load(displayPath(filePath)) && !image.isNull())
                    {
                        prepared.image    = image;
                        prepared.dhash    = Core::computeDHash(image);
                        prepared.readable = true;
                    }
                    return prepared;
                }));
    }

    void MemoryCoordinator::handlePrepared()
    {
        if (!m_running)
        {
            startNextOcr();
            return;
        }

        const QueueItem     item     = *m_running;
        const PreparedImage prepared = m_prepareWatcher.result();
        if (!prepared.readable)
        {
            writeOcrFailure(item.id, Base::uiText("记忆库：读取截图失败，原文件可能已被移动或删除；可稍后在记忆库页重新识别。"));
            finishTask();
            return;
        }

        refreshOcrAvailability();
        if (!m_ocrAvailable || m_runner == nullptr)
        {
            // 保持待识别，等后端恢复后由重试待处理重新入队
            m_running.reset();
            m_runningDhash.clear();
            emitStats();
            return;
        }

        m_runningDhash = prepared.dhash;
        m_runnerBusy   = true;
        m_runner->request(prepared.image, ocrLanguage());
    }

    void MemoryCoordinator::handleOcrSucceeded(const GyanisScribe::Core::OcrResult &result)
    {
        m_runnerBusy = false;
        if (!m_running)
        {
            startNextOcr();
            return;
        }

        const QueueItem item      = *m_running;
        bool            truncated = false;
        const QString   text      = truncateOcrText(result.text(), truncated);
        writeOcr(item.id, text, m_runningDhash, QStringLiteral("done"), QString{}, truncated);
        finishTask();
    }

    void MemoryCoordinator::handleOcrFailed(const Base::Error &error)
    {
        m_runnerBusy = false;
        if (!m_running)
        {
            startNextOcr();
            return;
        }

        const QueueItem item = *m_running;
        writeOcrFailure(item.id, QString::fromStdString(error.message));
        refreshOcrAvailability();
        finishTask();
    }

    void MemoryCoordinator::writeOcr(const qint64 id, QString text, QString dhash, QString status, QString error, const bool truncated)
    {
        if (!m_index.isOpen())
        {
            return;
        }

        const auto updated = m_index.updateOcr(id, std::move(text), std::move(dhash), std::move(status), std::move(error), truncated);
        if (!updated)
        {
            emit messageForUser(QString::fromStdString(updated.error().message), Gui::ToastKind::Danger);
        }
    }

    void MemoryCoordinator::writeOcrFailure(const qint64 id, QString error)
    {
        if (!m_index.isOpen())
        {
            return;
        }

        const auto updated = m_index.markOcrFailed(id, std::move(error));
        if (!updated)
        {
            emit messageForUser(QString::fromStdString(updated.error().message), Gui::ToastKind::Danger);
        }
    }

    void MemoryCoordinator::finishTask()
    {
        m_running.reset();
        m_runningDhash.clear();
        emit changed();
        emitStats();
        startNextOcr();
    }

    void MemoryCoordinator::stopQueue()
    {
        m_captureQueue.clear();
        m_importQueue.clear();
        m_running.reset();
        m_runningDhash.clear();
    }

    void MemoryCoordinator::removeQueuedByPath(const QString &pathKey)
    {
        const auto matches = [&pathKey](const QueueItem &item) { return Core::MemoryIndex::pathKey(item.filePath) == pathKey; };
        m_captureQueue.erase(std::remove_if(m_captureQueue.begin(), m_captureQueue.end(), matches), m_captureQueue.end());
        m_importQueue.erase(std::remove_if(m_importQueue.begin(), m_importQueue.end(), matches), m_importQueue.end());
        if (m_running && matches(*m_running))
        {
            m_running.reset();
            m_runningDhash.clear();
        }
    }

    void MemoryCoordinator::removeQueuedById(const qint64 id)
    {
        const auto matches = [id](const QueueItem &item) { return item.id == id; };
        m_captureQueue.erase(std::remove_if(m_captureQueue.begin(), m_captureQueue.end(), matches), m_captureQueue.end());
        m_importQueue.erase(std::remove_if(m_importQueue.begin(), m_importQueue.end(), matches), m_importQueue.end());
        if (m_running && m_running->id == id)
        {
            m_running.reset();
            m_runningDhash.clear();
        }
        m_knownItems.erase(id);
    }

    std::optional<MemoryCoordinator::QueueItem> MemoryCoordinator::findQueueItem(const qint64 id) const
    {
        const auto known = m_knownItems.find(id);
        if (known != m_knownItems.end())
        {
            return known->second;
        }
        for (const QueueItem &item: m_captureQueue)
        {
            if (item.id == id)
            {
                return item;
            }
        }
        for (const QueueItem &item: m_importQueue)
        {
            if (item.id == id)
            {
                return item;
            }
        }

        // 界面传来的主键可能来自更早的搜索结果：按主键回查，不受单页搜索条数限制
        const auto found = m_index.findById(id);
        if (found && found->has_value())
        {
            const Core::MemoryResult &result = **found;
            QueueItem                 item;
            item.id          = result.id;
            item.filePath    = result.filePath;
            item.size        = result.size;
            item.source      = result.source;
            item.processName = result.processName;
            return item;
        }
        return std::nullopt;
    }

    void MemoryCoordinator::refreshOcrAvailability()
    {
        const bool available = m_engine != nullptr && m_runner != nullptr && m_engine->isAvailable();
        if (available == m_ocrAvailable)
        {
            return;
        }

        QString reason;
        if (!available)
        {
            reason = m_engine != nullptr ? m_engine->unavailableReason() : QString{};
            if (reason.isEmpty())
            {
                reason = fallbackUnavailableReason();
            }
        }
        m_ocrAvailable = available;
        emit ocrAvailabilityChanged(available, available ? QString{} : reason);
    }

    void MemoryCoordinator::emitStats()
    {
        if (!m_index.isOpen())
        {
            return;
        }

        const int  excluded = static_cast<int>(m_settings.stringListValue(GyanisScribe::Core::kMemoryExcludeProcesses, QStringList{}).size());
        const auto stats    = m_index.stats(excluded);
        if (stats)
        {
            emit statsChanged(*stats);
        }
    }

    bool MemoryCoordinator::isExcludedProcess(const QString &processName) const
    {
        if (processName.isEmpty())
        {
            // 空进程名是导入条目的标记，不受排除名单影响
            return false;
        }

        const QStringList excluded = m_settings.stringListValue(GyanisScribe::Core::kMemoryExcludeProcesses, QStringList{});
        for (const QString &entry: excluded)
        {
            if (normalizedProcessName(entry) == processName)
            {
                return true;
            }
        }
        return false;
    }

    QString MemoryCoordinator::ocrLanguage() const
    {
        return m_settings.stringValue(GyanisScribe::Core::kOcrLanguage, QString::fromUtf8(GyanisScribe::Core::kDefaultOcrLanguage));
    }

} // namespace GyanisScribe::App
