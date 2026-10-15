#include "History/HistoryThumbnail.h"
#include "History/HistoryView.h"

#include "History/HistoryStore.h"
#include "Settings/SettingsStore.h"
#include "Gui/Resources/IconProvider.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QPushButton>
#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QPoint>
#include <QSize>

#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>

using GyanisScribe::History::entryCaption;
using GyanisScribe::History::HistoryEntry;
using GyanisScribe::History::historyFilterLabels;
using GyanisScribe::History::HistoryKind;
using GyanisScribe::History::HistoryRetentionPolicy;
using GyanisScribe::History::HistoryStore;
using GyanisScribe::History::HistoryView;
using GyanisScribe::History::kindFilterAt;
using GyanisScribe::History::kThumbnailCornerRadius;
using GyanisScribe::History::kThumbnailEdge;
using GyanisScribe::History::makeThumbnail;
using GyanisScribe::Settings::SettingsStore;
using GyanisScribe::Gui::IconProvider;

namespace
{
    std::unique_ptr<QApplication> application;

    /// 用例里一律用这个「现在」，相对日期才有确定答案
    const QDateTime kNow{QDate{2026, 10, 10}, QTime{12, 0}};

    /// 在临时目录里存一张真 PNG：PNG 的读写由 Qt Gui 自带，不靠镜像格式插件
    std::filesystem::path writeImageArtifact(const std::filesystem::path &directory, const std::string &name, const QSize &size)
    {
        QImage image{size, QImage::Format_ARGB32};
        image.fill(QColor{40, 120, 200});
        const auto path = directory / name;
        // 存不下就当场红：造不出前提的用例会把自己想测的东西读成「文件被删了」
        EXPECT_TRUE(image.save(QString::fromStdWString(path.wstring()))) << name;
        return path;
    }

    /// 录屏成品：写一段占位字节即可，视图读不出像素时会退回类别图标
    std::filesystem::path writeVideoArtifact(const std::filesystem::path &directory, const std::string &name)
    {
        const auto path = directory / name;
        std::ofstream{path, std::ios::binary} << "fake mp4";
        return path;
    }

    class HistoryViewFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestHistoryView";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            // 目录、清单与设置都按用例名给独立路径：并行跑时共写一份会互相裁掉记录
            m_directory = std::filesystem::temp_directory_path() / std::format("GyanisScribe-HistoryView-{}", ::testing::UnitTest::GetInstance()->current_test_info()->name());
            std::error_code cleanup;
            std::filesystem::remove_all(m_directory, cleanup);
            std::filesystem::create_directories(m_directory);

            m_storePath = m_directory / "history.json";
            m_iniPath   = m_directory / "settings.ini";
            m_store     = std::make_unique<HistoryStore>(m_storePath);
            m_settings  = std::make_unique<SettingsStore>(m_iniPath);
            m_icons     = std::make_unique<IconProvider>();
        }

        void TearDown() override
        {
            std::error_code cleanup;
            std::filesystem::remove_all(m_directory, cleanup);
        }

        /// 记一条成品（时间由用例给，避免依赖机器时钟）
        void addRecord(const std::filesystem::path &path, const HistoryKind kind, const QDateTime &moment)
        {
            ASSERT_TRUE(static_cast<bool>(m_store->append(HistoryEntry{.moment = moment, .kind = kind, .filePath = path, .size = QSize{640, 480}},
                                                          HistoryRetentionPolicy{.maximumEntries = 0, .retentionDays = 0}, kNow)));
        }

        [[nodiscard]] std::unique_ptr<HistoryView> makeView() const
        {
            return std::make_unique<HistoryView>(*m_store, HistoryRetentionPolicy{.maximumEntries = 0, .retentionDays = 0}, *m_icons);
        }

        std::filesystem::path          m_directory; ///< 本用例的临时目录
        std::filesystem::path          m_storePath; ///< 历史清单
        std::filesystem::path          m_iniPath;   ///< 本用例的设置文件
        std::unique_ptr<HistoryStore>  m_store;     ///< 被测清单
        std::unique_ptr<SettingsStore> m_settings;  ///< 配置存储
        std::unique_ptr<IconProvider>  m_icons;     ///< 图标供给器
    };

    /// 按无障碍名字找历史页工具条上的一枚按钮
    [[nodiscard]] QPushButton *buttonOf(const HistoryView &view, const QString &accessibleName)
    {
        const auto candidates = view.findChildren<QPushButton *>();
        for (auto *candidate: candidates)
        {
            if (candidate->accessibleName() == accessibleName)
            {
                return candidate;
            }
        }
        return nullptr;
    }

} // namespace

/**
 * @brief 钉住「打开所在目录」与「清空列表」这两枚键在空列表上的出口
 * @details 空列表时两枚都不该有下游动作：一个会去开一个不存在的目录，另一个会弹一次没意义的确认框，
 *          而确认框在无人点击的用例里就是永远等不到的那一发。
 */
