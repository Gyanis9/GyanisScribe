// 覆盖记忆库页的重复图合并、搜索防抖、空态与横幅、筛选、选中动作、缺失文件与导入进度对话框

#include "Core/Memory/DuplicateHash.h"
#include "Core/Memory/MemoryIndex.h"
#include "Core/Memory/MemoryTypes.h"
#include "Gui/Memory/ImportProgressDialog.h"
#include "Gui/Memory/MemoryExcludeDialog.h"
#include "Gui/Memory/MemoryView.h"
#include "Gui/Resources/IconProvider.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFont>
#include <QEvent>
#include <QEventLoop>
#include <QByteArray>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Core = GyanisScribe::Core;
using GyanisScribe::Gui::IconProvider;
using GyanisScribe::Gui::ImportProgressDialog;
using GyanisScribe::Gui::MemoryExcludeDialog;
using GyanisScribe::Gui::MemoryView;

namespace
{
    std::unique_ptr<QApplication> application;

    /// 有界等待：条件成立才返回，超时返回最后一次判定
    template<typename Predicate>
    [[nodiscard]] bool waitUntil(Predicate predicate, const qint64 timeoutMilliseconds)
    {
        QElapsedTimer watch;
        watch.start();
        while (watch.elapsed() < timeoutMilliseconds)
        {
            if (predicate())
            {
                return true;
            }
            QApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return predicate();
    }

    /// 竖条纹或棋盘格：两种图案的 dHash 距离明显大于合并阈值
    [[nodiscard]] QImage patternImage(const bool checkerboard)
    {
        QImage image{QSize{128, 96}, QImage::Format_RGB32};
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const int value = checkerboard ? (((x / 8) + (y / 8)) % 2 == 0 ? 255 : 0) : std::min(255, x * 2);
                image.setPixelColor(x, y, QColor{value, value, value});
            }
        }
        return image;
    }

    [[nodiscard]] QString unknownProcessFilter()
    {
        return QString::fromLatin1(Core::kUnknownProcessFilter.data(), static_cast<qsizetype>(Core::kUnknownProcessFilter.size()));
    }

    class MemoryViewFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestGuiMemory";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }

        void SetUp() override
        {
            ASSERT_TRUE(m_temp.isValid());
            m_index = std::make_unique<Core::MemoryIndex>(std::filesystem::path(m_temp.path().toStdString()) / "memory.sqlite");
            ASSERT_TRUE(m_index->open().has_value());
            m_icons = std::make_unique<IconProvider>();
        }

        void TearDown() override
        {
            // 在飞解码可能还握着临时目录里的图片句柄：先落删除事件，再等线程池排空
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            ASSERT_TRUE(QThreadPool::globalInstance()->waitForDone(10'000)) << "全局线程池没在时限内排空";
        }

        [[nodiscard]] std::filesystem::path writeImage(const std::string &name, const QImage &image) const
        {
            const auto path = std::filesystem::path(m_temp.path().toStdString()) / name;
            EXPECT_TRUE(image.save(QString::fromStdWString(path.wstring()))) << name;
            return path;
        }

        /// 写一张图并入索引，识别结果直接写成给定文本
        qint64 addResult(const std::filesystem::path &path, const QImage &image, const QString &text, const QDateTime &moment, const QString &processName,
                         const QString &windowTitle, const Core::MemorySource source = Core::MemorySource::Capture)
        {
            const Core::MemoryUpsert entry{
                    .filePath    = path,
                    .source      = source,
                    .capturedAt  = moment,
                    .size        = image.size(),
                    .processName = processName,
                    .windowTitle = windowTitle,
            };
            const auto id = m_index->upsertPending(entry);
            EXPECT_TRUE(id.has_value()) << (id ? "" : id.error().message);
            if (!id)
            {
                return 0;
            }
            const QString dhash = Core::computeDHash(image);
            EXPECT_TRUE(m_index->updateOcr(*id, text, dhash, QStringLiteral("done"), QString{}, false).has_value());
            return *id;
        }

        /// 只入索引不落文件：用来造「文件已不在」与无 dHash 的行
        qint64 addIndexOnly(const std::filesystem::path &path, const QString &text, const QDateTime &moment, const QString &processName)
        {
            const Core::MemoryUpsert entry{
                    .filePath    = path,
                    .source      = Core::MemorySource::Import,
                    .capturedAt  = moment,
                    .size        = QSize{640, 480},
                    .processName = processName,
            };
            const auto id = m_index->upsertPending(entry);
            EXPECT_TRUE(id.has_value());
            if (!id)
            {
                return 0;
            }
            EXPECT_TRUE(m_index->updateOcr(*id, text, QString{}, QStringLiteral("done"), QString{}, false).has_value());
            return *id;
        }

        /// 两条相同图案（dHash 距离 0）加一条不同图案；不同的那条最新，排在首行
        struct Seeded
        {
            std::filesystem::path duplicateOld;
            std::filesystem::path duplicateNew;
            std::filesystem::path distinct;
            qint64                distinctId = 0;
        };

        [[nodiscard]] Seeded seedDuplicateGroup()
        {
            const auto duplicate = patternImage(false);
            const auto distinct  = patternImage(true);
            EXPECT_GT(Core::hammingDistance(Core::computeDHash(duplicate), Core::computeDHash(distinct)), Core::kDuplicateHammingThreshold)
                    << "造出的两张图 dHash 太近，合并用例的前提不成立";

            Seeded seeded;
            seeded.duplicateOld = writeImage("duplicate-old.png", duplicate);
            seeded.duplicateNew = writeImage("duplicate-new.png", duplicate);
            seeded.distinct     = writeImage("distinct.png", distinct);

            const QDateTime now = QDateTime::currentDateTime();
            addResult(seeded.duplicateOld, duplicate, QStringLiteral("旧的一版"), now.addSecs(-7200), QStringLiteral("code"), QStringLiteral("笔记"));
            addResult(seeded.duplicateNew, duplicate, QStringLiteral("重复截图"), now.addSecs(-3600), QStringLiteral("code"), QStringLiteral("笔记"));
            seeded.distinctId = addResult(seeded.distinct, distinct, QStringLiteral("项目周报 第三版"), now, QStringLiteral("browser"), QStringLiteral("周报"));
            return seeded;
        }

        [[nodiscard]] MemoryView makeView() const
        {
            return MemoryView{*m_index, *m_icons};
        }

        [[nodiscard]] static QPushButton *buttonOf(const MemoryView &view, const QString &objectName)
        {
            return view.findChild<QPushButton *>(objectName);
        }

        QTemporaryDir                      m_temp;  ///< 本用例的临时目录
        std::unique_ptr<Core::MemoryIndex> m_index; ///< 被测索引
        std::unique_ptr<IconProvider>      m_icons; ///< 图标供给器
    };

} // namespace

