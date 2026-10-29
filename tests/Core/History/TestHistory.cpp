#include "Core/History/HistoryRules.h"
#include "Core/History/HistoryStore.h"
#include "Core/History/HistoryTypes.h"

#include "Core/Settings/SettingsKeys.h"
#include "Core/Settings/SettingsStore.h"

#include <gtest/gtest.h>

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTime>
#include <QVariant>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

using GyanisScribe::Core::HistoryEntry;
using GyanisScribe::Core::HistoryKind;
using GyanisScribe::Core::historyKindLabel;
using GyanisScribe::Core::historyKindToken;
using GyanisScribe::Core::HistoryRetentionPolicy;
using GyanisScribe::Core::HistoryStore;
using GyanisScribe::Core::kDefaultMaximumEntries;
using GyanisScribe::Core::kDefaultRetentionDays;
using GyanisScribe::Core::matchesFilter;
using GyanisScribe::Core::parseHistoryKind;
using GyanisScribe::Core::policyFromSettings;
using GyanisScribe::Core::pruneForPolicy;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Core::sortNewestFirst;

namespace
{
    /// 一个固定的判「过期」用的时刻：用例里的时间一律由调用方给，不靠机器时钟
    const QDateTime kNow{QDate{2026, 10, 10}, QTime{12, 0}};

    /// 「两个上限都放开」的策略写法，避免用默认值当不限（默认值是 200 条 / 30 天）
    const HistoryRetentionPolicy kNoLimits{.maximumEntries = 0, .retentionDays = 0};

    /// 读回清单里的 entries 数组：用例据此检查写回后坏记录还在不在
    QJsonArray manifestEntries(const std::filesystem::path &manifestPath)
    {
        QFile file{QString::fromStdWString(manifestPath.wstring())};
        if (!file.open(QIODevice::ReadOnly))
        {
            return {};
        }
        const auto document = QJsonDocument::fromJson(file.readAll());
        return document.isObject() ? document.object().value(QStringLiteral("entries")).toArray() : QJsonArray{};
    }

    /// 造一条记录；路径由调用方给，是否真的存在由用例决定
    HistoryEntry makeEntry(const std::string &name, const int daysAgo, const HistoryKind kind)
    {
        return HistoryEntry{
                .moment   = kNow.addDays(-daysAgo),
                .kind     = kind,
                .filePath = std::filesystem::path{name},
                .size     = QSize{800, 600},
        };
    }

    /**
     * @brief 给每条用例一份独立的临时目录
     * @details 历史清单与假成品文件都是共用资源，并行跑（ctest -j）时两条用例写同一份
     *          history.json 会互相裁掉对方的记录。
     */
    class HistoryFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            m_directory = std::filesystem::temp_directory_path() / std::format("GyanisScribe-History-{}", ::testing::UnitTest::GetInstance()->current_test_info()->name());
            std::error_code cleanup;
            std::filesystem::remove_all(m_directory, cleanup);
            std::filesystem::create_directories(m_directory);
            m_storePath = m_directory / "history.json";
        }

        void TearDown() override
        {
            std::error_code cleanup;
            std::filesystem::remove_all(m_directory, cleanup);
        }

        /// 在临时目录里放一份「成品文件」，让 exists() 认它
        [[nodiscard]] std::filesystem::path makeArtifact(const std::string &name) const
        {
            const auto path = m_directory / name;
            std::ofstream{path} << "x";
            return path;
        }

        [[nodiscard]] std::filesystem::path storePath() const
        {
            return m_storePath;
        }

        std::filesystem::path m_directory; ///< 本用例的临时目录
        std::filesystem::path m_storePath; ///< 历史清单路径
    };

} // namespace

/**
 * @brief 钉住类别的存储标记与中文名各归一处，且能互相读回
 * @details 标记写进盘、中文名给人看；两者混用会让改一次中文文案等于换一次文件格式。
 */