TEST_F(HistoryViewFixture, EmptyListGuidesInsteadOfActing)
{
    auto view = makeView();
    view->refresh();

    auto *folder = buttonOf(*view, QStringLiteral("打开所在目录"));
    auto *clear  = buttonOf(*view, QStringLiteral("清空列表"));
    ASSERT_NE(folder, nullptr) << "历史页该有一枚打开所在目录";
    ASSERT_NE(clear, nullptr) << "历史页该有一枚清空列表";
    EXPECT_FALSE(folder->icon().isNull()) << "图标名拼错或资源没进 qrc";
    EXPECT_FALSE(clear->icon().isNull()) << "图标名拼错或资源没进 qrc";

    int     opens  = 0;
    int     clears = 0;
    QString guidance;
    QObject::connect(view.get(), &HistoryView::openFolderRequested, view.get(), [&opens](const std::filesystem::path &) { ++opens; });
    QObject::connect(view.get(), &HistoryView::clearAllRequested, view.get(), [&clears] { ++clears; });
    QObject::connect(view.get(), &HistoryView::messageForUser, view.get(), [&guidance](const QString &text, GyanisScribe::Gui::ToastKind) { guidance = text; });

    folder->click();
    EXPECT_EQ(opens, 0) << "没选中就去了开目录";
    EXPECT_NE(guidance.indexOf(QStringLiteral("先在历史里选一张图")), -1) << guidance.toStdString();

    guidance.clear();
    clear->click();
    EXPECT_EQ(clears, 0) << "空列表还去弹确认框";
    EXPECT_NE(guidance.indexOf(QStringLiteral("空")), -1) << guidance.toStdString();
}

/**
 * @brief 钉住列表文案的三种日期写法与固定分段
 * @details 期望值逐字写死：相对日期算错一天，用户就会在「今天」里找上周的图。
 */
TEST(HistoryViewMapping, CaptionUsesTodayYesterdayThenAbsoluteDate)
{
    const auto today = HistoryEntry{.moment = QDateTime{QDate{2026, 10, 10}, QTime{9, 5}}, .kind = HistoryKind::Screenshot, .filePath = "shot.png", .size = QSize{800, 600}};
    EXPECT_EQ(entryCaption(today, kNow), QString::fromUtf8("今天 09:05 · 截图 · 800×600"));

    auto yesterday   = today;
    yesterday.moment = QDateTime{QDate{2026, 10, 9}, QTime{23, 59}};
    EXPECT_EQ(entryCaption(yesterday, kNow), QString::fromUtf8("昨天 23:59 · 截图 · 800×600")) << "差一天就是昨天，跨过午夜不该还写今天";

    auto older   = today;
    older.moment = QDateTime{QDate{2026, 9, 28}, QTime{7, 30}};
    older.kind   = HistoryKind::Recording;
    EXPECT_EQ(entryCaption(older, kNow), QString::fromUtf8("2026-09-28 07:30 · 录屏 · 800×600"));
}

/**
 * @brief 钉住尺寸读不出时列表文案不带那一格
 * @details 默认构造的 QSize 是 (-1,-1)，旧清单里还留着 0×0 的记录，两种都代表「不知道」：
 *          打成「0×0」会被读成「这张成品是 0 像素」，而不是「这一轮没落下帧」。
 */
TEST(HistoryViewMapping, CaptionDropsTheSizeSegmentWhenItIsNotReadable)
{
    const auto moment = QDateTime{QDate{2026, 10, 10}, QTime{9, 5}};

    EXPECT_EQ(entryCaption(HistoryEntry{.moment = moment, .kind = HistoryKind::Recording, .filePath = "clip.mp4", .size = QSize{}}, kNow), QString::fromUtf8("今天 09:05 · 录屏"));
    EXPECT_EQ(entryCaption(HistoryEntry{.moment = moment, .kind = HistoryKind::Recording, .filePath = "clip.mp4", .size = QSize{0, 0}}, kNow),
              QString::fromUtf8("今天 09:05 · 录屏"));

    EXPECT_EQ(entryCaption(HistoryEntry{.moment = moment, .kind = HistoryKind::Recording, .filePath = "clip.mp4", .size = QSize{8, 8}}, kNow),
              QString::fromUtf8("今天 09:05 · 录屏 · 8×8"))
            << "读得出尺寸时那一格还得在，不然凭空少一个读数";
}

/**
 * @brief 钉住筛选下拉的标签与档位互指
 * @details 只测 kindFilterAt 的话，把表重排也照样全绿，而用户看到的是标签：
 *          选「录屏」却列出截图就是这里漏出来的。标签沿用 historyKindLabel，改中文时两处同改。
 */