/**
 * @brief 钉住重复图合并默认开启、关闭后列出全部结果
 * @details 两条 dHash 距离 0 的截图折成组头加最新一条，连同另一组的一张共三行；
 *          折叠时组内旧的不出现，关掉开关后三条卡片全部列出、不再有组头。
 */
TEST_F(MemoryViewFixture, MergesDuplicateResultsOnlyWhenEnabled)
{
    static_cast<void>(seedDuplicateGroup());
    auto view = makeView();
    view.refresh();

    auto *list = view.findChild<QListWidget *>(QStringLiteral("memoryList"));
    ASSERT_NE(list, nullptr) << "没有结果列表";
    const auto hasText = [list](const QString &needle)
    {
        for (int row = 0; row < list->count(); ++row)
        {
            if (list->item(row)->text().contains(needle))
            {
                return true;
            }
        }
        return false;
    };

    ASSERT_EQ(view.rowCount(), 3) << "折叠时应是另一组一张、重复组头与组内最新一条";
    EXPECT_TRUE(hasText(QStringLiteral("张重复/相似"))) << "折叠时没有组头";
    EXPECT_FALSE(hasText(QStringLiteral("旧的一版"))) << "折叠时露出了组内旧的一条";

    auto *merge = view.findChild<QCheckBox *>(QStringLiteral("memoryMergeToggle"));
    ASSERT_NE(merge, nullptr) << "没有合并重复图开关";
    EXPECT_TRUE(merge->isChecked()) << "合并重复图默认应开启";

    merge->setChecked(false);
    EXPECT_EQ(view.rowCount(), 3) << "关闭合并后应列出全部三条卡片";
    EXPECT_FALSE(hasText(QStringLiteral("张重复/相似"))) << "关闭合并后还留着组头";
    EXPECT_TRUE(hasText(QStringLiteral("旧的一版"))) << "关闭合并后旧的一条没有列出";

    merge->setChecked(true);
    EXPECT_EQ(view.rowCount(), 3) << "重新开启后应再次折成三行";
    EXPECT_TRUE(hasText(QStringLiteral("张重复/相似"))) << "重新合并后没有组头";
    EXPECT_FALSE(hasText(QStringLiteral("旧的一版"))) << "重新合并后旧的一条还露着";
}

/**
 * @brief 钉住记忆库页每一枚动作按钮各有一枚不同的图标
 * @details 重试待处理与重新识别曾共用 OCR 图标、清空与移除曾共用垃圾桶：同一排里两枚长得一样，
 *          用户只能逐字读标签。按画出来的位图逐枚比对，补图或换图时不会再有重复悄悄溜进来。
 */
TEST_F(MemoryViewFixture, ActionButtonsKeepDistinctIcons)
{
    const auto view = makeView();

    const auto iconBits = [](const QIcon &icon)
    {
        const QImage image = icon.pixmap(QSize{20, 20}).toImage().convertToFormat(QImage::Format_ARGB32);
        return QByteArray{reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()};
    };
    const std::array<const char *, 11> buttonNames{
            "memoryEnable", "memoryRebuild", "memoryImport", "memoryPin",          "memoryOpenFile", "memoryOpenFolder",
            "memoryRetry",  "memoryRemove",  "memoryClear",  "memoryRetryPending", "memoryExclude",
    };
    QSet<QByteArray> seen;
    for (const auto *name: buttonNames)
    {
        auto *button = buttonOf(view, QString::fromLatin1(name));
        ASSERT_NE(button, nullptr) << name;
        ASSERT_FALSE(button->icon().isNull()) << name << " 没有图标";
        const auto bits = iconBits(button->icon());
        EXPECT_FALSE(seen.contains(bits)) << name << " 与前面的按钮用了同一枚图标";
        seen.insert(bits);
    }
}

/**
 * @brief 钉住重复组头用 DemiBold、字号与列表正文同一档
 * @details 组头是强调不是标题：整行 Bold 在中文里会糊成一团，也会显得跟旁边的卡片不是一套字体。
 */