TEST(HistoryKindTable, TokensRoundTripAndLabelsAreChinese)
{
    EXPECT_EQ(historyKindToken(HistoryKind::Screenshot), "Screenshot");
    EXPECT_EQ(historyKindToken(HistoryKind::Recording), "Recording");
    EXPECT_EQ(historyKindLabel(HistoryKind::Screenshot), QString::fromUtf8("截图"));
    EXPECT_EQ(historyKindLabel(HistoryKind::Recording), QString::fromUtf8("录屏"));

    for (const auto kind: {HistoryKind::Screenshot, HistoryKind::Recording})
    {
        const auto parsed = parseHistoryKind(historyKindToken(kind));
        ASSERT_TRUE(static_cast<bool>(parsed));
        EXPECT_EQ(*parsed, kind);
    }

    const auto rejected = parseHistoryKind("picture");
    ASSERT_FALSE(static_cast<bool>(rejected));
    EXPECT_NE(rejected.error().message.find("Screenshot"), std::string::npos) << "拒绝文案要把可用标记列出来";
    EXPECT_NE(rejected.error().message.find("Recording"), std::string::npos);
}

/**
 * @brief 钉住列表次序是时间倒序、同一时刻按路径升序
 * @details 并列时不给确定次序，同一份历史文件两次运行会给出不同列表，用例钉不住也就算了，
 *          用户也会以为记录在自己换位。
 */
TEST(HistoryOrdering, NewestFirstWithDeterministicTieBreak)
{
    QVector<HistoryEntry> table;
    table.push_back(makeEntry("a-old", 5, HistoryKind::Screenshot));
    table.push_back(makeEntry("z-new", 0, HistoryKind::Recording));
    table.push_back(makeEntry("b-same-second", 1, HistoryKind::Screenshot));
    table.push_back(makeEntry("a-same-second", 1, HistoryKind::Screenshot));

    sortNewestFirst(table);

    ASSERT_EQ(table.size(), 4);
    EXPECT_EQ(table[0].filePath.filename().string(), "z-new");
    EXPECT_EQ(table[1].filePath.filename().string(), "a-same-second") << "同一时刻按路径升序，a 在 b 前";
    EXPECT_EQ(table[2].filePath.filename().string(), "b-same-second");
    EXPECT_EQ(table[3].filePath.filename().string(), "a-old");
}

/**
 * @brief 钉住保留策略的两条上限与「0 表示不限」
 * @details 先按时长再按条数；0 走保留而不是清空——把 0 当成「一条都不留」会让用户的整片历史消失。
 */
TEST(HistoryPolicy, LimitsApplyAsDaysThenCountWithZeroMeaningUnlimited)
{
    QVector<HistoryEntry> table;
    table.push_back(makeEntry("newest.png", 0, HistoryKind::Screenshot));
    table.push_back(makeEntry("middle.png", 3, HistoryKind::Screenshot));
    table.push_back(makeEntry("oldest.png", 40, HistoryKind::Screenshot));

    const auto counted = pruneForPolicy(table, HistoryRetentionPolicy{.maximumEntries = 2, .retentionDays = 0}, kNow);
    ASSERT_EQ(counted.size(), 2);
    EXPECT_EQ(counted[0].filePath.filename().string(), "newest.png");
    EXPECT_EQ(counted[1].filePath.filename().string(), "middle.png");

    const auto dated = pruneForPolicy(table, HistoryRetentionPolicy{.maximumEntries = 0, .retentionDays = 10}, kNow);
    ASSERT_EQ(dated.size(), 2) << "40 天前那条已过 10 天的窗口";
    EXPECT_EQ(dated[1].filePath.filename().string(), "middle.png");

    const auto unlimited = pruneForPolicy(table, HistoryRetentionPolicy{.maximumEntries = 0, .retentionDays = 0}, kNow);
    EXPECT_EQ(unlimited.size(), 3) << "两个上限都是 0 时必须一条都不裁";
}

