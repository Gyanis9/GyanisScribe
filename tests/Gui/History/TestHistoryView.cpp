#include "Core/History/HistoryThumbnail.h"
#include "Gui/History/HistoryView.h"

#include "Core/History/HistoryStore.h"
#include "Core/Settings/SettingsStore.h"
#include "Gui/Resources/IconProvider.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QColor>
#include <QDateTime>
#include <QIcon>
#include <QImage>
#include <QPoint>
#include <QSize>

#include <cstdio>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

using GyanisScribe::Core::HistoryEntry;
using GyanisScribe::Core::HistoryKind;
using GyanisScribe::Core::HistoryRetentionPolicy;
using GyanisScribe::Core::HistoryStore;
using GyanisScribe::Core::kThumbnailCornerRadius;
using GyanisScribe::Core::kThumbnailEdge;
using GyanisScribe::Core::makeThumbnail;
using GyanisScribe::Core::SettingsStore;
using GyanisScribe::Gui::entryCaption;
using GyanisScribe::Gui::historyFilterLabels;
using GyanisScribe::Gui::HistoryView;
using GyanisScribe::Gui::IconProvider;
using GyanisScribe::Gui::kindFilterAt;

namespace
{
    std::unique_ptr<QApplication> application;

    /// 用例里一律用这个「现在」，相对日期才有确定答案
    const QDateTime kNow{QDate{2026, 10, 10}, QTime{12, 0}};

    /// 在临时目录里存一张真 PNG：PNG 的读写由 Qt Gui 自带，不靠镜像格式插件
    std::filesystem::path writeImageArtifact(const std::filesystem::path &directory, const std::string &name, const QSize &size, const QColor &fill = QColor{40, 120, 200})
    {
        QImage image{size, QImage::Format_ARGB32};
        image.fill(fill);
        const auto path = directory / name;
        // 存不下就当场红：造不出前提的用例会把自己想测的东西读成「文件被删了」
        EXPECT_TRUE(image.save(QString::fromStdWString(path.wstring()))) << name;
        return path;
    }

    /// 把文件补零到指定字节数：PNG 读完 IEND 就收尾，尾部的填充不会被当成像素
    void padFileTo(const std::filesystem::path &path, const std::uintmax_t size)
    {
        const auto current = std::filesystem::file_size(path);
        if (current >= size)
        {
            return;
        }
        std::ofstream stream{path, std::ios::binary | std::ios::app};
        stream << std::string(static_cast<std::size_t>(size - current), '\0');
    }

    /// 列表项图标中心处的颜色与期望色是否一致（圆角只切四个角，中心一定是图本身）
    [[nodiscard]] bool iconCenterMatches(const QIcon &icon, const QColor &expected)
    {
        const auto bitmap = icon.pixmap(QSize{kThumbnailEdge, kThumbnailEdge});
        if (bitmap.isNull())
        {
            return false;
        }
        const auto image  = bitmap.toImage();
        const auto center = image.pixelColor(image.width() / 2, image.height() / 2);
        return center.alpha() > 0 && qAbs(center.red() - expected.red()) <= 2 && qAbs(center.green() - expected.green()) <= 2 && qAbs(center.blue() - expected.blue()) <= 2;
    }