TEST_F(MemoryViewFixture, GroupHeaderUsesDemiBoldAtTheListBodySize)
{
    static_cast<void>(seedDuplicateGroup());
    auto view = makeView();
    view.refresh();

    auto *list = view.findChild<QListWidget *>(QStringLiteral("memoryList"));
    ASSERT_NE(list, nullptr);

    QListWidgetItem *header = nullptr;
    for (int row = 0; row < list->count(); ++row)
    {
        if (list->item(row)->text().contains(QStringLiteral("张重复/相似")))
        {
            header = list->item(row);
            break;
        }
    }
    ASSERT_NE(header, nullptr) << "没有找到重复组头";
    EXPECT_EQ(header->font().weight(), QFont::DemiBold) << "组头不是 DemiBold";
    EXPECT_EQ(header->font().pointSizeF(), list->font().pointSizeF()) << "组头字号与列表正文不是同一档";
}

/**
 * @brief 钉住首行卡片带 OCR 摘要、应用与时间
 * @details 最新的那条是不同的图，排在首行；只有摘要、应用、时间三样都落在文字里，
 *          用户才不用点开就知道这条是不是要找的。
 */
TEST_F(MemoryViewFixture, FirstRowCarriesOcrSnippetAndMetadata)
{
    static_cast<void>(seedDuplicateGroup());
    auto view = makeView();
    view.refresh();

    const QString caption = view.firstCaption();
    EXPECT_NE(caption.indexOf(QStringLiteral("项目周报")), -1) << caption.toStdString();
    EXPECT_NE(caption.indexOf(QStringLiteral("browser")), -1) << caption.toStdString();
    EXPECT_NE(caption.indexOf(QStringLiteral("今天")), -1) << caption.toStdString();
}

/**
 * @brief 钉住搜索防抖与无结果提示
 * @details 键入后先不查库，150ms 后才刷新；空结果要有自己的引导句，不能显示「还没有索引」。
 */