/**
 * @brief 钉住配置里的保留策略读法：缺省给默认值，负数按不限并留话
 * @details 把 -1 当成「最多 -1 条」会让刚写进去的下一条立刻被裁掉，看起来像历史功能坏了。
 */
TEST_F(HistoryFixture, PolicyReadingDefaultsAndNegativeFallsBackToUnlimited)
{
    const auto    iniPath = m_directory / "settings.ini";
    SettingsStore store{iniPath};

    EXPECT_EQ(policyFromSettings(store).maximumEntries, kDefaultMaximumEntries);
    EXPECT_EQ(policyFromSettings(store).retentionDays, kDefaultRetentionDays);

    ASSERT_TRUE(static_cast<bool>(store.setValue(GyanisScribe::Core::kHistoryMaximumEntries, QVariant{3})));
    ASSERT_TRUE(static_cast<bool>(store.setValue(GyanisScribe::Core::kHistoryRetentionDays, QVariant{-7})));
    store.sync();

    const auto policy = policyFromSettings(store);
    EXPECT_EQ(policy.maximumEntries, 3);
    EXPECT_EQ(policy.retentionDays, 0) << "负天数按「不限时长」处理，不当上限用";
}

/**
 * @brief 钉住筛选同时查文件名、类别中文名与日期
 * @details 用户记得的是「上周的截图」而不是文件名；三处都不查的筛选等于只能按文件名找。
 */
TEST(HistoryFilter, QueryMatchesNameKindLabelAndDate)
{
    const auto entry = makeEntry("GyanisScribe_20261010_083000_000.png", 0, HistoryKind::Screenshot);

    EXPECT_TRUE(matchesFilter(entry, QString{}, std::nullopt));
    EXPECT_TRUE(matchesFilter(entry, QStringLiteral("20261010"), std::nullopt));
    EXPECT_TRUE(matchesFilter(entry, QStringLiteral("scribe_"), std::nullopt)) << "文件名匹配不分大小写";
    EXPECT_TRUE(matchesFilter(entry, QString::fromUtf8("截图"), std::nullopt));
    EXPECT_TRUE(matchesFilter(entry, QStringLiteral("2026-10-10"), std::nullopt));
    EXPECT_FALSE(matchesFilter(entry, QStringLiteral("录屏"), std::nullopt));
    EXPECT_FALSE(matchesFilter(entry, QStringLiteral("不存在的词"), std::nullopt));

    EXPECT_TRUE(matchesFilter(entry, QString{}, HistoryKind::Screenshot));
    EXPECT_FALSE(matchesFilter(entry, QString{}, HistoryKind::Recording));
}

/**
 * @brief 钉住写进去的记录能原样读回来
 * @details 时间、类别、尺寸、路径四个字段就是「重新贴图」与「重新识别」需要的全部信息，
 *          少一个就得回去打开文件才知道那张图是什么。
 */
TEST_F(HistoryFixture, StoreRoundTripsEveryField)
{
    HistoryStore store{storePath()};
    const auto   artifact = makeArtifact("shot.png");
    const auto   entry    = HistoryEntry{.moment = kNow, .kind = HistoryKind::Recording, .filePath = artifact, .size = QSize{1280, 720}};

    ASSERT_TRUE(static_cast<bool>(store.append(entry, kNoLimits, kNow)));

    const auto loaded = store.load(kNoLimits, kNow);
    ASSERT_TRUE(static_cast<bool>(loaded));
    ASSERT_EQ(loaded->entries.size(), 1);
    EXPECT_EQ(loaded->entries[0].moment, kNow);
    EXPECT_EQ(loaded->entries[0].kind, HistoryKind::Recording);
    EXPECT_EQ(loaded->entries[0].filePath, artifact);
    EXPECT_EQ(loaded->entries[0].size, QSize(1280, 720));
    EXPECT_EQ(loaded->skippedRecords, 0);
}

/**
 * @brief 钉住成品被用户删掉之后不再列出这一条
 * @details 点一张不存在的图，界面上是什么都不出；少列一条至少不会误导。
 */
