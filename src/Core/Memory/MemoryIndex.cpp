#include "Core/Memory/MemoryIndex.h"

#include "Base/Paths.h"
#include "Core/Memory/MemoryToken.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

#include <algorithm>
#include <atomic>
#include <expected>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace GyanisScribe::Core
{
    namespace
    {
        /// 当前 schema 版本，写在 PRAGMA user_version 里
        constexpr int kSchemaVersion = 1;

        /// 查询文本上限；超出部分在构造 MATCH 串之前丢弃
        constexpr qsizetype kMaximumQueryCharacters = 256;

        /// OCR 错误文本按 UTF-8 字节计的上限
        constexpr qsizetype kMaximumOcrErrorBytes = 2048;

        /// 搜索摘要取 OCR 原文的字符数
        constexpr qsizetype kSnippetCharacters = 120;

        /// 每个 MemoryIndex 独占一个 QSQLITE 连接，避免多实例互相顶掉
        std::atomic<quint64> s_connectionCounter{1};

        QString fromPath(const std::filesystem::path &path)
        {
            return QString::fromStdWString(path.wstring());
        }

        std::filesystem::path toPath(const QString &text)
        {
            return std::filesystem::path{text.toStdWString()};
        }

        /// 空 QString 会被 QSQLITE 绑成 NULL，而文本列都是 NOT NULL，统一换成空串
        QString nonNull(const QString &text)
        {
            return text.isNull() ? QStringLiteral("") : text;
        }

        const QString &unknownProcessFilter()
        {
            static const QString value = QString::fromLatin1(kUnknownProcessFilter.data(), static_cast<qsizetype>(kUnknownProcessFilter.size()));
            return value;
        }

        Base::Error makeError(const Base::ErrorCode code, const QString &reason)
        {
            return Base::Error{code, QStringLiteral("记忆索引：%1 请稍后重试；若问题持续，请检查应用数据目录的写入权限，或在记忆库页重建索引。").arg(reason).toStdString()};
        }

        Base::Error notOpenError()
        {
            return Base::Error{Base::ErrorCode::InternalError, QStringLiteral("记忆索引：数据库尚未打开，请先打开记忆库再重试。").toStdString()};
        }

        Base::Error queryError(const QString &reason, const QSqlError &error)
        {
            const QString detail = error.text().trimmed();
            if (detail.isEmpty())
            {
                return makeError(Base::ErrorCode::InternalError, reason);
            }
            return makeError(Base::ErrorCode::InternalError, QStringLiteral("%1（%2）").arg(reason, detail));
        }

        QSqlDatabase databaseFor(const QString &connectionName)
        {
            return QSqlDatabase::database(connectionName, false);
        }

        void closeConnection(const QString &connectionName)
        {
            if (!QSqlDatabase::contains(connectionName))
            {
                return;
            }
            {
                QSqlDatabase database = QSqlDatabase::database(connectionName, false);
                if (database.isOpen())
                {
                    database.close();
                }
            }
            QSqlDatabase::removeDatabase(connectionName);
        }

        qint64 fileBytes(const std::filesystem::path &path)
        {
            std::error_code error;
            const auto      bytes = std::filesystem::file_size(path, error);
            return error ? 0 : static_cast<qint64>(bytes);
        }

        /// 进程名去掉 .exe 的可读名；导入条目的空进程名保持为空
        QString readableProcessName(const QString &processName)
        {
            QString name = QFileInfo(processName).fileName();
            if (name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
            {
                name.chop(4);
            }
            return name;
        }

        /// 拼可检索文本：空部分不参与，统一交给中文感知的预处理
        QString buildSearchText(const QStringList &parts)
        {
            QStringList present;
            present.reserve(parts.size());
            for (const QString &part: parts)
            {
                if (!part.isEmpty())
                {
                    present.append(part);
                }
            }
            return nonNull(prepareFtsText(present.join(QLatin1Char(' '))));
        }

        /// 按 UTF-8 字节上限截断，回退到字符边界，避免留下半个多字节序列
        QString truncateUtf8(const QString &text, const qsizetype maximumBytes, bool &truncated)
        {
            const QByteArray bytes = text.toUtf8();
            if (bytes.size() <= maximumBytes)
            {
                return text;
            }

            qsizetype cut = maximumBytes;
            while (cut > 0 && cut < bytes.size() && (static_cast<unsigned char>(bytes.at(cut)) & 0xC0U) == 0x80U)
            {
                --cut;
            }
            truncated = true;
            return QString::fromUtf8(bytes.constData(), cut);
        }

        /// 卡片摘要：原文前 120 字符，空白压成单空格
        QString snippetOf(const QString &ocrText)
        {
            const QString source = ocrText.left(kSnippetCharacters);
            QString       snippet;
            snippet.reserve(source.size());

            bool pendingSpace = false;
            for (const QChar character: source)
            {
                if (character.isSpace())
                {
                    pendingSpace = !snippet.isEmpty();
                    continue;
                }
                if (pendingSpace)
                {
                    snippet.append(QLatin1Char(' '));
                    pendingSpace = false;
                }
                snippet.append(character);
            }
            return snippet;
        }

        MemoryResult readResult(const QSqlQuery &query)
        {
            MemoryResult result;
            result.id          = query.value(0).toLongLong();
            result.filePath    = toPath(query.value(1).toString());
            result.source      = parseMemorySource(query.value(2).toString().toStdString()).value_or(MemorySource::Capture);
            result.capturedAt  = QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong());
            result.size        = QSize(query.value(4).toInt(), query.value(5).toInt());
            result.processName = query.value(6).toString();
            result.windowTitle = query.value(7).toString();
            result.ocrSnippet  = snippetOf(query.value(8).toString());
            result.ocrStatus   = query.value(9).toString();
            result.dhash       = query.value(10).toString();

            std::error_code existsError;
            result.fileMissing = !std::filesystem::exists(result.filePath, existsError);
            return result;
        }

        QString resultColumns()
        {
            return QStringLiteral("items.id, items.path, items.source, items.captured_at, items.width, items.height, "
                                  "items.process_name, items.window_title, items.ocr_text, items.ocr_status, items.dhash");
        }

        /// 打开参数：WAL 让读写并行，secure_delete 让清空后的文本不留在空闲页
        std::optional<Base::Error> configureDatabase(QSqlDatabase &database)
        {
            const QStringList pragmas{
                    QStringLiteral("PRAGMA journal_mode = WAL"),
                    QStringLiteral("PRAGMA synchronous = NORMAL"),
                    QStringLiteral("PRAGMA secure_delete = ON"),
                    QStringLiteral("PRAGMA foreign_keys = ON"),
            };
            for (const QString &statement: pragmas)
            {
                QSqlQuery query{database};
                if (!query.exec(statement))
                {
                    return queryError(QStringLiteral("设置数据库参数失败"), query.lastError());
                }
            }
            return std::nullopt;
        }

        /// 建表、索引、FTS5 外部内容表与三个同步触发器
        std::optional<Base::Error> ensureSchema(QSqlDatabase &database)
        {
            int version = 0;
            {
                QSqlQuery versionQuery{database};
                if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next())
                {
                    return queryError(QStringLiteral("读取数据库版本失败"), versionQuery.lastError());
                }
                version = versionQuery.value(0).toInt();
            }

            if (version > kSchemaVersion)
            {
                return Base::Error{Base::ErrorCode::PlatformUnsupported,
                                   QStringLiteral("记忆索引：数据库来自更新版本的软件（schema %1，本版本支持 %2），本版本不会降级写入；请升级软件后再打开，或换一个数据目录。")
                                           .arg(version)
                                           .arg(kSchemaVersion)
                                           .toStdString()};
            }
            if (version == kSchemaVersion)
            {
                return std::nullopt;
            }

            if (!database.transaction())
            {
                return queryError(QStringLiteral("开启建表事务失败"), database.lastError());
            }

            const QStringList statements{
                    QStringLiteral("CREATE TABLE items("
                                   "id INTEGER PRIMARY KEY, "
                                   "path TEXT NOT NULL, "
                                   "path_key TEXT NOT NULL UNIQUE, "
                                   "source TEXT NOT NULL, "
                                   "captured_at INTEGER NOT NULL, "
                                   "added_at INTEGER NOT NULL, "
                                   "width INTEGER NOT NULL DEFAULT 0, "
                                   "height INTEGER NOT NULL DEFAULT 0, "
                                   "process_name TEXT NOT NULL DEFAULT '', "
                                   "window_title TEXT NOT NULL DEFAULT '', "
                                   "ocr_text TEXT NOT NULL DEFAULT '', "
                                   "ocr_status TEXT NOT NULL DEFAULT 'pending', "
                                   "ocr_error TEXT NOT NULL DEFAULT '', "
                                   "truncated INTEGER NOT NULL DEFAULT 0, "
                                   "search_text TEXT NOT NULL DEFAULT '', "
                                   "dhash TEXT NOT NULL DEFAULT '')"),
                    QStringLiteral("CREATE INDEX idx_items_captured_at ON items(captured_at DESC)"),
                    QStringLiteral("CREATE INDEX idx_items_process ON items(process_name)"),
                    QStringLiteral("CREATE INDEX idx_items_dhash ON items(dhash)"),
                    QStringLiteral("CREATE VIRTUAL TABLE items_fts USING fts5(search_text, content='items', content_rowid='id', tokenize='unicode61')"),
                    QStringLiteral("CREATE TRIGGER items_ai AFTER INSERT ON items BEGIN "
                                   "INSERT INTO items_fts(rowid, search_text) VALUES (new.id, new.search_text); "
                                   "END"),
                    QStringLiteral("CREATE TRIGGER items_ad AFTER DELETE ON items BEGIN "
                                   "INSERT INTO items_fts(items_fts, rowid, search_text) VALUES ('delete', old.id, old.search_text); "
                                   "END"),
                    QStringLiteral("CREATE TRIGGER items_au AFTER UPDATE OF search_text ON items BEGIN "
                                   "INSERT INTO items_fts(items_fts, rowid, search_text) VALUES ('delete', old.id, old.search_text); "
                                   "INSERT INTO items_fts(rowid, search_text) VALUES (new.id, new.search_text); "
                                   "END"),
                    QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion),
            };

            for (const QString &statement: statements)
            {
                QSqlQuery query{database};
                if (!query.exec(statement))
                {
                    const QSqlError schemaError = query.lastError();
                    database.rollback();
                    return queryError(QStringLiteral("建立数据库结构失败"), schemaError);
                }
            }

            if (!database.commit())
            {
                const QSqlError commitError = database.lastError();
                database.rollback();
                return queryError(QStringLiteral("提交数据库结构失败"), commitError);
            }
            return std::nullopt;
        }
    } // namespace

    MemoryIndex::MemoryIndex(std::filesystem::path databasePath) :
        m_databasePath(std::move(databasePath)), m_connectionName(QStringLiteral("MemoryIndex-%1").arg(s_connectionCounter.fetch_add(1)))
    {
    }

    MemoryIndex::~MemoryIndex()
    {
        close();
    }

    void MemoryIndex::close()
    {
        closeConnection(m_connectionName);
        m_open = false;
    }

    std::filesystem::path MemoryIndex::defaultStoragePath()
    {
        return Base::Paths::applicationDataDirectory() / "memory-index.sqlite";
    }

    QString MemoryIndex::pathKey(const std::filesystem::path &path)
    {
        const std::u8string normalized = path.lexically_normal().generic_u8string();
        return QString::fromUtf8(reinterpret_cast<const char *>(normalized.data()), static_cast<qsizetype>(normalized.size())).toCaseFolded();
    }

    Base::Expected<void> MemoryIndex::open()
    {
        if (m_open)
        {
            return {};
        }

        // 上一次打开失败可能留下同名连接的残留，重新建立前先清理，避免 Qt 报重复连接名
        closeConnection(m_connectionName);

        // 数据目录可能还没建：先把父目录建出来，失败时给出可操作的路径与原因
        const std::filesystem::path parent = m_databasePath.parent_path();
        if (!parent.empty())
        {
            std::error_code directoryError;
            std::filesystem::create_directories(parent, directoryError);
            if (directoryError)
            {
                return std::unexpected(makeError(Base::ErrorCode::FileWriteFailed,
                                                 QStringLiteral("无法创建数据库目录 %1（%2）").arg(fromPath(parent), QString::fromStdString(directoryError.message()))));
            }
        }

        std::optional<Base::Error> failure;
        {
            QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
            database.setDatabaseName(fromPath(m_databasePath));

            if (!database.isValid())
            {
                // 驱动缺失属于部署问题：提示插件位置，不把 QSqlError 的英文原文当唯一说明
                failure = Base::Error{Base::ErrorCode::DependencyMissing,
                                      QStringLiteral("记忆索引：缺少 Qt SQLite 驱动，请确认程序目录下的 sqldrivers/qsqlite.dll 随安装包部署，然后重新打开记忆库。").toStdString()};
            } else if (!database.open())
            {
                failure =
                        makeError(Base::ErrorCode::FileWriteFailed, QStringLiteral("无法打开数据库 %1（%2）").arg(fromPath(m_databasePath), database.lastError().text().trimmed()));
            } else
            {
                failure = configureDatabase(database);
                if (!failure)
                {
                    failure = ensureSchema(database);
                }
                if (failure)
                {
                    database.close();
                }
            }
        }

        if (failure)
        {
            close();
            return std::unexpected(*failure);
        }

        m_open = true;
        return {};
    }

    bool MemoryIndex::isOpen() const noexcept
    {
        return m_open;
    }

    Base::Expected<void> MemoryIndex::beginTransaction()
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        QSqlDatabase database = databaseFor(m_connectionName);
        if (!database.transaction())
        {
            return std::unexpected(queryError(QStringLiteral("开启事务失败"), database.lastError()));
        }
        return {};
    }

    Base::Expected<void> MemoryIndex::commitTransaction()
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        QSqlDatabase database = databaseFor(m_connectionName);
        if (!database.commit())
        {
            return std::unexpected(queryError(QStringLiteral("提交事务失败"), database.lastError()));
        }
        return {};
    }

    Base::Expected<void> MemoryIndex::rollbackTransaction()
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        QSqlDatabase database = databaseFor(m_connectionName);
        if (!database.rollback())
        {
            return std::unexpected(queryError(QStringLiteral("回滚事务失败"), database.lastError()));
        }
        return {};
    }

    Base::Expected<bool> MemoryIndex::containsPath(const std::filesystem::path &path) const
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        QSqlQuery statement{databaseFor(m_connectionName)};
        if (!statement.prepare(QStringLiteral("SELECT 1 FROM items WHERE path_key = ? LIMIT 1")))
        {
            return std::unexpected(queryError(QStringLiteral("准备按路径查在库失败"), statement.lastError()));
        }
        statement.addBindValue(pathKey(path));
        if (!statement.exec())
        {
            return std::unexpected(queryError(QStringLiteral("按路径查在库失败"), statement.lastError()));
        }
        return statement.next();
    }

    Base::Expected<qint64> MemoryIndex::upsertPending(const MemoryUpsert &entry)
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }
        if (entry.filePath.empty())
        {
            return std::unexpected(Base::Error{Base::ErrorCode::InvalidArgument, QStringLiteral("记忆索引：条目路径为空，请传入截图落盘后的文件路径再重试。").toStdString()});
        }

        const QString pathText   = fromPath(entry.filePath);
        const QString key        = pathKey(entry.filePath);
        const qint64  now        = QDateTime::currentMSecsSinceEpoch();
        const qint64  capturedAt = entry.capturedAt.isValid() ? entry.capturedAt.toMSecsSinceEpoch() : now;
        const QString searchText = buildSearchText({QFileInfo(toPath(pathText)).completeBaseName(), readableProcessName(entry.processName), entry.windowTitle});
        const QString source     = QString::fromLatin1(memorySourceToken(entry.source).data(), static_cast<qsizetype>(memorySourceToken(entry.source).size()));

        QSqlDatabase database = databaseFor(m_connectionName);
        QSqlQuery    statement{database};
        if (!statement.prepare(
                    QStringLiteral("INSERT INTO items(path, path_key, source, captured_at, added_at, width, height, process_name, window_title, ocr_status, search_text) "
                                   "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, 'pending', ?) "
                                   "ON CONFLICT(path_key) DO UPDATE SET path = excluded.path, source = excluded.source, "
                                   "captured_at = excluded.captured_at, width = excluded.width, height = excluded.height, "
                                   "process_name = excluded.process_name, window_title = excluded.window_title, "
                                   "ocr_text = '', ocr_status = 'pending', ocr_error = '', truncated = 0, "
                                   "search_text = excluded.search_text, dhash = ''")))
        {
            return std::unexpected(queryError(QStringLiteral("准备写入条目失败"), statement.lastError()));
        }

        statement.addBindValue(nonNull(pathText));
        statement.addBindValue(nonNull(key));
        statement.addBindValue(source);
        statement.addBindValue(capturedAt);
        statement.addBindValue(now);
        statement.addBindValue(entry.size.width());
        statement.addBindValue(entry.size.height());
        statement.addBindValue(nonNull(entry.processName));
        statement.addBindValue(nonNull(entry.windowTitle));
        statement.addBindValue(searchText);
        if (!statement.exec())
        {
            return std::unexpected(queryError(QStringLiteral("写入条目失败"), statement.lastError()));
        }

        // UPSERT 的 lastInsertId 在更新分支上不可靠，统一按唯一键回查
        QSqlQuery identifierQuery{database};
        identifierQuery.prepare(QStringLiteral("SELECT id FROM items WHERE path_key = ?"));
        identifierQuery.addBindValue(key);
        if (!identifierQuery.exec() || !identifierQuery.next())
        {
            return std::unexpected(queryError(QStringLiteral("回查条目主键失败"), identifierQuery.lastError()));
        }
        return identifierQuery.value(0).toLongLong();
    }

    Base::Expected<void> MemoryIndex::updateOcr(const qint64 id, QString text, QString dhash, QString status, QString error, const bool truncated)
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }
        if (status != QStringLiteral("done") && status != QStringLiteral("failed") && status != QStringLiteral("pending"))
        {
            return std::unexpected(
                    Base::Error{Base::ErrorCode::InvalidArgument,
                                QStringLiteral("记忆索引：识别状态 %1 无效，只接受 done、failed 或 pending；请修正调用方传入的状态后重试。").arg(status).toStdString()});
        }

        QSqlDatabase database = databaseFor(m_connectionName);
        QString      pathText;
        QString      processName;
        QString      windowTitle;
        {
            QSqlQuery rowQuery{database};
            rowQuery.prepare(QStringLiteral("SELECT path, process_name, window_title FROM items WHERE id = ?"));
            rowQuery.addBindValue(id);
            if (!rowQuery.exec())
            {
                return std::unexpected(queryError(QStringLiteral("读取条目元数据失败"), rowQuery.lastError()));
            }
            if (!rowQuery.next())
            {
                // 识别完成前条目可能已被删除或清空，此时没有可写的行，按幂等成功处理
                return {};
            }
            pathText    = rowQuery.value(0).toString();
            processName = rowQuery.value(1).toString();
            windowTitle = rowQuery.value(2).toString();
        }

        bool          isTruncated    = truncated;
        const QString storedText     = truncateUtf8(text, kMaxOcrTextBytes, isTruncated);
        bool          errorTruncated = false;
        const QString storedError    = truncateUtf8(error, kMaximumOcrErrorBytes, errorTruncated);

        const QString searchText = buildSearchText({storedText, windowTitle, readableProcessName(processName), QFileInfo(toPath(pathText)).completeBaseName()});

        QSqlQuery statement{database};
        if (!statement.prepare(QStringLiteral("UPDATE items SET ocr_text = ?, ocr_status = ?, ocr_error = ?, truncated = ?, search_text = ?, dhash = ? WHERE id = ?")))
        {
            return std::unexpected(queryError(QStringLiteral("准备更新识别结果失败"), statement.lastError()));
        }
        statement.addBindValue(nonNull(storedText));
        statement.addBindValue(nonNull(status));
        statement.addBindValue(nonNull(storedError));
        statement.addBindValue(isTruncated ? 1 : 0);
        statement.addBindValue(searchText);
        statement.addBindValue(nonNull(dhash));
        statement.addBindValue(id);
        if (!statement.exec())
        {
            return std::unexpected(queryError(QStringLiteral("更新识别结果失败"), statement.lastError()));
        }
        return {};
    }

    Base::Expected<void> MemoryIndex::markOcrFailed(const qint64 id, QString error)
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        bool          errorTruncated = false;
        const QString storedError    = truncateUtf8(error, kMaximumOcrErrorBytes, errorTruncated);

        QSqlQuery statement{databaseFor(m_connectionName)};
        if (!statement.prepare(QStringLiteral("UPDATE items SET ocr_status = 'failed', ocr_error = ? WHERE id = ?")))
        {
            return std::unexpected(queryError(QStringLiteral("准备写入识别失败状态失败"), statement.lastError()));
        }
        statement.addBindValue(nonNull(storedError));
        statement.addBindValue(id);
        if (!statement.exec())
        {
            return std::unexpected(queryError(QStringLiteral("写入识别失败状态失败"), statement.lastError()));
        }
        return {};
    }

    Base::Expected<void> MemoryIndex::resetOcr(const qint64 id)
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        QSqlQuery statement{databaseFor(m_connectionName)};
        statement.prepare(QStringLiteral("UPDATE items SET ocr_status = 'pending', ocr_error = '' WHERE id = ?"));
        statement.addBindValue(id);
        if (!statement.exec())
        {
            return std::unexpected(queryError(QStringLiteral("重置识别状态失败"), statement.lastError()));
        }
        return {};
    }

    Base::Expected<std::optional<MemoryResult>> MemoryIndex::findById(const qint64 id) const
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        QSqlQuery statement{databaseFor(m_connectionName)};
        if (!statement.prepare(QStringLiteral("SELECT %1 FROM items WHERE items.id = ?").arg(resultColumns())))
        {
            return std::unexpected(queryError(QStringLiteral("准备按主键读取条目失败"), statement.lastError()));
        }
        statement.addBindValue(id);
        if (!statement.exec())
        {
            return std::unexpected(queryError(QStringLiteral("按主键读取条目失败"), statement.lastError()));
        }
        if (!statement.next())
        {
            return std::optional<MemoryResult>{};
        }
        return std::optional<MemoryResult>{readResult(statement)};
    }

    Base::Expected<std::vector<MemoryResult>> MemoryIndex::search(const MemoryQuery &query) const
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        // 查询先归一空白再截到 256 字符：超长输入不能变成一个超长短语交给 FTS5
        QString normalizedText = query.text.simplified();
        if (normalizedText.size() > kMaximumQueryCharacters)
        {
            normalizedText.truncate(kMaximumQueryCharacters);
        }
        const QString matchExpression = buildFtsQuery(normalizedText);
        const bool    hasKeyword      = !matchExpression.isEmpty();

        QStringList  clauses;
        QVariantList bindings;
        if (hasKeyword)
        {
            clauses.append(QStringLiteral("items_fts MATCH ?"));
            bindings.append(matchExpression);
        }
        if (!query.processName.isEmpty())
        {
            if (query.processName == unknownProcessFilter())
            {
                clauses.append(QStringLiteral("items.process_name = ''"));
            } else
            {
                clauses.append(QStringLiteral("items.process_name = ?"));
                bindings.append(query.processName);
            }
        }
        if (query.from.has_value())
        {
            clauses.append(QStringLiteral("items.captured_at >= ?"));
            bindings.append(query.from->toMSecsSinceEpoch());
        }
        if (query.to.has_value())
        {
            clauses.append(QStringLiteral("items.captured_at <= ?"));
            bindings.append(query.to->toMSecsSinceEpoch());
        }

        // 子句与排序都是固定 SQL 片段；用户输入只经绑定参数进入
        QString sql = QStringLiteral("SELECT %1 FROM items").arg(resultColumns());
        if (hasKeyword)
        {
            sql += QStringLiteral(" JOIN items_fts ON items_fts.rowid = items.id");
        }
        if (!clauses.isEmpty())
        {
            sql += QStringLiteral(" WHERE ") + clauses.join(QStringLiteral(" AND "));
        }
        if (hasKeyword)
        {
            sql += QStringLiteral(" ORDER BY bm25(items_fts), items.captured_at DESC");
        } else
        {
            sql += QStringLiteral(" ORDER BY items.captured_at DESC");
        }
        sql += QStringLiteral(" LIMIT ?");
        bindings.append(std::clamp(query.limit, 1, kMemorySearchLimit));

        QSqlQuery statement{databaseFor(m_connectionName)};
        if (!statement.prepare(sql))
        {
            return std::unexpected(queryError(QStringLiteral("准备搜索失败"), statement.lastError()));
        }
        for (const QVariant &binding: bindings)
        {
            statement.addBindValue(binding);
        }
        if (!statement.exec())
        {
            return std::unexpected(queryError(QStringLiteral("执行搜索失败"), statement.lastError()));
        }

        std::vector<MemoryResult> results;
        while (statement.next())
        {
            results.push_back(readResult(statement));
        }
        return results;
    }

    Base::Expected<std::vector<MemoryAppCount>> MemoryIndex::apps() const
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        QSqlQuery statement{databaseFor(m_connectionName)};
        if (!statement.exec(QStringLiteral("SELECT process_name, COUNT(*) FROM items GROUP BY process_name ORDER BY COUNT(*) DESC, process_name ASC")))
        {
            return std::unexpected(queryError(QStringLiteral("统计应用分布失败"), statement.lastError()));
        }

        std::vector<MemoryAppCount> counts;
        while (statement.next())
        {
            counts.push_back(MemoryAppCount{statement.value(0).toString(), statement.value(1).toInt()});
        }
        return counts;
    }

    Base::Expected<MemoryStats> MemoryIndex::stats(const int excludedProcessCount) const
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        QSqlQuery statement{databaseFor(m_connectionName)};
        if (!statement.exec(QStringLiteral("SELECT COUNT(*), COALESCE(SUM(ocr_status = 'pending'), 0), COALESCE(SUM(ocr_status = 'failed'), 0) FROM items")) || !statement.next())
        {
            return std::unexpected(queryError(QStringLiteral("读取统计失败"), statement.lastError()));
        }

        MemoryStats result;
        result.totalItems           = statement.value(0).toInt();
        result.pendingOcr           = statement.value(1).toInt();
        result.failedOcr            = statement.value(2).toInt();
        result.excludedProcessCount = excludedProcessCount;

        // WAL 模式下来不及落主文件的页在 -wal 里，页脚读数要把两份都算上
        std::filesystem::path walPath = m_databasePath;
        walPath += "-wal";
        result.databaseBytes = fileBytes(m_databasePath) + fileBytes(walPath);
        return result;
    }

    Base::Expected<std::vector<MemoryResult>> MemoryIndex::pendingItems(const int limit) const
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        QSqlQuery statement{databaseFor(m_connectionName)};
        if (!statement.prepare(QStringLiteral("SELECT %1 FROM items WHERE ocr_status = 'pending' "
                                              "ORDER BY CASE source WHEN 'capture' THEN 0 ELSE 1 END, added_at ASC, id ASC LIMIT ?")
                                       .arg(resultColumns())))
        {
            return std::unexpected(queryError(QStringLiteral("准备待识别列表失败"), statement.lastError()));
        }
        statement.addBindValue(std::max(limit, 1));
        if (!statement.exec())
        {
            return std::unexpected(queryError(QStringLiteral("读取待识别列表失败"), statement.lastError()));
        }

        std::vector<MemoryResult> results;
        while (statement.next())
        {
            results.push_back(readResult(statement));
        }
        return results;
    }

    Base::Expected<void> MemoryIndex::removeByPath(const std::filesystem::path &path)
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        QSqlQuery statement{databaseFor(m_connectionName)};
        statement.prepare(QStringLiteral("DELETE FROM items WHERE path_key = ?"));
        statement.addBindValue(pathKey(path));
        if (!statement.exec())
        {
            return std::unexpected(queryError(QStringLiteral("按路径删除条目失败"), statement.lastError()));
        }
        return {};
    }

    Base::Expected<void> MemoryIndex::removeCaptureEntries()
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        QSqlQuery statement{databaseFor(m_connectionName)};
        if (!statement.exec(QStringLiteral("DELETE FROM items WHERE source = 'capture'")))
        {
            return std::unexpected(queryError(QStringLiteral("删除截图条目失败"), statement.lastError()));
        }
        return {};
    }

    Base::Expected<void> MemoryIndex::removeProcess(QString processName)
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }
        if (processName.isEmpty())
        {
            // 空进程名是导入条目的标记，排除操作不得误删它们
            return {};
        }

        QSqlQuery statement{databaseFor(m_connectionName)};
        statement.prepare(QStringLiteral("DELETE FROM items WHERE process_name = ? COLLATE NOCASE"));
        statement.addBindValue(processName);
        if (!statement.exec())
        {
            return std::unexpected(queryError(QStringLiteral("删除应用条目失败"), statement.lastError()));
        }
        return {};
    }

    Base::Expected<void> MemoryIndex::clear()
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        QSqlDatabase database = databaseFor(m_connectionName);
        QSqlQuery    deletion{database};
        if (!deletion.exec(QStringLiteral("DELETE FROM items")))
        {
            return std::unexpected(queryError(QStringLiteral("清空索引失败"), deletion.lastError()));
        }

        // secure_delete 已把空闲页内容抹掉，VACUUM 进一步收缩文件
        QSqlQuery vacuum{database};
        if (!vacuum.exec(QStringLiteral("VACUUM")))
        {
            return std::unexpected(queryError(QStringLiteral("回收数据库空间失败"), vacuum.lastError()));
        }
        return {};
    }

    Base::Expected<int> MemoryIndex::pruneMissing()
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        QSqlDatabase database = databaseFor(m_connectionName);
        QSqlQuery    listing{database};
        if (!listing.exec(QStringLiteral("SELECT id, path FROM items")))
        {
            return std::unexpected(queryError(QStringLiteral("读取条目路径失败"), listing.lastError()));
        }

        std::vector<qint64> missing;
        while (listing.next())
        {
            const std::filesystem::path path = toPath(listing.value(1).toString());
            std::error_code             existsError;
            if (!std::filesystem::exists(path, existsError))
            {
                missing.push_back(listing.value(0).toLongLong());
            }
        }
        if (missing.empty())
        {
            return 0;
        }

        if (!database.transaction())
        {
            return std::unexpected(queryError(QStringLiteral("开启缺失清理事务失败"), database.lastError()));
        }

        QSqlQuery deletion{database};
        if (!deletion.prepare(QStringLiteral("DELETE FROM items WHERE id = ?")))
        {
            database.rollback();
            return std::unexpected(queryError(QStringLiteral("准备删除缺失条目失败"), deletion.lastError()));
        }
        for (const qint64 id: missing)
        {
            deletion.bindValue(0, id);
            if (!deletion.exec())
            {
                const QSqlError deleteError = deletion.lastError();
                database.rollback();
                return std::unexpected(queryError(QStringLiteral("删除缺失条目失败"), deleteError));
            }
        }

        if (!database.commit())
        {
            const QSqlError commitError = database.lastError();
            database.rollback();
            return std::unexpected(queryError(QStringLiteral("提交缺失清理失败"), commitError));
        }
        return static_cast<int>(missing.size());
    }

    Base::Expected<std::vector<qint64>> MemoryIndex::enforceCapacity(const int maxEntries, const qint64 keepId)
    {
        if (!m_open)
        {
            return std::unexpected(notOpenError());
        }

        QSqlDatabase database = databaseFor(m_connectionName);
        int          total    = 0;
        {
            QSqlQuery countQuery{database};
            if (!countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM items")) || !countQuery.next())
            {
                return std::unexpected(queryError(QStringLiteral("读取条目总数失败"), countQuery.lastError()));
            }
            total = countQuery.value(0).toInt();
        }

        std::vector<qint64> evicted;
        const int           maximum = std::max(maxEntries, 0);
        const int           excess  = total - maximum;
        if (excess <= 0)
        {
            return evicted;
        }

        // 本次刚写入的一行必须活过这次淘汰；调用方没给主键时退回保护最新一行
        qint64 protectedId = keepId;
        if (protectedId <= 0)
        {
            QSqlQuery newestQuery{database};
            if (!newestQuery.exec(QStringLiteral("SELECT id FROM items ORDER BY added_at DESC, id DESC LIMIT 1")) || !newestQuery.next())
            {
                return std::unexpected(queryError(QStringLiteral("确定最新条目失败"), newestQuery.lastError()));
            }
            protectedId = newestQuery.value(0).toLongLong();
        }

        if (!database.transaction())
        {
            return std::unexpected(queryError(QStringLiteral("开启容量淘汰事务失败"), database.lastError()));
        }

        QSqlQuery nextVictim{database};
        if (!nextVictim.prepare(QStringLiteral("SELECT id FROM items WHERE source = ? AND id <> ? ORDER BY added_at ASC, id ASC LIMIT 1")))
        {
            database.rollback();
            return std::unexpected(queryError(QStringLiteral("准备查找待淘汰条目失败"), nextVictim.lastError()));
        }
        QSqlQuery removal{database};
        if (!removal.prepare(QStringLiteral("DELETE FROM items WHERE id = ?")))
        {
            database.rollback();
            return std::unexpected(queryError(QStringLiteral("准备容量淘汰失败"), removal.lastError()));
        }

        // 每次淘汰一条：先导入后截图，并始终跳过 protectedId
        while (static_cast<int>(evicted.size()) < excess)
        {
            bool deletedOne = false;
            for (const QString &source: {QStringLiteral("import"), QStringLiteral("capture")})
            {
                nextVictim.bindValue(0, source);
                nextVictim.bindValue(1, protectedId);
                if (!nextVictim.exec())
                {
                    const QSqlError findError = nextVictim.lastError();
                    database.rollback();
                    return std::unexpected(queryError(QStringLiteral("查找待淘汰条目失败"), findError));
                }
                if (!nextVictim.next())
                {
                    continue;
                }

                const qint64 victimId = nextVictim.value(0).toLongLong();
                nextVictim.finish();
                removal.bindValue(0, victimId);
                if (!removal.exec())
                {
                    const QSqlError deleteError = removal.lastError();
                    database.rollback();
                    return std::unexpected(queryError(QStringLiteral("容量淘汰失败"), deleteError));
                }
                evicted.push_back(victimId);
                deletedOne = true;
                break;
            }
            if (!deletedOne)
            {
                break;
            }
        }

        if (!database.commit())
        {
            const QSqlError commitError = database.lastError();
            database.rollback();
            return std::unexpected(queryError(QStringLiteral("提交容量淘汰失败"), commitError));
        }
        return evicted;
    }
} // namespace GyanisScribe::Core