TEST_F(MemoryViewFixture, SearchDebouncesThenShowsNoMatchHint)
{
    static_cast<void>(seedDuplicateGroup());
    auto view = makeView();
    view.refresh();
    ASSERT_EQ(view.rowCount(), 3);

    auto *search = view.findChild<QLineEdit *>(QStringLiteral("memorySearch"));
    ASSERT_NE(search, nullptr) << "没有搜索框";
    EXPECT_EQ(search->placeholderText(), QString::fromUtf8("搜索截图里的文字…"));

    search->setText(QStringLiteral("完全不存在的关键词"));
    EXPECT_EQ(view.rowCount(), 3) << "防抖期内不该立刻查库";

    ASSERT_TRUE(waitUntil([&view] { return view.rowCount() == 0; }, 2'000)) << "防抖结束后没有按关键词刷新";

    auto *hint = view.findChild<QLabel *>(QStringLiteral("memoryEmptyHint"));
    ASSERT_NE(hint, nullptr) << "没有空态提示";
    EXPECT_FALSE(hint->isHidden()) << "无结果时引导句没露出来";
    EXPECT_NE(hint->text().indexOf(QStringLiteral("没有搜到")), -1) << hint->text().toStdString();
}

/**
 * @brief 钉住未开启与数据库错误两条横幅各有自己的按钮
 * @details 未开启时要给「开启记忆」出口，数据库错误时要给「重建索引」出口；
 *          两个按钮都接回信号，不在这里直接改库。
 */
TEST_F(MemoryViewFixture, BannersExposeEnableAndRebuildActions)
{
    auto view = makeView();
    view.setMemoryEnabled(false);

    auto *enable = buttonOf(view, QStringLiteral("memoryEnable"));
    auto *banner = view.findChild<QLabel *>(QStringLiteral("memoryBanner"));
    auto *frame  = view.findChild<QWidget *>(QStringLiteral("memoryBannerFrame"));
    ASSERT_NE(enable, nullptr) << "未开启时没有开启按钮";
    ASSERT_NE(banner, nullptr) << "没有顶部横幅";
    ASSERT_NE(frame, nullptr) << "没有顶部横幅容器";
    EXPECT_FALSE(enable->isHidden());
    EXPECT_FALSE(frame->isHidden());
    EXPECT_NE(banner->text().indexOf(QStringLiteral("开启")), -1) << banner->text().toStdString();

    int enables = 0;
    QObject::connect(&view, &MemoryView::enableRequested, &view, [&enables] { ++enables; });
    enable->click();
    EXPECT_EQ(enables, 1);

    view.setMemoryEnabled(true);
    EXPECT_TRUE(enable->isHidden()) << "已开启时还亮着开启按钮";
    EXPECT_TRUE(frame->isHidden()) << "没有理由时横幅还占着地方";

    view.setDatabaseError(QStringLiteral("数据库文件损坏"));
    auto *rebuild = buttonOf(view, QStringLiteral("memoryRebuild"));
    ASSERT_NE(rebuild, nullptr) << "数据库错误时没有重建按钮";
    EXPECT_FALSE(rebuild->isHidden());
    EXPECT_FALSE(frame->isHidden());
    EXPECT_NE(banner->text().indexOf(QStringLiteral("数据库文件损坏")), -1) << banner->text().toStdString();

    int rebuilds = 0;
    QObject::connect(&view, &MemoryView::rebuildRequested, &view, [&rebuilds] { ++rebuilds; });
    rebuild->click();
    EXPECT_EQ(rebuilds, 1);

    view.setDatabaseError(QString{});
    EXPECT_TRUE(rebuild->isHidden()) << "错误清掉后重建按钮还亮着";
    EXPECT_TRUE(frame->isHidden()) << "错误清掉后错误横幅还占着地方";
}

/**
 * @brief 钉住搜索失败会把数据库错误摆进横幅，恢复后横幅与重建入口一起收起
 * @details 库连不上时只弹气泡，用户找不到「重建索引」；查询成功之后旧错误必须过期，
 *          否则修好的库前面会一直挂着错误横幅与重建按钮。
 */
TEST_F(MemoryViewFixture, SearchFailureShowsDatabaseErrorUntilRecovery)
{
    auto view = makeView();
    m_index->close();
    view.refresh();

    auto *banner  = view.findChild<QLabel *>(QStringLiteral("memoryBanner"));
    auto *frame   = view.findChild<QWidget *>(QStringLiteral("memoryBannerFrame"));
    auto *rebuild = buttonOf(view, QStringLiteral("memoryRebuild"));
    ASSERT_NE(banner, nullptr) << "没有顶部横幅";
    ASSERT_NE(frame, nullptr) << "没有顶部横幅容器";
    ASSERT_NE(rebuild, nullptr) << "数据库错误时没有重建按钮";
    EXPECT_FALSE(frame->isHidden()) << "搜索失败时错误横幅没露出来";
    EXPECT_FALSE(rebuild->isHidden()) << "搜索失败时没给重建入口";
    EXPECT_NE(banner->text().indexOf(QStringLiteral("数据库尚未打开")), -1) << banner->text().toStdString();

    ASSERT_TRUE(m_index->open().has_value()) << "临时索引重开失败，恢复这一半就无从判起";
    view.refresh();
    EXPECT_TRUE(rebuild->isHidden()) << "库恢复后重建按钮还亮着";
    EXPECT_TRUE(frame->isHidden()) << "库恢复后错误横幅还占着地方";
}

/**
 * @brief 钉住「重试待处理」只在后端不可用或有积压时出现，点击发一次重试请求
 * @details 后端恢复后积压仍可能留在库里；只有单条「重新识别」入口时，用户清不完一批待识别行。
 */
TEST_F(MemoryViewFixture, RetryPendingButtonFollowsBackendAndPendingRows)
{
    auto  view         = makeView();
    auto *retryPending = buttonOf(view, QStringLiteral("memoryRetryPending"));
    ASSERT_NE(retryPending, nullptr) << "页脚没有重试待处理按钮";
    EXPECT_EQ(retryPending->text(), QStringLiteral("重试待处理"));
    EXPECT_TRUE(retryPending->isHidden()) << "没有理由时重试入口不该占着页脚";

    int requests = 0;
    QObject::connect(&view, &MemoryView::retryPendingRequested, &view, [&requests] { ++requests; });

    view.setOcrAvailability(false, QStringLiteral("缺少识别引擎"));
    EXPECT_FALSE(retryPending->isHidden()) << "后端不可用时没给重试入口";
    EXPECT_TRUE(retryPending->isEnabled());
    retryPending->click();
    EXPECT_EQ(requests, 1) << "点了重试待处理却没发请求";

    view.setOcrAvailability(true, QString{});
    EXPECT_TRUE(retryPending->isHidden()) << "后端恢复且没有积压时重试入口还亮着";

    const auto               pendingPath = std::filesystem::path(m_temp.path().toStdString()) / "pending-shot.png";
    const Core::MemoryUpsert entry{
            .filePath   = pendingPath,
            .source     = Core::MemorySource::Capture,
            .capturedAt = QDateTime::currentDateTime(),
            .size       = QSize{32, 24},
    };
    const auto identifier = m_index->upsertPending(entry);
    ASSERT_TRUE(identifier.has_value()) << identifier.error().message;

    view.refresh();
    EXPECT_FALSE(retryPending->isHidden()) << "有待识别行时没有重试入口";
    retryPending->click();
    EXPECT_EQ(requests, 2) << "有积压时点重试待处理却没发请求";

    ASSERT_TRUE(m_index->updateOcr(*identifier, QStringLiteral("补上的文本"), QString{}, QStringLiteral("done"), QString{}, false).has_value());
    view.refresh();
    EXPECT_TRUE(retryPending->isHidden()) << "积压清完后重试入口还亮着";
}

/**
 * @brief 钉住识别不可用的原因会出现在横幅上
 * @details 后端不可用时用户要知道「为什么搜不到新图」；这句原因不能只留在日志里。
 */
TEST_F(MemoryViewFixture, OcrReasonIsShownInBanner)
{
    auto view = makeView();
    view.setOcrAvailability(false, QStringLiteral("缺少识别引擎"));

    auto *banner = view.findChild<QLabel *>(QStringLiteral("memoryBanner"));
    ASSERT_NE(banner, nullptr);
    EXPECT_NE(banner->text().indexOf(QStringLiteral("缺少识别引擎")), -1) << banner->text().toStdString();
}

/**
 * @brief 钉住选中结果后五个动作各发各的信号
 * @details 视图不认识贴图与识别，只把路径与主键交出去；没有选中时按钮必须灰着。
 */
TEST_F(MemoryViewFixture, SelectedRowEmitsActionSignals)
{
    const auto seeded = seedDuplicateGroup();
    auto       view   = makeView();
    view.refresh();

    auto *list = view.findChild<QListWidget *>(QStringLiteral("memoryList"));
    ASSERT_NE(list, nullptr) << "没有结果列表";
    ASSERT_GE(list->count(), 2);
    list->setCurrentRow(0);

    auto *pin    = buttonOf(view, QStringLiteral("memoryPin"));
    auto *open   = buttonOf(view, QStringLiteral("memoryOpenFile"));
    auto *folder = buttonOf(view, QStringLiteral("memoryOpenFolder"));
    auto *retry  = buttonOf(view, QStringLiteral("memoryRetry"));
    auto *remove = buttonOf(view, QStringLiteral("memoryRemove"));
    ASSERT_NE(pin, nullptr);
    ASSERT_NE(open, nullptr);
    ASSERT_NE(folder, nullptr);
    ASSERT_NE(retry, nullptr);
    ASSERT_NE(remove, nullptr);
    ASSERT_TRUE(pin->isEnabled()) << "选中结果后贴图仍灰着";

    int                   pins    = 0;
    int                   opens   = 0;
    int                   folders = 0;
    int                   retries = 0;
    int                   removes = 0;
    std::filesystem::path pinnedPath;
    std::filesystem::path openedPath;
    qint64                retriedId = 0;
    qint64                removedId = 0;
    QObject::connect(&view, &MemoryView::pinRequested, &view,
                     [&pins, &pinnedPath](const std::filesystem::path &path)
                     {
                         ++pins;
                         pinnedPath = path;
                     });
    QObject::connect(&view, &MemoryView::openFileRequested, &view,
                     [&opens, &openedPath](const std::filesystem::path &path)
                     {
                         ++opens;
                         openedPath = path;
                     });
    QObject::connect(&view, &MemoryView::openFolderRequested, &view, [&folders](const std::filesystem::path &) { ++folders; });
    QObject::connect(&view, &MemoryView::retryOcrRequested, &view, [&retries, &retriedId](const qint64 id) { ++retries, retriedId = id; });
    QObject::connect(&view, &MemoryView::removeIndexRequested, &view, [&removes, &removedId](const qint64 id) { ++removes, removedId = id; });

    pin->click();
    open->click();
    folder->click();
    retry->click();
    remove->click();

    EXPECT_EQ(pins, 1);
    EXPECT_EQ(pinnedPath, seeded.distinct);
    EXPECT_EQ(opens, 1);
    EXPECT_EQ(openedPath, seeded.distinct);
    EXPECT_EQ(folders, 1);
    EXPECT_EQ(retries, 1);
    EXPECT_EQ(retriedId, seeded.distinctId);
    EXPECT_EQ(removes, 1);
    EXPECT_EQ(removedId, seeded.distinctId);

    list->setCurrentRow(-1);
    EXPECT_FALSE(pin->isEnabled()) << "没有选中时贴图还亮着";
    EXPECT_FALSE(remove->isEnabled()) << "没有选中时移除还亮着";
}

/**
 * @brief 钉住原图缺失的行提示「文件已不在」并禁用贴图与打开
 * @details 原图被用户移走后，贴图与打开都没有落点；目录与移除仍可用。
 */
TEST_F(MemoryViewFixture, MissingFileDisablesPinAndOpen)
{
    const auto missingPath = std::filesystem::path(m_temp.path().toStdString()) / "gone.png";
    ASSERT_FALSE(std::filesystem::exists(missingPath));
    addIndexOnly(missingPath, QStringLiteral("旧周报"), QDateTime::currentDateTime(), QStringLiteral("code"));

    auto view = makeView();
    view.refresh();
    auto *list = view.findChild<QListWidget *>(QStringLiteral("memoryList"));
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 1);
    EXPECT_NE(list->item(0)->text().indexOf(QStringLiteral("文件已不在")), -1) << list->item(0)->text().toStdString();

    list->setCurrentRow(0);
    auto *pin    = buttonOf(view, QStringLiteral("memoryPin"));
    auto *open   = buttonOf(view, QStringLiteral("memoryOpenFile"));
    auto *folder = buttonOf(view, QStringLiteral("memoryOpenFolder"));
    auto *remove = buttonOf(view, QStringLiteral("memoryRemove"));
    ASSERT_NE(pin, nullptr);
    ASSERT_NE(open, nullptr);
    ASSERT_NE(folder, nullptr);
    ASSERT_NE(remove, nullptr);
    EXPECT_FALSE(pin->isEnabled()) << "文件不在还能贴图";
    EXPECT_FALSE(open->isEnabled()) << "文件不在还能打开";
    EXPECT_TRUE(folder->isEnabled()) << "打开所在目录不依赖原图仍在，不该一起灰掉";
    EXPECT_TRUE(remove->isEnabled());
}

/**
 * @brief 钉住应用与时间筛选会重查并收窄结果
 * @details 应用下拉的空进程名写成「未知应用」并用保留取值筛选；时间档从当天零点往前数。
 */
TEST_F(MemoryViewFixture, AppAndTimeFiltersNarrowResults)
{
    const QDateTime now   = QDateTime::currentDateTime();
    const auto      today = writeImage("today.png", patternImage(false));
    const auto      old   = writeImage("old.png", patternImage(true));
    addResult(today, patternImage(false), QStringLiteral("今天的笔记"), now, QStringLiteral("code"), QStringLiteral("笔记"));
    addResult(old, patternImage(true), QStringLiteral("旧周报"), now.addDays(-40), QString{}, QString{});

    auto view = makeView();
    view.refresh();
    ASSERT_EQ(view.rowCount(), 2);

    auto *appFilter  = view.findChild<QComboBox *>(QStringLiteral("memoryAppFilter"));
    auto *timeFilter = view.findChild<QComboBox *>(QStringLiteral("memoryTimeFilter"));
    ASSERT_NE(appFilter, nullptr);
    ASSERT_NE(timeFilter, nullptr);

    const int unknownIndex = appFilter->findData(unknownProcessFilter());
    ASSERT_GE(unknownIndex, 0) << "空进程名没有「未知应用」档";
    EXPECT_EQ(appFilter->itemText(unknownIndex), QString::fromUtf8("未知应用"));
    appFilter->setCurrentIndex(unknownIndex);
    EXPECT_EQ(view.rowCount(), 1) << "未知应用筛选没生效";

    const int codeIndex = appFilter->findData(QStringLiteral("code"));
    ASSERT_GE(codeIndex, 0);
    appFilter->setCurrentIndex(codeIndex);
    EXPECT_EQ(view.rowCount(), 1) << "应用筛选没生效";

    appFilter->setCurrentIndex(0);
    ASSERT_EQ(view.rowCount(), 2);

    const int weekIndex = timeFilter->findData(7);
    ASSERT_GE(weekIndex, 0);
    timeFilter->setCurrentIndex(weekIndex);
    EXPECT_EQ(view.rowCount(), 1) << "7 天筛选没把 40 天前的记录挡掉";

    timeFilter->setCurrentIndex(0);
    EXPECT_EQ(view.rowCount(), 2) << "时间档回到全部后记录没回来";
}

/**
 * @brief 钉住空库的引导句说明第一次截图后会自动建索引
 * @details 空库与筛不出结果不是同一件事：一个要教用户怎么开始，一个要教用户怎么放宽条件。
 */
TEST_F(MemoryViewFixture, EmptyIndexExplainsFirstScreenshot)
{
    auto view = makeView();
    view.refresh();

    auto *hint = view.findChild<QLabel *>(QStringLiteral("memoryEmptyHint"));
    ASSERT_NE(hint, nullptr);
    EXPECT_EQ(view.rowCount(), 0);
    EXPECT_FALSE(hint->isHidden());
    EXPECT_NE(hint->text().indexOf(QStringLiteral("还没有索引")), -1) << hint->text().toStdString();
    EXPECT_NE(hint->text().indexOf(QStringLiteral("导入旧截图")), -1) << hint->text().toStdString();
}

/**
 * @brief 钉住结果顶到 200 条上限时页脚说明只显示了一页
 * @details 搜索固定只回一页；不说明这一点，用户会以为更早的截图没进索引。
 */
TEST_F(MemoryViewFixture, ShowsSearchLimitHintAtTwoHundredResults)
{
    const QDateTime now = QDateTime::currentDateTime();
    for (int index = 0; index < Core::kMemorySearchLimit; ++index)
    {
        const auto path = std::filesystem::path(m_temp.path().toStdString()) / ("bulk-" + std::to_string(index) + ".png");
        addIndexOnly(path, QStringLiteral("批量条目 %1").arg(index), now.addSecs(-index), QString{});
    }

    auto view = makeView();
    view.refresh();
    EXPECT_EQ(view.rowCount(), Core::kMemorySearchLimit) << "空 dHash 的条目不该被合并";

    auto *stats = view.findChild<QLabel *>(QStringLiteral("memoryStats"));
    ASSERT_NE(stats, nullptr);
    EXPECT_NE(stats->text().indexOf(QStringLiteral("只显示前 200 条")), -1) << stats->text().toStdString();
}

/**
 * @brief 钉住识别文本为空的卡片明说「没有识别到文字」
 * @details 空摘要不能留一截空白：那会被读成加载中或界面坏了。
 */
TEST_F(MemoryViewFixture, EmptyOcrTextShowsPlaceholder)
{
    const auto image = patternImage(false);
    const auto path  = writeImage("empty-ocr.png", image);
    addResult(path, image, QString{}, QDateTime::currentDateTime(), QStringLiteral("code"), QString{});

    auto view = makeView();
    view.refresh();
    ASSERT_EQ(view.rowCount(), 1);
    EXPECT_NE(view.firstCaption().indexOf(QString::fromUtf8("没有识别到文字")), -1) << view.firstCaption().toStdString();
}

/**
 * @brief 钉住页脚与工具条的三个出口在空索引上不会误弹确认框
 * @details 导入按钮直接发信号；空索引点清空只给一句说明，不弹带条数的确认框。
 */
TEST_F(MemoryViewFixture, EmptyIndexExposesImportAndClearExits)
{
    auto view = makeView();
    view.refresh();

    int     imports = 0;
    int     clears  = 0;
    QString message;
    QObject::connect(&view, &MemoryView::importRequested, &view, [&imports] { ++imports; });
    QObject::connect(&view, &MemoryView::clearIndexRequested, &view, [&clears] { ++clears; });
    QObject::connect(&view, &MemoryView::messageForUser, &view, [&message](const QString &text, GyanisScribe::Gui::ToastKind) { message = text; });

    auto *import  = buttonOf(view, QStringLiteral("memoryImport"));
    auto *clear   = buttonOf(view, QStringLiteral("memoryClear"));
    auto *exclude = buttonOf(view, QStringLiteral("memoryExclude"));
    ASSERT_NE(import, nullptr);
    ASSERT_NE(clear, nullptr);
    ASSERT_NE(exclude, nullptr) << "页脚没有排除应用入口";

    import->click();
    EXPECT_EQ(imports, 1) << "导入按钮没有发出请求";

    clear->click();
    EXPECT_EQ(clears, 0) << "空索引还去弹确认框";
    EXPECT_NE(message.indexOf(QStringLiteral("空")), -1) << message.toStdString();
}

/**
 * @brief 钉住重复组头可点击展开与收起
 * @details 组头不可选中；折叠时组头下已露出最新一条，点一下补出组内旧的一条，
 *          再点一下收回旧的一条。
 */
TEST_F(MemoryViewFixture, GroupHeaderTogglesExpansion)
{
    static_cast<void>(seedDuplicateGroup());
    auto view = makeView();
    view.refresh();

    auto *list = view.findChild<QListWidget *>(QStringLiteral("memoryList"));
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 3) << "折叠时应是另一组一张、组头与组内最新一条";
    ASSERT_NE(list->item(1)->text().indexOf(QStringLiteral("张重复/相似")), -1) << list->item(1)->text().toStdString();

    view.show();
    QApplication::processEvents();

    auto       *header     = list->item(1);
    const QRect headerRect = list->visualItemRect(header);
    ASSERT_FALSE(headerRect.isEmpty()) << "组头没有可见矩形，点击展开的前提不成立";
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, headerRect.center());
    QApplication::processEvents();
    EXPECT_EQ(view.rowCount(), 4) << "点组头没有补出组内旧的一条";

    header                    = list->item(1);
    const QRect collapsedRect = list->visualItemRect(header);
    ASSERT_FALSE(collapsedRect.isEmpty());
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, collapsedRect.center());
    QApplication::processEvents();
    EXPECT_EQ(view.rowCount(), 3) << "再点组头没有收回旧的一条";
}