TEST_F(HistoryFixture, StoreDropsRecordsWhoseFileIsGone)
{
    HistoryStore store{storePath()};
    const auto   kept = makeArtifact("kept.png");

    ASSERT_TRUE(static_cast<bool>(store.append(HistoryEntry{.moment = kNow, .kind = HistoryKind::Screenshot, .filePath = kept, .size = QSize{10, 10}}, kNoLimits, kNow)));
    ASSERT_TRUE(static_cast<bool>(
            store.append(HistoryEntry{.moment = kNow.addDays(-1), .kind = HistoryKind::Screenshot, .filePath = m_directory / "gone.png", .size = QSize{10, 10}}, kNoLimits, kNow)));

    const auto loaded = store.load(kNoLimits, kNow);
    ASSERT_TRUE(static_cast<bool>(loaded));
    EXPECT_EQ(loaded->entries.size(), 1) << "文件不在的那条不该出现在列表里";
    EXPECT_EQ(loaded->entries[0].filePath, kept);
}

/**
 * @brief 钉住追加时按路径去重并按策略裁剪
 * @details 同一路径记两条会让删除一次只去掉一半；裁剪发生在写入之前，清单不会无限长。
 */
TEST_F(HistoryFixture, StoreReplacesSamePathAndPrunesOnAppend)
{
    HistoryStore store{storePath()};
    const auto   first  = makeArtifact("a.png");
    const auto   second = makeArtifact("b.png");
    const auto   third  = makeArtifact("c.png");
    const auto   policy = HistoryRetentionPolicy{.maximumEntries = 2, .retentionDays = 0};

    ASSERT_TRUE(static_cast<bool>(store.append(HistoryEntry{.moment = kNow.addDays(-2), .kind = HistoryKind::Screenshot, .filePath = first, .size = QSize{10, 10}}, policy, kNow)));
    ASSERT_TRUE(
            static_cast<bool>(store.append(HistoryEntry{.moment = kNow.addDays(-1), .kind = HistoryKind::Screenshot, .filePath = second, .size = QSize{10, 10}}, policy, kNow)));
    ASSERT_TRUE(static_cast<bool>(store.append(HistoryEntry{.moment = kNow, .kind = HistoryKind::Screenshot, .filePath = third, .size = QSize{10, 10}}, policy, kNow)));

    const auto loaded = store.load(policy, kNow);
    ASSERT_TRUE(static_cast<bool>(loaded));
    ASSERT_EQ(loaded->entries.size(), 2);
    EXPECT_EQ(loaded->entries[0].filePath, third);
    EXPECT_EQ(loaded->entries[1].filePath, second) << "最旧的一条已被条数上限裁掉";

    // 同一张图再记一次：换成新的时间，而不是并列两条
    ASSERT_TRUE(static_cast<bool>(store.append(HistoryEntry{.moment = kNow.addSecs(60), .kind = HistoryKind::Screenshot, .filePath = third, .size = QSize{20, 20}}, policy, kNow)));
    const auto again = store.load(policy, kNow);
    ASSERT_TRUE(static_cast<bool>(again));
    ASSERT_EQ(again->entries.size(), 2);
    EXPECT_EQ(again->entries[0].filePath, third) << "同一路径只能有一条，第二条不该是同一张图的旧记录";
    EXPECT_EQ(again->entries[1].filePath, second);
    EXPECT_EQ(again->entries[0].size, QSize(20, 20));
}

/**
 * @brief 钉住删除只去掉记录、不碰成品文件，且删不存在的项要报错
 * @details 用户的图由用户自己删；报「删掉了」而文件还在，或反过来静默删文件，都是最难查的那种不一致。
 */
