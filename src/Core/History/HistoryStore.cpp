#include "Core/History/HistoryStore.h"

#include "Base/Logger.h"
#include "Base/Paths.h"
#include "Base/Text.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <format>
#include <string>

namespace
{
    using GyanisScribe::Base::Error;
    using GyanisScribe::Base::ErrorCode;
    using GyanisScribe::Base::Expected;
    using GyanisScribe::Core::HistoryEntry;

    /// 清单里各字段的键名：写在纸上就是协议，改名等于换格式
    constexpr const char *kRootKeyEntries = "entries";
    constexpr const char *kKeyPath        = "path";
    constexpr const char *kKeyKind        = "kind";
    constexpr const char *kKeyMoment      = "moment";
    constexpr const char *kKeyWidth       = "width";
    constexpr const char *kKeyHeight      = "height";

    /**
     * @brief 造一条带中文文案的错误
     * @param code 错误码
     * @param message 说清原因与下一步的那句话
     * @return 填好的 Error
     */
    Error makeError(const ErrorCode code, std::string message)
    {
        return Error{.code = code, .message = std::move(message)};
    }

    /// 成品路径在清单里的写法：通用分隔符 + UTF-8 字节
    std::string pathToText(const std::filesystem::path &path)
    {
        const auto utf8 = path.generic_u8string();
        return std::string{reinterpret_cast<const char *>(utf8.data()), utf8.size()};
    }

    /**
     * @brief 把清单里的 UTF-8 路径读回 path
     * @param text 清单里的那串字符
     * @return 成品路径
     * @details 走 u8string 而不是 narrow string：Windows 上 narrow 会按本地 ANSI 码页解释，
     *          中文用户名下的截图目录就变成另一条不存在的路。
     */
    std::filesystem::path pathFromText(const QString &text)
    {
        const auto utf8 = text.toUtf8();
        return std::filesystem::path{std::u8string{reinterpret_cast<const char8_t *>(utf8.constData()), static_cast<std::size_t>(utf8.size())}};
    }
} // namespace

namespace GyanisScribe::Core
{
    HistoryStore::HistoryStore(std::filesystem::path storagePath) : m_storagePath(std::move(storagePath))
    {
    }

    std::filesystem::path HistoryStore::defaultStoragePath()
    {
        return Base::Paths::applicationDataDirectory() / "history.json";
    }

    Expected<HistoryLoadResult> HistoryStore::load(const HistoryRetentionPolicy &policy, const QDateTime &now) const
    {
        auto raw = readRaw();
        if (!raw)
        {
            return Expected<HistoryLoadResult>{std::unexpected{raw.error()}};
        }

        // 成品已经不在了的记录先丢掉：列表里点一张不存在的图，比列表少一条更难解释
        const auto before = raw->entries.size();
        // QVector 没有 std::erase_if 的重载，用 erase + remove_if 这一对
        raw->entries.erase(std::remove_if(raw->entries.begin(), raw->entries.end(), [](const HistoryEntry &entry) { return !std::filesystem::exists(entry.filePath); }),
                           raw->entries.end());
        if (const auto dropped = before - raw->entries.size(); dropped > 0)
        {
            Base::Logger::info(std::format("历史清单里有 {} 条的成品文件已经不在了，这次不再列出", dropped));
        }

        return Expected<HistoryLoadResult>{HistoryLoadResult{
                .entries        = pruneForPolicy(raw->entries, policy, now),
                .skippedRecords = raw->skipped,
                .firstProblem   = std::move(raw->firstProblem),
        }};
    }

    Expected<void> HistoryStore::append(const HistoryEntry &entry, const HistoryRetentionPolicy &policy, const QDateTime &now)
    {
        auto raw = readRaw();
        if (!raw)
        {
            return Expected<void>{std::unexpected{raw.error()}};
        }

        // 同一路径只留一条：同一张图被记进两次时，后写的这条覆盖前一条，而不是并列两行
        raw->entries.erase(std::remove_if(raw->entries.begin(), raw->entries.end(), [&entry](const HistoryEntry &existing) { return existing.filePath == entry.filePath; }),
                           raw->entries.end());
        raw->entries.push_back(entry);

        return writeRaw(pruneForPolicy(raw->entries, policy, now), raw->badRecords);
    }

    Expected<void> HistoryStore::remove(const std::filesystem::path &filePath)
    {
        auto raw = readRaw();
        if (!raw)
        {
            return Expected<void>{std::unexpected{raw.error()}};
        }

        const auto found = std::ranges::find(raw->entries, filePath, &HistoryEntry::filePath);
        if (found == raw->entries.end())
        {
            return Expected<void>{std::unexpected{
                    makeError(ErrorCode::InvalidArgument,
                              GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("历史清单里没有这条记录：{}。请刷新列表后再试一次"), pathToText(filePath)))}};
        }