/**
 * @brief 钉住导入进度对话框的读数与取消按钮
 * @details 正文要带扫描、入库、跳过、失败四个读数；待识别条数读得出时补一行；取消只发信号。
 */
TEST_F(MemoryViewFixture, ImportProgressDialogShowsCountsAndCancels)
{
    ImportProgressDialog dialog;
    auto                *text = dialog.findChild<QLabel *>(QStringLiteral("importProgressText"));
    ASSERT_NE(text, nullptr);

    dialog.setProgress(Core::MemoryImportProgress{.scanned = 10, .inserted = 8, .skipped = 2, .failed = 1});
    EXPECT_NE(text->text().indexOf(QStringLiteral("已扫描 10")), -1) << text->text().toStdString();
    EXPECT_NE(text->text().indexOf(QStringLiteral("入库 8")), -1) << text->text().toStdString();
    EXPECT_NE(text->text().indexOf(QStringLiteral("跳过 2")), -1) << text->text().toStdString();
    EXPECT_NE(text->text().indexOf(QStringLiteral("失败 1")), -1) << text->text().toStdString();

    dialog.setPendingOcr(5);
    EXPECT_NE(text->text().indexOf(QStringLiteral("待识别 5")), -1) << text->text().toStdString();

    int cancels = 0;
    QObject::connect(&dialog, &ImportProgressDialog::cancelRequested, &dialog, [&cancels] { ++cancels; });
    auto *cancel = dialog.findChild<QPushButton *>(QStringLiteral("importCancel"));
    ASSERT_NE(cancel, nullptr);
    cancel->click();
    EXPECT_EQ(cancels, 1);
}