TEST_F(HistoryFixture, RemoveClearsRecordOnlyAndRejectsUnknownPath)
{
    HistoryStore store{storePath()};
    const auto   artifact = makeArtifact("only-record.png");
    ASSERT_TRUE(static_cast<bool>(store.append(HistoryEntry{.moment = kNow, .kind = HistoryKind::Screenshot, .filePath = artifact, .size = QSize{10, 10}}, kNoLimits, kNow)));

    ASSERT_TRUE(static_cast<bool>(store.remove(artifact)));
    EXPECT_TRUE(std::filesystem::exists(artifact)) << "从历史里删掉不等于把成品文件删了";

    const auto loaded = store.load(kNoLimits, kNow);
    ASSERT_TRUE(static_cast<bool>(loaded));
    EXPECT_TRUE(loaded->entries.isEmpty());

    const auto missing = store.remove(m_directory / "never-was.png");
    ASSERT_FALSE(static_cast<bool>(missing));
    EXPECT_NE(missing.error().message.find("没有这条记录"), std::string::npos);
}

/**
 * @brief 钉住清空清单只清记录，并且留下的是能读的空清单
 * @details 两条各钉一件事：成品文件不许跟着一起消失（那是用户的东西，不是一键点击该做的），
 *          而清空后必须写得回一份读得懂的清单——留下一份截断的文件，下次启动就是「历史读不出来」。
 */
TEST_F(HistoryFixture, ClearEmptiesTheManifestAndKeepsTheArtifacts)
{
    HistoryStore store{storePath()};
    const auto   first  = makeArtifact("clear-first.png");
    const auto   second = makeArtifact("clear-second.png");
    for (const auto &path: {first, second})
    {
        ASSERT_TRUE(static_cast<bool>(store.append(HistoryEntry{.moment = kNow, .kind = HistoryKind::Screenshot, .filePath = path, .size = QSize{8, 8}}, kNoLimits, kNow)));
    }

    ASSERT_TRUE(static_cast<bool>(store.clear()));
    EXPECT_TRUE(std::filesystem::exists(first)) << "清列表把成品文件也删了";
    EXPECT_TRUE(std::filesystem::exists(second));

    const auto loaded = store.load(kNoLimits, kNow);
    ASSERT_TRUE(static_cast<bool>(loaded)) << "清空后清单读不出来了";
    EXPECT_TRUE(loaded->entries.isEmpty());
}

/**
 * @brief 钉住整份读不懂的清单仍整体报错并指出文件位置
 * @details 单条坏记录改为跳过，但根 JSON 都读不出来时没有任何可用的记录表，
 *          报错里必须给出文件位置让用户能删掉重建。
 */
TEST_F(HistoryFixture, CorruptManifestFailsWithPlaceToFix)
{
    {
        std::ofstream broken{storePath()};
        broken << "{ not json";
    }
    HistoryStore store{storePath()};
    const auto   unparsable = store.load(kNoLimits, kNow);
    ASSERT_FALSE(static_cast<bool>(unparsable));
    EXPECT_NE(unparsable.error().message.find("格式不对"), std::string::npos);
    EXPECT_NE(unparsable.error().message.find(storePath().generic_string()), std::string::npos) << "报错里没有清单路径，用户不知道去修哪一份";
}

/**
 * @brief 钉住一条坏记录只被跳过、原因留下，且写回时原样保留
 * @details 一条手改坏的记录不该让整份历史都读不出；坏条目也不能在下次 append 时被吞掉，
 *          否则用户修好那一格的机会也一起没了。只有 clear() 才整份清空。
 */