TEST(HistoryViewMapping, FilterLabelsAndKindsPointAtEachOther)
{
    const QStringList labels = historyFilterLabels();
    ASSERT_EQ(labels.size(), 3);
    EXPECT_EQ(labels[0], QString::fromUtf8("全部"));
    EXPECT_EQ(labels[1], QString::fromUtf8("截图"));
    EXPECT_EQ(labels[2], QString::fromUtf8("录屏"));

    EXPECT_FALSE(kindFilterAt(0).has_value());
    ASSERT_TRUE(kindFilterAt(1).has_value());
    EXPECT_EQ(*kindFilterAt(1), HistoryKind::Screenshot);
    ASSERT_TRUE(kindFilterAt(2).has_value());
    EXPECT_EQ(*kindFilterAt(2), HistoryKind::Recording);
    EXPECT_FALSE(kindFilterAt(9).has_value()) << "越界按不限处理，不要让列表空掉";
}

/**
 * @brief 钉住缩略图是统一尺寸且四角被圆角切掉
 * @details 尺寸不统一，网格里就看不出是同一批；圆角没裁，四角会露出图外的白。
 */
TEST(HistoryThumbnail, SquaresAreUniformAndCornersAreCut)
{
    QImage source{QSize{240, 120}, QImage::Format_ARGB32};
    source.fill(QColor{200, 30, 30});

    const auto thumb = makeThumbnail(source, {kThumbnailEdge, kThumbnailEdge}, kThumbnailCornerRadius);
    ASSERT_EQ(thumb.size(), QSize(kThumbnailEdge, kThumbnailEdge));
    EXPECT_EQ(thumb.pixelColor(0, 0).alpha(), 0) << "左上角在圆角之外";
    EXPECT_GT(thumb.pixelColor(kThumbnailEdge / 2, kThumbnailEdge / 2).alpha(), 0) << "中心必须还是图本身";

    EXPECT_TRUE(makeThumbnail(QImage{}, {kThumbnailEdge, kThumbnailEdge}, kThumbnailCornerRadius).isNull());
    EXPECT_TRUE(makeThumbnail(source, {0, 0}, kThumbnailCornerRadius).isNull());
}

/**
 * @brief 钉住视图建立就自读清单，初读不靠外部触发
 * @details 程序重启后第一次打开这一页时盘上没有新增：初读若交给调用方，列表就是空的，
 *          而空列表读起来与「记录丢了」一模一样。这一条只构造视图、不调 refresh()。
 */
TEST_F(HistoryViewFixture, ConstructorReadsTheManifestOnItsOwn)
{
    const auto first  = writeImageArtifact(m_directory, "first.png", {640, 480});
    const auto second = writeImageArtifact(m_directory, "second.png", {640, 480});
    addRecord(first, HistoryKind::Screenshot, kNow.addDays(-1));
    addRecord(second, HistoryKind::Screenshot, kNow);

    const auto view = makeView();
    EXPECT_EQ(view->rowCount(), 2) << "构造没读清单：这一页要等下一次保存才把旧记录列出来";
}

/**
 * @brief 钉住列表按时间倒序列出、搜索能收窄、选中项给回路径
 * @details 列表是历史页唯一的读数：少一行或次序反了，用户会以为成品丢了。
 */
TEST_F(HistoryViewFixture, ListShowsNewestFirstAndNarrowsBySearch)
{
    const auto newest = writeVideoArtifact(m_directory, "newest.mp4");
    const auto older  = writeImageArtifact(m_directory, "older.png", {640, 480});
    addRecord(newest, HistoryKind::Recording, kNow);
    addRecord(older, HistoryKind::Screenshot, kNow.addDays(-1));

    auto view = makeView();
    view->refresh();
    ASSERT_EQ(view->rowCount(), 2);
    EXPECT_NE(view->firstCaption().toStdString().find("录屏"), std::string::npos) << "第一行应是刚记的那条录屏";

    const auto selected = view->selectedPath();
    EXPECT_FALSE(selected.has_value()) << "没有选中项时不该给出一个路径";
}

/**
 * @brief 钉住清单读不出来时保留上一次的列表
 * @details 把列表清成空白，用户会以为成品没了；留着旧行至少还能顺着路径找到文件。
 */
TEST_F(HistoryViewFixture, BrokenKeepsPreviousRowsAndTellsTheUser)
{
    const auto artifact = writeImageArtifact(m_directory, "kept.png", {320, 200});
    addRecord(artifact, HistoryKind::Screenshot, kNow);

    auto view = makeView();
    view->refresh();
    ASSERT_EQ(view->rowCount(), 1);

    {
        std::ofstream broken{m_storePath};
        broken << "{ broken";
    }

    QString message;
    int     emitted = 0;
    auto    viewPtr = view.get();
    QObject::connect(viewPtr, &HistoryView::messageForUser, viewPtr,
                     [&message, &emitted](const QString &text, GyanisScribe::Gui::ToastKind)
                     {
                         ++emitted;
                         message = text;
                     });
    view->refresh();

    EXPECT_EQ(emitted, 1) << "读不出清单必须说一声，不能安静地把列表刷成空";
    EXPECT_NE(message.toStdString().find("历史清单没能读出来"), std::string::npos);
    EXPECT_EQ(view->rowCount(), 1) << "上一次的记录要留着，让用户还能顺着路径找文件";
}