/**
 * @brief 钉住记忆库页持有并更新导入进度对话框
 * @details 首次进度时创建并显示；取消按钮接回页面的 importCancelRequested；结束时关闭。
 */
TEST_F(MemoryViewFixture, ViewOwnsImportDialog)
{
    auto view    = makeView();
    int  cancels = 0;
    QObject::connect(&view, &MemoryView::importCancelRequested, &view, [&cancels] { ++cancels; });

    view.setImportProgress(Core::MemoryImportProgress{.scanned = 1, .inserted = 1});
    auto *dialog = view.findChild<ImportProgressDialog *>();
    ASSERT_NE(dialog, nullptr) << "首次进度没有创建对话框";
    EXPECT_FALSE(dialog->isHidden()) << "进度到了对话框却没显示";
    auto *text = dialog->findChild<QLabel *>(QStringLiteral("importProgressText"));
    ASSERT_NE(text, nullptr);
    EXPECT_NE(text->text().indexOf(QStringLiteral("已扫描 1")), -1) << text->text().toStdString();
    auto *cancel = dialog->findChild<QPushButton *>(QStringLiteral("importCancel"));
    ASSERT_NE(cancel, nullptr);
    cancel->click();
    EXPECT_EQ(cancels, 1);

    view.hideImportProgress();
    EXPECT_TRUE(dialog->isHidden()) << "导入结束后对话框还开着";
}