TEST_F(HistoryFixture, OneBadRecordIsSkippedAndPreserved)
{
    const auto artifact = makeArtifact("kept.png");
    {
        QJsonObject good;
        good.insert(QStringLiteral("path"), QString::fromStdWString(artifact.wstring()));
        good.insert(QStringLiteral("kind"), QStringLiteral("Screenshot"));
        good.insert(QStringLiteral("moment"), QStringLiteral("2026-10-10T12:00:00.000"));
        good.insert(QStringLiteral("width"), 10);
        good.insert(QStringLiteral("height"), 10);

        QJsonObject bad;
        bad.insert(QStringLiteral("path"), QStringLiteral("broken.png"));
        bad.insert(QStringLiteral("kind"), QStringLiteral("picture"));
        bad.insert(QStringLiteral("moment"), QStringLiteral("2026-10-10T12:00:00.000"));

        QJsonObject root;
        root.insert(QStringLiteral("entries"), QJsonArray{good, bad});

        QFile manifest{QString::fromStdWString(storePath().wstring())};
        ASSERT_TRUE(manifest.open(QIODevice::WriteOnly)) << "造不出用例前提：清单写不进去";
        manifest.write(QJsonDocument{root}.toJson());
    }

    HistoryStore store{storePath()};
    const auto   loaded = store.load(kNoLimits, kNow);
    ASSERT_TRUE(static_cast<bool>(loaded)) << "一条坏记录把整份历史都判成了读不出";
    ASSERT_EQ(loaded->entries.size(), 1);
    EXPECT_EQ(loaded->entries[0].filePath, artifact) << "好记录没有留下";
    EXPECT_EQ(loaded->skippedRecords, 1);
    EXPECT_FALSE(loaded->firstProblem.empty()) << "跳过了一条却没有留下原因，界面无从提示";

    const auto added = makeArtifact("added.png");
    ASSERT_TRUE(static_cast<bool>(store.append(HistoryEntry{.moment = kNow, .kind = HistoryKind::Screenshot, .filePath = added, .size = QSize{10, 10}}, kNoLimits, kNow)));

    const auto addedText   = QString::fromStdString(added.generic_string());
    const auto containsBad = [](const QJsonArray &entries)
    {
        return std::any_of(entries.begin(), entries.end(),
                           [](const QJsonValue &value)
                           {
                               const auto item = value.toObject();
                               return item.value(QStringLiteral("path")).toString() == QStringLiteral("broken.png") &&
                                      item.value(QStringLiteral("kind")).toString() == QStringLiteral("picture");
                           });
    };
    const auto containsAdded = [&addedText](const QJsonArray &entries)
    {
        return std::any_of(entries.begin(), entries.end(),
                           [&addedText](const QJsonValue &value) { return value.toObject().value(QStringLiteral("path")).toString() == addedText; });
    };

    const auto afterAppend = manifestEntries(storePath());
    ASSERT_EQ(afterAppend.size(), 3) << "append 后清单条目数不对";
    EXPECT_TRUE(containsBad(afterAppend)) << "append 之后坏记录被吞掉了，用户再也没有机会修好它";
    EXPECT_TRUE(containsAdded(afterAppend)) << "append 的新记录没有写进清单";

    ASSERT_TRUE(static_cast<bool>(store.remove(added)));
    const auto afterRemove = manifestEntries(storePath());
    ASSERT_EQ(afterRemove.size(), 2) << "remove 后清单条目数不对";
    EXPECT_TRUE(containsBad(afterRemove)) << "remove 之后坏记录被吞掉了";
    EXPECT_FALSE(containsAdded(afterRemove)) << "remove 没有去掉那条记录";

    ASSERT_TRUE(static_cast<bool>(store.clear()));
    EXPECT_TRUE(manifestEntries(storePath()).isEmpty()) << "clear() 之后清单里还留着记录";
    const auto afterClear = store.load(kNoLimits, kNow);
    ASSERT_TRUE(static_cast<bool>(afterClear));
    EXPECT_TRUE(afterClear->entries.isEmpty());
    EXPECT_EQ(afterClear->skippedRecords, 0);
}

/**
 * @brief 钉住清单文件不存在时是空历史而不是错误
 * @details 第一次启动没有 history.json 是正常形状，报「打不开」会让用户以为软件坏了。
 */
TEST_F(HistoryFixture, AbsentManifestLoadsAsEmptyHistory)
{
    HistoryStore store{m_directory / "does-not-exist.json"};
    const auto   loaded = store.load(kNoLimits, kNow);
    ASSERT_TRUE(static_cast<bool>(loaded));
    EXPECT_TRUE(loaded->entries.isEmpty());
}