        raw->entries.erase(found);
        return writeRaw(raw->entries, raw->badRecords);
    }

    Expected<void> HistoryStore::clear()
    {
        // 原子替换那条路已经由 writeRaw 兜着：空清单也走同一次替换，不留「先清空再写」的中间窗口。
        // 坏记录也在这里一并丢掉：用户点的是清空整份清单，不是只清读得懂的那一半
        return writeRaw({}, {});
    }

    Expected<HistoryStore::RawHistory> HistoryStore::readRaw() const
    {
        std::error_code existsError;
        if (!std::filesystem::exists(m_storagePath, existsError))
        {
            return Expected<RawHistory>{RawHistory{}};
        }

        QFile file{QString::fromStdWString(m_storagePath.wstring())};
        if (!file.open(QIODevice::ReadOnly))
        {
            return Expected<RawHistory>{std::unexpected{
                    makeError(ErrorCode::FileWriteFailed,
                              GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("历史清单打不开：{}。请检查这个文件是否被别的程序占着，或把它删掉让软件重建"),
                                                           pathToText(m_storagePath)))}};
        }
        const auto bytes = file.readAll();

        const auto document = QJsonDocument::fromJson(bytes);
        if (document.isNull() || !document.isObject())
        {
            return Expected<RawHistory>{std::unexpected{makeError(
                    ErrorCode::InvalidArgument,
                    GyanisScribe::Base::uiFormat(
                            GyanisScribe::Base::uiTextUtf8("历史清单格式不对：{}。可以把这个文件删掉让软件重建（成品文件不受影响），或按 {{\"entries\": [...]}} 的形状修好"),
                            pathToText(m_storagePath)))}};
        }

        const auto array = document.object().value(QLatin1String{kRootKeyEntries}).toArray();
        RawHistory raw;
        raw.entries.reserve(static_cast<int>(array.size()));
        // 单条读不懂只跳过这一条：原始 JSON 一并留下，下一次写回时原样接上，用户还有机会修好它
        const auto skipRecord = [&raw, &array](const int index, std::string problem)
        {
            raw.badRecords.append(array.at(index));
            ++raw.skipped;
            if (raw.firstProblem.empty())
            {
                raw.firstProblem = std::move(problem);
            }
        };
        for (auto index = 0; index < array.size(); ++index)
        {
            const auto item       = array.at(index).toObject();
            const auto pathText   = item.value(QLatin1String{kKeyPath}).toString();
            const auto kindText   = item.value(QLatin1String{kKeyKind}).toString();
            const auto momentText = item.value(QLatin1String{kKeyMoment}).toString();
            if (pathText.isEmpty() || kindText.isEmpty() || momentText.isEmpty())
            {
                skipRecord(index, GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("历史清单第 {} 条缺 path、kind 或 moment"), index + 1));
                continue;
            }

            auto kind = parseHistoryKind(kindText.toStdString());
            if (!kind)
            {
                skipRecord(index, GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("历史清单第 {} 条：{}"), index + 1, kind.error().message));
                continue;
            }
            const auto moment = QDateTime::fromString(momentText, Qt::ISODateWithMs);
            if (!moment.isValid())
            {
                skipRecord(index, GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("历史清单第 {} 条的时间 \"{}\" 读不出来。请用 2026-10-10T08:30:00.000 这种写法"),
                                                               index + 1, momentText.toStdString()));
                continue;
            }

            raw.entries.push_back(HistoryEntry{
                    .moment   = moment,
                    .kind     = *kind,
                    .filePath = pathFromText(pathText),
                    .size     = QSize{item.value(QLatin1String{kKeyWidth}).toInt(), item.value(QLatin1String{kKeyHeight}).toInt()},
            });
        }

        return Expected<RawHistory>{std::move(raw)};
    }

    Expected<void> HistoryStore::writeRaw(const QVector<HistoryEntry> &entries, const QJsonArray &badRecords) const
    {
        QJsonArray array;
        for (const auto &entry: entries)
        {
            QJsonObject item;
            item.insert(QLatin1String{kKeyPath}, QString::fromStdString(pathToText(entry.filePath)));
            item.insert(QLatin1String{kKeyKind}, QString::fromUtf8(historyKindToken(entry.kind).data(), static_cast<qsizetype>(historyKindToken(entry.kind).size())));
            item.insert(QLatin1String{kKeyMoment}, entry.moment.toString(Qt::ISODateWithMs));
            item.insert(QLatin1String{kKeyWidth}, entry.size.width());
            item.insert(QLatin1String{kKeyHeight}, entry.size.height());
            array.append(item);
        }
        // 坏记录原样接在好记录后面：它们仍是用户文件的一部分，只有 clear() 才整份丢弃
        for (const auto &record: badRecords)
        {
            array.append(record);
        }

        QJsonObject root;
        root.insert(QLatin1String{kRootKeyEntries}, array);

        std::error_code directoryError;
        std::filesystem::create_directories(m_storagePath.parent_path(), directoryError);

        // QSaveFile 先写临时名、成功才替换：中途失败留下截断清单，下次启动就连历史都读不出
        QSaveFile file{QString::fromStdWString(m_storagePath.wstring())};
        if (!file.open(QIODevice::WriteOnly))
        {
            Base::Logger::error(std::format("历史清单写不了：{}", pathToText(m_storagePath)));
            return Expected<void>{std::unexpected{
                    makeError(ErrorCode::FileWriteFailed,
                              GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("历史清单写不了：{}。请确认这个目录还能写，或先把软件设的截图目录改到一个可写位置"),
                                                           pathToText(m_storagePath)))}};
        }
        file.write(QJsonDocument{root}.toJson(QJsonDocument::Indented));
        if (!file.commit())
        {
            return Expected<void>{std::unexpected{
                    makeError(ErrorCode::FileWriteFailed,
                              GyanisScribe::Base::uiFormat(GyanisScribe::Base::uiTextUtf8("历史清单没能落盘：{}。请重试一次，或换一个可写的目录"), pathToText(m_storagePath)))}};
        }
        return Expected<void>{};
    }

} // namespace GyanisScribe::Core