/**
 * @brief 钉住排除应用对话框列出应用与已排除项、保存前按条数确认并返回完整勾选
 * @details 进程名为空的导入条目不可排除；已排除但当前没有条目的进程也要能显示与取消勾选；
 *          新勾选的应用有索引行时，确认框要写明将删除的条数。
 */
TEST_F(MemoryViewFixture, ExcludeDialogReturnsCheckedProcesses)
{
    const std::vector<Core::MemoryAppCount> apps{
            {.processName = QStringLiteral("code"), .count = 3},
            {.processName = QStringLiteral("browser"), .count = 2},
            {.processName = QString{}, .count = 1},
    };
    MemoryExcludeDialog dialog{apps, QStringList{QStringLiteral("code"), QStringLiteral("legacy")}};

    auto *list = dialog.findChild<QListWidget *>(QStringLiteral("excludeList"));
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 3) << "空进程名不该列出来，已排除的 legacy 要补一行";

    QListWidgetItem *codeRow    = nullptr;
    QListWidgetItem *browserRow = nullptr;
    QListWidgetItem *legacyRow  = nullptr;
    for (int row = 0; row < list->count(); ++row)
    {
        auto *item = list->item(row);
        if (item->text().startsWith(QStringLiteral("code")))
        {
            codeRow = item;
        } else if (item->text().startsWith(QStringLiteral("browser")))
        {
            browserRow = item;
        } else if (item->text() == QStringLiteral("legacy"))
        {
            legacyRow = item;
        }
    }
    ASSERT_NE(codeRow, nullptr);
    ASSERT_NE(browserRow, nullptr);
    ASSERT_NE(legacyRow, nullptr);
    EXPECT_EQ(codeRow->checkState(), Qt::Checked) << "已排除的 code 没勾上";
    EXPECT_EQ(browserRow->checkState(), Qt::Unchecked);
    EXPECT_EQ(legacyRow->checkState(), Qt::Checked) << "当前没有条目的已排除项也要勾上";

    // 尚未出现过的进程也能提前排除；重复添加只勾已有行，不补第二条
    auto *input = dialog.findChild<QLineEdit *>(QStringLiteral("excludeProcessInput"));
    auto *add   = dialog.findChild<QPushButton *>(QStringLiteral("excludeAddProcess"));
    ASSERT_NE(input, nullptr);
    ASSERT_NE(add, nullptr);
    input->setText(QStringLiteral("newapp"));
    add->click();
    ASSERT_EQ(list->count(), 4) << "输入的新进程没有加进清单";
    bool newAppFound = false;
    for (int row = 0; row < list->count(); ++row)
    {
        if (list->item(row)->text() == QStringLiteral("newapp"))
        {
            newAppFound = true;
            EXPECT_EQ(list->item(row)->checkState(), Qt::Checked) << "新增的进程默认要勾上";
        }
    }
    EXPECT_TRUE(newAppFound);

    input->setText(QStringLiteral("code"));
    add->click();
    EXPECT_EQ(list->count(), 4) << "重复输入已有进程时补了第二条";

    codeRow->setCheckState(Qt::Unchecked);
    browserRow->setCheckState(Qt::Checked);
    auto *save = dialog.findChild<QPushButton *>(QStringLiteral("excludeSave"));
    ASSERT_NE(save, nullptr);

    // 保存会删掉新排除的 browser 已有的 2 条：先起一枚短定时器，等确认框成为活动模态窗时点「是」
    QString confirmText;
    int     confirms = 0;
    QTimer  driver;
    driver.setInterval(1);
    QObject::connect(&driver, &QTimer::timeout, &driver,
                     [&]
                     {
                         auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                         if (box == nullptr)
                         {
                             return;
                         }
                         confirmText = box->text();
                         auto *yes   = box->button(QMessageBox::Yes);
                         ASSERT_NE(yes, nullptr);
                         yes->click();
                         ++confirms;
                         driver.stop();
                     });
    driver.start();
    save->click();
    driver.stop();
    EXPECT_EQ(confirms, 1) << "保存时没有弹出写明删除条数的确认框";
    EXPECT_NE(confirmText.indexOf(QStringLiteral("2 条")), -1) << confirmText.toStdString();

    const QStringList selected = dialog.selectedProcesses();
    EXPECT_FALSE(selected.contains(QStringLiteral("code"))) << "取消勾选的 code 还在名单里";
    EXPECT_TRUE(selected.contains(QStringLiteral("browser")));
    EXPECT_TRUE(selected.contains(QStringLiteral("legacy")));
    EXPECT_TRUE(selected.contains(QStringLiteral("newapp"))) << "输入的新进程没有进最终名单";
}