    /**
     * @brief 处理事件直到列表里每一行的缩略图都显示成期望色
     * @details 形状判据而不是计时判据：返回值只说明「解完了没有」，耗时由调用方另量。
     *          旧实现解码是同步的，第一轮检查就已经成立；按需解码要等定时器一轮轮跑完。
     */
    [[nodiscard]] bool waitForThumbnails(const QListWidget &list, const QColor &expected, const qint64 timeoutMilliseconds)
    {
        QElapsedTimer watch;
        watch.start();
        while (watch.elapsed() < timeoutMilliseconds)
        {
            bool all = list.count() > 0;
            for (int row = 0; row < list.count() && all; ++row)
            {
                all = iconCenterMatches(list.item(row)->icon(), expected);
            }
            if (all)
            {
                return true;
            }
            QApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        return false;
    }

    /// 处理事件直到视图解过 expected 张缩略图
    [[nodiscard]] bool waitForDecodeCount(const HistoryView &view, const int expected, const qint64 timeoutMilliseconds)
    {
        QElapsedTimer watch;
        watch.start();
        while (watch.elapsed() < timeoutMilliseconds)
        {
            if (view.thumbnailDecodeCountForTesting() >= expected)
            {
                return true;
            }
            QApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        return view.thumbnailDecodeCountForTesting() >= expected;
    }

    /// 空转一段时间：用来证明这段时间里没有新的解码发生
    void pumpEvents(const qint64 milliseconds)
    {
        QElapsedTimer watch;
        watch.start();
        while (watch.elapsed() < milliseconds)
        {
            QApplication::processEvents(QEventLoop::AllEvents, 5);
        }
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
 * @brief 钉住空列表与「筛不出记录」各有自己的引导句（界面审查 UI-64）
 * @details 一片白卡读起来像记录丢了或窗体坏了；两种空又不该共用一句——筛选没命中时说
 *          「还没有记录」是在撒谎。判据读的是视口里那句提示的文字与显示态。
 */
TEST_F(HistoryViewFixture, EmptyListAndNoMatchesExplainThemselves)
{
    auto view = makeView();
    view->refresh();
    auto *hint = view->findChild<QLabel *>(QStringLiteral("pageHint"));
    ASSERT_NE(hint, nullptr) << "空态没有引导句";
    EXPECT_FALSE(hint->isHidden()) << "空列表时引导句没露出来";
    EXPECT_NE(hint->text().indexOf(QStringLiteral("还没有记录")), -1) << hint->text().toStdString();

    const auto artifact = writeImageArtifact(m_directory, "kept.png", {320, 200});
    addRecord(artifact, HistoryKind::Screenshot, kNow);
    view->refresh();
    EXPECT_TRUE(hint->isHidden()) << "有记录时引导句还盖在列表上";

    auto *search = view->findChild<QLineEdit *>();
    ASSERT_NE(search, nullptr);
    search->setText(QStringLiteral("no-such-file"));
    EXPECT_EQ(view->rowCount(), 0);
    EXPECT_FALSE(hint->isHidden()) << "筛不出记录时没有提示";
    EXPECT_NE(hint->text().indexOf(QStringLiteral("没有符合条件")), -1) << hint->text().toStdString();
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
 * @brief 钉住第一次显示才读清单，构造期不碰盘
 * @details 程序重启后第一次打开这一页时盘上没有新增：初读若交给下一次保存，列表就是空的，
 *          而空列表读起来与「记录丢了」一模一样。但这一页挂在设置窗里、通常还不是第一页，
 *          构造期读清单会顺带解两百张缩略图，设置窗还没露面就先卡住。因此构造后行数为 0，
 *          show() 之后才把记录列出来。
 */
TEST_F(HistoryViewFixture, FirstShowReadsTheManifestOnItsOwn)
{
    const auto first  = writeImageArtifact(m_directory, "first.png", {640, 480});
    const auto second = writeImageArtifact(m_directory, "second.png", {640, 480});
    addRecord(first, HistoryKind::Screenshot, kNow.addDays(-1));
    addRecord(second, HistoryKind::Screenshot, kNow);

    const auto view = makeView();
    EXPECT_EQ(view->rowCount(), 0) << "构造期就把清单读进来了：设置窗打开时整页都要为看不见的缩略图付解码钱";
    EXPECT_EQ(view->thumbnailDecodeCountForTesting(), 0) << "构造期解了缩略图";

    view->show();
    QApplication::processEvents();
    EXPECT_EQ(view->rowCount(), 2) << "第一次显示没读清单：这一页要等下一次保存才把旧记录列出来";
}

/**
 * @brief 钉住同一批文件第二次刷新不再解码
 * @details 搜索每次键入都会重建列表；旧实现逐条现读现裁，两百条历史每敲一个字就解两百张 4K 图。
 *          计数只认真正的解码尝试：命中缓存时列表图标照换，但计数不动。
 */
TEST_F(HistoryViewFixture, ThumbnailsAreCachedAcrossSearch)
{
    const auto first  = writeImageArtifact(m_directory, "cached-one.png", {640, 480});
    const auto second = writeImageArtifact(m_directory, "cached-two.png", {640, 480});
    addRecord(first, HistoryKind::Screenshot, kNow.addDays(-1));
    addRecord(second, HistoryKind::Screenshot, kNow);

    auto view = makeView();
    ASSERT_EQ(view->thumbnailDecodeCountForTesting(), 0) << "构造期就解了缩略图";

    view->show();
    ASSERT_TRUE(waitForDecodeCount(*view, 2, 10'000)) << "首显没把两张缩略图解出来，缓存用例的前提不成立";
    ASSERT_EQ(view->rowCount(), 2);

    auto *search = view->findChild<QLineEdit *>();
    ASSERT_NE(search, nullptr) << "历史页里没有搜索框";
    search->setText(QStringLiteral("cached"));
    view->refresh();
    ASSERT_EQ(view->rowCount(), 2) << "搜索词该同时命中两张";

    // 给按需解码留出足够的轮次：缓存没命中时这里会多解两张
    pumpEvents(300);
    EXPECT_EQ(view->thumbnailDecodeCountForTesting(), 2) << "同一批文件第二次刷新又解了一遍：缓存没命中";
}

/**
 * @brief 钉住成品内容变了要重解，没变的仍走缓存
 * @details 缓存键含修改时间：同一路径被新图覆盖时旧缩略图必须换掉，否则列表一直显示上一版。
 *          只该重解改了的那一张——两张都重解说明缓存键里没有修改时间。
 */
TEST_F(HistoryViewFixture, ChangedFileInvalidatesItsThumbnail)
{
    const QColor kOriginal{40, 120, 200};
    const QColor kReplacement{220, 30, 30};

    const auto changed     = writeImageArtifact(m_directory, "changed.png", {640, 480}, kOriginal);
    const auto replacement = writeImageArtifact(m_directory, "replacement.png", {640, 480}, kReplacement);
    const auto kept        = writeImageArtifact(m_directory, "kept.png", {640, 480}, kOriginal);

    // 两份内容不同的 PNG 补到同一字节数：后面覆盖时文件大小不变，缓存键里只剩修改时间能变
    const auto paddedSize = std::max(std::filesystem::file_size(changed), std::filesystem::file_size(replacement)) + 4096;
    padFileTo(changed, paddedSize);
    padFileTo(replacement, paddedSize);
    ASSERT_EQ(std::filesystem::file_size(changed), std::filesystem::file_size(replacement)) << "两份文件没补成同样大小，失效判据会误读成大小变了";

    addRecord(changed, HistoryKind::Screenshot, kNow.addDays(-1));
    addRecord(kept, HistoryKind::Screenshot, kNow);

    auto view = makeView();
    view->show();
    ASSERT_TRUE(waitForDecodeCount(*view, 2, 10'000)) << "首显没把两张缩略图解出来，失效判据的前提不成立";
    ASSERT_EQ(view->thumbnailDecodeCountForTesting(), 2);

    auto *list = view->findChild<QListWidget *>();
    ASSERT_NE(list, nullptr) << "历史页里没有缩略图列表";
    ASSERT_EQ(list->count(), 2);

    // 同一路径换成新内容并推到将来的修改时间：大小不变，只有修改时间这一格变了
    std::error_code copied;
    std::filesystem::copy_file(replacement, changed, std::filesystem::copy_options::overwrite_existing, copied);
    ASSERT_FALSE(copied) << "覆盖成品失败：" << copied.message();
    std::error_code touched;
    std::filesystem::last_write_time(changed, std::filesystem::file_time_type::clock::now() + std::chrono::seconds{10}, touched);
    ASSERT_FALSE(touched) << "改不动修改时间，失效判据的前提不成立：" << touched.message();

    view->refresh();
    ASSERT_TRUE(waitForDecodeCount(*view, 3, 10'000)) << "换了内容的文件没有重解";
    pumpEvents(100);
    EXPECT_EQ(view->thumbnailDecodeCountForTesting(), 3) << "只该重解改了的那一张；两张都重解说明缓存键里没有修改时间";
    // 列表按时间倒序：改了的那条较早，排在第二行
    EXPECT_TRUE(iconCenterMatches(list->item(1)->icon(), kReplacement)) << "列表项还是旧图：修改时间没有进缓存键";
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

/**
 * @brief 钉住历史列表的类别图标按 96 原生档取
 * @details 缩略图读不出时列表仍显示类别图标；列表 iconSize 是 96，图标必须备 96。
 *          只查供给器不算接线：这里放一条读不出像素的录屏成品，让视图自己走到兜底分支，
 *          再从列表项上取回那一枚图标。
 */
TEST_F(HistoryViewFixture, CategoryFallbackIconCarriesNinetySix)
{
    const auto video = writeVideoArtifact(m_directory, "clip.mp4");
    addRecord(video, HistoryKind::Recording, kNow);

    auto view = makeView();
    view->refresh();
    ASSERT_EQ(view->rowCount(), 1);

    auto *list = view->findChild<QListWidget *>();
    ASSERT_NE(list, nullptr) << "历史页里没有缩略图列表";
    const auto icon = list->item(0)->icon();
    ASSERT_FALSE(icon.isNull()) << "读不出缩略图时列表项没有兜底图标";
    EXPECT_TRUE(icon.availableSizes().contains(QSize(96, 96))) << "列表按 96px 摆图标，兜底图却没有 96 原生档，会被放大发糊";
}

/**
 * @brief 钉住历史列表锁等宽数字
 * @details 每张卡片的时间与尺寸都是读数；比例字体会让同一列的数字随条目左右跳。
 *          列表按 Mixed 只上 tnum：卡片里还有中文标题，不换字族也不降字重。
 */
TEST_F(HistoryViewFixture, ListUsesTabularFigures)
{
    auto  view = makeView();
    auto *list = view->findChild<QListWidget *>();
    ASSERT_NE(list, nullptr) << "历史页里没有缩略图列表";
    EXPECT_EQ(list->accessibleName(), QStringLiteral("历史列表"));
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    EXPECT_EQ(list->font().featureValue(QFont::Tag("tnum")), 1u) << "历史列表没锁等宽，卡片读数会左右跳";
#endif
}

/**
 * @brief 读数用例：200 条 4K 成品时，构造加首次显示、以及第二次搜索刷新各要多久（不断言时间）
 * @details 一份 4K PNG 复制成 200 个名字，清单里记 200 条：旧实现构造期逐条同步解码，
 *          第二次搜索又把同一批全解一遍；新实现首次显示才读、按修改时间缓存，第二次应当只剩命中。
 *          这里只报「列表里每一行都换成真缩略图」所需的时间，跑在什么机器上就报什么数。
 */
TEST_F(HistoryViewFixture, ReportsTheCostOfRefreshingTwoHundredFourKHistoryEntries)
{
    constexpr int    kEntries = 200;
    const QSize      kSourceSize{3840, 2160};
    const QColor     kSourceColor{40, 120, 200};
    constexpr qint64 kTimeoutMilliseconds = 60'000;

    const auto seed = writeImageArtifact(m_directory, "slide-seed.png", kSourceSize);
    for (int index = 0; index < kEntries; ++index)
    {
        const auto      name   = std::format("slide-{:03}.png", index);
        const auto      target = m_directory / name;
        std::error_code copied;
        std::filesystem::copy_file(seed, target, std::filesystem::copy_options::overwrite_existing, copied);
        ASSERT_FALSE(copied) << "复制第 " << index << " 份 4K 成品失败：" << copied.message();
        addRecord(target, HistoryKind::Screenshot, kNow.addSecs(-index));
    }

    QElapsedTimer firstShow;
    firstShow.start();
    auto  view = makeView();
    auto *list = view->findChild<QListWidget *>();
    ASSERT_NE(list, nullptr) << "历史页里没有缩略图列表";

    view->show();
    const auto windowReadyMilliseconds = firstShow.elapsed();
    ASSERT_TRUE(waitForThumbnails(*list, kSourceColor, kTimeoutMilliseconds)) << "首屏缩略图没在时限内解完，这次的读数不成立";
    const auto firstShowMilliseconds = firstShow.elapsed();

    auto *search = view->findChild<QLineEdit *>();
    ASSERT_NE(search, nullptr) << "历史页里没有搜索框";

    // 搜索词命中全部 200 条：列表整批重建，旧实现会在这里把同一批图全解第二遍
    QElapsedTimer searchRefresh;
    searchRefresh.start();
    search->setText(QStringLiteral("slide"));
    const auto searchReturnMilliseconds = searchRefresh.elapsed();
    ASSERT_TRUE(waitForThumbnails(*list, kSourceColor, kTimeoutMilliseconds)) << "搜索刷新后的缩略图没在时限内解完，这次的读数不成立";
    const auto searchRefreshMilliseconds = searchRefresh.elapsed();

    std::printf("[读数] 历史页 200 条 4K：构造+显示返回 %lldms；首屏缩略图全部就绪 %lldms；搜索刷新返回 %lldms；刷新后缩略图全部就绪 %lldms\n",
                static_cast<long long>(windowReadyMilliseconds), static_cast<long long>(firstShowMilliseconds), static_cast<long long>(searchReturnMilliseconds),
                static_cast<long long>(searchRefreshMilliseconds));
}