/**
 * @brief 钉住排除保存的删除条数只数新勾选的应用
 * @details 已排除的应用在当初保存时已经删过行，重复计入会把确认框的数字说大；
 *          没在索引里出现过的进程没有行可删，也不计入。
 */
TEST_F(MemoryViewFixture, ExcludeDeletionCountCoversNewlyCheckedApps)
{
    const std::vector<Core::MemoryAppCount> apps{
            {.processName = QStringLiteral("code"), .count = 3},
            {.processName = QStringLiteral("browser"), .count = 2},
            {.processName = QString{}, .count = 1},
    };

    EXPECT_EQ(MemoryExcludeDialog::pendingDeletionCount(apps, QStringList{QStringLiteral("legacy")}, QStringList{QStringLiteral("code"), QStringLiteral("browser")}), 5)
            << "新勾选的 code 与 browser 应合计 5 条";
    EXPECT_EQ(MemoryExcludeDialog::pendingDeletionCount(apps, QStringList{QStringLiteral("code")}, QStringList{QStringLiteral("CODE"), QStringLiteral("newapp")}), 0)
            << "大小写不敏感地认出已排除的 code，没出现过的 newapp 没有行可删";
    EXPECT_EQ(MemoryExcludeDialog::pendingDeletionCount(apps, QStringList{}, QStringList{QStringLiteral("browser")}), 2) << "空名单时只数 browser 的 2 条";
}

/**
 * @brief 钉住排除名单进入页脚统计
 * @details 统计里的排除应用数由页面自己带来的名单决定，不从数据库另取。
 */
TEST_F(MemoryViewFixture, ExcludedProcessCountReachesStats)
{
    auto view = makeView();
    view.refresh();

    view.setExcludedProcesses(QStringList{QStringLiteral("code"), QStringLiteral("browser")});
    auto *stats = view.findChild<QLabel *>(QStringLiteral("memoryStats"));
    ASSERT_NE(stats, nullptr);
    EXPECT_NE(stats->text().indexOf(QStringLiteral("排除应用 2")), -1) << stats->text().toStdString();
}
