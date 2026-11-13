#include "Gui/Rewind/RewindPickerWindow.h"

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QObject>
#include <QPixmap>
#include <QPoint>
#include <QPushButton>
#include <QRect>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSize>
#include <QString>
#include <QTest>
#include <QWheelEvent>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

using GyanisScribe::Core::RewindFrameView;
using GyanisScribe::Gui::RewindPickerWindow;

namespace
{
    std::unique_ptr<QApplication> application;

    class RewindPickerFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，装置函数返回之后还会读它们
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestRewindPickerWindow";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
        }

        static void TearDownTestSuite()
        {
            application.reset();
        }
    };

    /// 造一条只带选择器所需字段的样本视图
    RewindFrameView makeView(const quint64 id, const bool candidate)
    {
        RewindFrameView view;
        view.id        = id;
        view.thumbnail = QImage(32, 18, QImage::Format_ARGB32_Premultiplied);
        // 纯红底：候选圆点用绿色，扫到绿色像素就说明圆点确实画进了缩略图
        view.thumbnail.fill(Qt::red);
        view.capturedAt  = QDateTime::currentDateTime();
        view.isCandidate = candidate;
        return view;
    }

    /// 在胶片条里找有没有候选圆点那抹绿色
    bool hasCandidateDot(const QIcon &icon)
    {
        const QImage image = icon.pixmap(QSize{160, 90}).toImage();
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.green() > 150 && pixel.red() < 150)
                {
                    return true;
                }
            }
        }
        return false;
    }

    /// 按「按下、松开、双击」投一组鼠标事件；列表视图要先见过同一格的按下才算双击
    void doubleClickItem(QListWidget *list, const QPoint &point)
    {
        const QPointF local{point};
        const QPointF global{list->viewport()->mapToGlobal(point)};
        QMouseEvent   press{QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(list->viewport(), &press);
        QMouseEvent release{QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
        QApplication::sendEvent(list->viewport(), &release);
        QMouseEvent doubleClick{QEvent::MouseButtonDblClick, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QApplication::sendEvent(list->viewport(), &doubleClick);
    }

    /// 造一批编号递增、都不带候选标记的样本
    std::vector<RewindFrameView> makeFrames(const quint64 count)
    {
        std::vector<RewindFrameView> frames;
        frames.reserve(static_cast<std::size_t>(count));
        for (quint64 id = 1; id <= count; ++id)
        {
            frames.push_back(makeView(id, false));
        }
        return frames;
    }

    /// 造一扇选择器；用例经指针读测试口
    std::unique_ptr<RewindPickerWindow> makeWindow()
    {
        return std::make_unique<RewindPickerWindow>();
    }

    /// 造 N 条样本：样本号从 1 起，抓帧时刻每秒一格
    std::vector<RewindFrameView> makeViews(const quint64 count)
    {
        const QDateTime              start = QDateTime::currentDateTime();
        std::vector<RewindFrameView> views;
        views.reserve(static_cast<std::size_t>(count));
        for (quint64 index = 0; index < count; ++index)
        {
            RewindFrameView view = makeView(index + 1, false);
            view.capturedAt      = start.addMSecs(static_cast<qint64>(index) * 1000);
            views.push_back(view);
        }
        return views;
    }

    /// 把按键直接投给选择器，用例不依赖窗口拿到焦点
    void pressKey(QWidget &window, const Qt::Key key)
    {
        QTest::keyClick(&window, key);
    }
} // namespace

/**
 * @brief 钉住胶片条逐条列出样本，并把光标落在最后一个候选帧上
 * @details 预选候选帧是「回哪一刻」的默认答案：用户打开选择器后直接回车，选中的应当是最近一次变化，
 *          而不是最末尾那条大概率与更早一帧画面相同的样本。
 */
TEST_F(RewindPickerFixture, ShowsFramesAndPreselectsLastCandidate)
{
    RewindPickerWindow                 window;
    const std::vector<RewindFrameView> frames{makeView(1, false), makeView(2, true), makeView(3, false)};
    window.showFrames(frames, QRect(0, 0, 800, 600), QStringLiteral("FakeScreen-1"), false, {});

    auto *list = window.findChild<QListWidget *>(QStringLiteral("rewindFilmstrip"));
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->count(), 3);
    ASSERT_NE(list->currentItem(), nullptr);
    EXPECT_EQ(list->currentItem()->data(Qt::UserRole).toULongLong(), 2U);
}

/**
 * @brief 钉住候选帧的可判标记：数据角色、提示与缩略图上的小圆点缺一不可
 * @details 只有提示的话，用户扫一眼胶片条看不出哪几帧是变化点；角色又是后续任务与测试读取候选标记的接口，
 *          圆点只是它的可见投影，三者一起才构成 spec 要求的候选标记。
 */
TEST_F(RewindPickerFixture, CandidateFramesCarryMarkerAndTooltip)
{
    RewindPickerWindow                 window;
    const std::vector<RewindFrameView> frames{makeView(1, false), makeView(2, true)};
    window.showFrames(frames, QRect(0, 0, 800, 600), QStringLiteral("FakeScreen-1"), false, {});

    auto *list = window.findChild<QListWidget *>(QStringLiteral("rewindFilmstrip"));
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 2);
    auto *plain     = list->item(0);
    auto *candidate = list->item(1);
    ASSERT_NE(plain, nullptr);
    ASSERT_NE(candidate, nullptr);

    EXPECT_FALSE(plain->data(RewindPickerWindow::kCandidateRole).toBool()) << "普通帧被标成了候选帧";
    EXPECT_TRUE(candidate->data(RewindPickerWindow::kCandidateRole).toBool()) << "候选帧在数据角色里没有标记";
    EXPECT_TRUE(plain->toolTip().isEmpty()) << "普通帧不该带候选提示";
    EXPECT_TRUE(candidate->toolTip().contains(QStringLiteral("候选帧"))) << "候选帧的提示丢了";
    EXPECT_FALSE(hasCandidateDot(plain->icon())) << "普通帧缩略图上画了圆点";
    EXPECT_TRUE(hasCandidateDot(candidate->icon())) << "候选帧缩略图上没有可见的小圆点";
}

/**
 * @brief 钉住回车发出当前样本号并收起选择器
 * @details 协调器只拿样本号去解码，选择器自己不能替它做任何抓帧决定。
 */
TEST_F(RewindPickerFixture, EnterEmitsChosenAndHides)
{
    RewindPickerWindow                 window;
    const std::vector<RewindFrameView> frames{makeView(1, false), makeView(2, true)};
    window.showFrames(frames, QRect(0, 0, 800, 600), QStringLiteral("FakeScreen-1"), false, {});

    QSignalSpy chosen(&window, &RewindPickerWindow::frameChosen);

    QTest::keyClick(&window, Qt::Key_Return);
    ASSERT_EQ(chosen.count(), 1);
    EXPECT_EQ(chosen.first().first().toULongLong(), 2U) << "回车确认的该是预选的候选帧";
    EXPECT_FALSE(window.isVisible()) << "确认后选择器应自己收起";
}

/**
 * @brief 钉住 Esc 在用户看得见的选择器上发出取消并收起
 * @details Esc 只在可见窗口上才是真实的取消路径：本用例不接在回车之后复用同一扇已隐藏的窗口，
 *          而是新建窗口、展示样本之后再按键，断言才能代表协调器实际收到的信号。
 */
TEST_F(RewindPickerFixture, EscapeEmitsDismissedAndHides)
{
    RewindPickerWindow                 window;
    const std::vector<RewindFrameView> frames{makeView(1, false), makeView(2, true)};
    window.showFrames(frames, QRect(0, 0, 800, 600), QStringLiteral("FakeScreen-1"), false, {});

    QSignalSpy dismissed(&window, &RewindPickerWindow::dismissed);

    QTest::keyClick(&window, Qt::Key_Escape);
    ASSERT_EQ(dismissed.count(), 1);
    EXPECT_FALSE(window.isVisible()) << "取消后选择器应自己收起";

    window.close();
    EXPECT_EQ(dismissed.count(), 1) << "Esc 之后再收到关闭事件不该重复取消";
}

/**
 * @brief 钉住左右键逐步移动、Home/End 跳到首尾，并在两端停住
 * @details 没有候选帧时默认选最新一帧；从末帧继续往右、从首帧继续往左都不该绕回另一端。
 */
TEST_F(RewindPickerFixture, LeftRightHomeEndMoveSelection)
{
    RewindPickerWindow                 window;
    const std::vector<RewindFrameView> frames = makeFrames(4);
    window.showFrames(frames, QRect(0, 0, 800, 600), QStringLiteral("FakeScreen-1"), false, {});

    auto *list = window.findChild<QListWidget *>(QStringLiteral("rewindFilmstrip"));
    ASSERT_NE(list, nullptr);
    ASSERT_NE(list->currentItem(), nullptr);
    const auto currentId = [list] { return list->currentItem()->data(Qt::UserRole).toULongLong(); };

    EXPECT_EQ(currentId(), 4U) << "没有候选帧时该默认最新一帧";
    QTest::keyClick(&window, Qt::Key_Left);
    EXPECT_EQ(currentId(), 3U);
    QTest::keyClick(&window, Qt::Key_Right);
    EXPECT_EQ(currentId(), 4U);
    QTest::keyClick(&window, Qt::Key_Right);
    EXPECT_EQ(currentId(), 4U) << "末帧再往右应停住";
    QTest::keyClick(&window, Qt::Key_Home);
    EXPECT_EQ(currentId(), 1U);
    QTest::keyClick(&window, Qt::Key_Left);
    EXPECT_EQ(currentId(), 1U) << "首帧再往左应停住";
    QTest::keyClick(&window, Qt::Key_Right);
    EXPECT_EQ(currentId(), 2U);
    QTest::keyClick(&window, Qt::Key_End);
    EXPECT_EQ(currentId(), 4U);
}

/**
 * @brief 钉住单击选中、双击确认
 * @details 双击走的是与回车同一条确认路径：只发样本号并收起，不抓帧。
 */
TEST_F(RewindPickerFixture, DoubleClickEmitsChosen)
{
    RewindPickerWindow                 window;
    const std::vector<RewindFrameView> frames{makeView(1, false), makeView(2, true), makeView(3, false)};
    window.showFrames(frames, QRect(0, 0, 1920, 1080), QStringLiteral("FakeScreen-1"), false, {});
    QApplication::processEvents();

    auto *list = window.findChild<QListWidget *>(QStringLiteral("rewindFilmstrip"));
    ASSERT_NE(list, nullptr);
    const QRect itemRect = list->visualItemRect(list->item(0));
    ASSERT_FALSE(itemRect.isEmpty()) << "首格没排进视口，双击判据够不着";

    QSignalSpy chosen(&window, &RewindPickerWindow::frameChosen);

    doubleClickItem(list, itemRect.center());
    ASSERT_EQ(chosen.count(), 1);
    EXPECT_EQ(chosen.first().first().toULongLong(), 1U) << "双击确认的该是双击的那一帧";
    EXPECT_FALSE(window.isVisible()) << "双击确认后选择器应自己收起";
}

/**
 * @brief 钉住暂停徽标把原因写出来
 * @details 只写「已暂停」的话，用户分不清是排除程序、锁屏还是别的原因，只能去别处翻状态。
 */
TEST_F(RewindPickerFixture, PauseBadgeShowsReason)
{
    RewindPickerWindow                 window;
    const std::vector<RewindFrameView> frames{makeView(1, true)};
    window.showFrames(frames, QRect(0, 0, 800, 600), QStringLiteral("FakeScreen-1"), true, QStringLiteral("命中了排除名单"));

    auto *status = window.findChild<QLabel *>(QStringLiteral("rewindStatus"));
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->text().contains(QStringLiteral("已暂停"))) << "暂停徽标没说暂停";
    EXPECT_TRUE(status->text().contains(QStringLiteral("命中了排除名单"))) << "暂停徽标里没有原因";
}

/**
 * @brief 钉住无帧时不弹空选择器，显示中拿到空表也要先收起
 * @details 空胶片条没有可选内容，留着窗口只会让用户对着一片空白按 Esc。
 */
TEST_F(RewindPickerFixture, EmptyFramesLeaveWindowHidden)
{
    RewindPickerWindow window;
    window.showFrames({}, QRect(0, 0, 800, 600), QStringLiteral("FakeScreen-1"), false, {});
    EXPECT_FALSE(window.isVisible()) << "没有帧时不该弹空选择器";

    const std::vector<RewindFrameView> frames{makeView(1, true)};
    window.showFrames(frames, QRect(0, 0, 800, 600), QStringLiteral("FakeScreen-1"), false, {});
    EXPECT_TRUE(window.isVisible());

    window.showFrames({}, QRect(0, 0, 800, 600), QStringLiteral("FakeScreen-1"), false, {});
    EXPECT_FALSE(window.isVisible()) << "显示中收到空表也要先收起";
}

/**
 * @brief 钉住窗口被关闭时按取消处理，且只发一次 dismissed
 * @details Alt+F4 或无边框窗被系统关闭走的是 closeEvent，不经过 Esc；协调器若收不到 dismissed 会停在 PickerOpen。
 *          重复 close 不该把同一次取消发两遍。
 */
TEST_F(RewindPickerFixture, CloseEmitsDismissedOnce)
{
    RewindPickerWindow                 window;
    const std::vector<RewindFrameView> frames{makeView(1, true)};
    window.showFrames(frames, QRect(0, 0, 800, 600), QStringLiteral("FakeScreen-1"), false, {});

    QSignalSpy dismissed(&window, &RewindPickerWindow::dismissed);

    EXPECT_TRUE(window.close());
    EXPECT_EQ(dismissed.count(), 1);
    EXPECT_FALSE(window.isVisible()) << "关闭后选择器应自己收起";

    window.close();
    EXPECT_EQ(dismissed.count(), 1) << "同一次关闭只该算一次取消";
}

/**
 * @brief 钉住「取消」按钮与 Esc 走同一条取消路径
 * @details 鼠标用户不一定知道 Esc，窗口上必须有一枚看得见的取消按钮。
 */
TEST_F(RewindPickerFixture, CancelButtonEmitsDismissed)
{
    RewindPickerWindow                 window;
    const std::vector<RewindFrameView> frames{makeView(1, true)};
    window.showFrames(frames, QRect(0, 0, 800, 600), QStringLiteral("FakeScreen-1"), false, {});

    auto *cancel = window.findChild<QPushButton *>(QStringLiteral("rewindCancel"));
    ASSERT_NE(cancel, nullptr) << "选择器上应有可见的取消按钮";
    EXPECT_TRUE(cancel->isVisible());

    QSignalSpy dismissed(&window, &RewindPickerWindow::dismissed);

    cancel->click();
    EXPECT_EQ(dismissed.count(), 1);
    EXPECT_FALSE(window.isVisible()) << "点取消后选择器应自己收起";
}

/**
 * @brief 钉住胶片条按 8–12 格排布，且不出现竖向滚动条
 * @details spec 要求一屏可见约 8–12 格；只按提示文字宽度定窗会退化成两三格。
 *          12 格在 1920 宽的屏上应当溢出并可横向滚动，竖向滚动条必须关掉，
 *          否则滚轮会被它吃掉，横向滚动也就无从谈起。
 */
TEST_F(RewindPickerFixture, SizesFilmstripForEightToTwelveCells)
{
    const QRect        screen{0, 0, 1920, 1080};
    RewindPickerWindow window;
    window.showFrames(makeFrames(12), screen, QStringLiteral("FakeScreen-1"), false, {});
    QApplication::processEvents();

    auto *list = window.findChild<QListWidget *>(QStringLiteral("rewindFilmstrip"));
    ASSERT_NE(list, nullptr);
    const QSize grid = list->gridSize();
    ASSERT_FALSE(grid.isEmpty()) << "胶片条没有固定格宽，可见格数不可判";
    EXPECT_GE(grid.width(), 160) << "格宽没有按缩略图最大边来";

    const int visibleCells = list->viewport()->width() / grid.width();
    EXPECT_GE(visibleCells, 8) << "一屏可见格数少于 8";
    EXPECT_LE(visibleCells, 12) << "一屏可见格数多于 12";

    EXPECT_EQ(list->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff) << "竖向滚动条会吃掉滚轮";
    EXPECT_GT(list->horizontalScrollBar()->maximum(), 0) << "12 格在 1920 宽的屏上应溢出";
    EXPECT_GE(window.x(), screen.left());
    EXPECT_LE(window.geometry().right(), screen.right());
    EXPECT_GE(window.y(), screen.top());
    EXPECT_LE(window.geometry().bottom(), screen.bottom());
}

/**
 * @brief 钉住滚轮把胶片条横向滚动
 * @details 竖向滚动条已关，滚轮事件必须自己映射到横向滚动条；否则用户只能拖滚动条。
 */
TEST_F(RewindPickerFixture, WheelScrollsFilmstripHorizontally)
{
    RewindPickerWindow window;
    window.showFrames(makeFrames(12), QRect(0, 0, 1920, 1080), QStringLiteral("FakeScreen-1"), false, {});
    QApplication::processEvents();

    auto *list = window.findChild<QListWidget *>(QStringLiteral("rewindFilmstrip"));
    ASSERT_NE(list, nullptr);
    auto *bar = list->horizontalScrollBar();
    ASSERT_GT(bar->maximum(), 0) << "胶片条没有可滚动余量，滚轮判据够不着";
    bar->setValue(0);

    const QPoint local = list->viewport()->rect().center();
    QWheelEvent  wheel{QPointF{local}, QPointF{list->viewport()->mapToGlobal(local)}, QPoint{}, QPoint{0, -120}, Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false};
    QApplication::sendEvent(list->viewport(), &wheel);

    EXPECT_GT(bar->value(), 0) << "滚轮没有转成横向滚动";
}

/**
 * @brief 钉住底部居中、放不下改顶部居中，左右都夹在屏内
 * @details 选择器贴着抓帧屏底边出现；任务栏或小屏把下方空间吃光时要翻到顶部，不能露出半截。
 */
TEST_F(RewindPickerFixture, PlacesBottomOrFlipsToTop)
{
    const QRect  tallScreen{0, 0, 1920, 1080};
    const QSize  windowSize{800, 200};
    const QPoint bottomPlaced = RewindPickerWindow::positionFor(tallScreen, windowSize);
    EXPECT_EQ(bottomPlaced.x(), (tallScreen.width() - windowSize.width()) / 2);
    EXPECT_EQ(bottomPlaced.y(), tallScreen.height() - windowSize.height() - 24);

    const QRect  shortScreen{0, 0, 1920, 300};
    const QSize  shortWindowSize{800, 260};
    const QPoint topPlaced = RewindPickerWindow::positionFor(shortScreen, shortWindowSize);
    EXPECT_EQ(topPlaced.y(), 24) << "底部放不下时应改顶部居中";
    EXPECT_LE(topPlaced.y() + shortWindowSize.height(), shortScreen.bottom() + 1);

    const QPoint clamped = RewindPickerWindow::positionFor(QRect{100, 100, 800, 600}, QSize{900, 100});
    EXPECT_EQ(clamped.x(), 100) << "比屏还宽的窗口应从左边贴齐，不能露出负坐标";
}

/**
 * @brief 钉住 `[`/`]` 标起终点、顺序自动纠正与读数
 * @details 先标终点再标起点是常见操作，端点顺序在这层就该纠正，不能让读数和导出把区间算反。
 */
TEST_F(RewindPickerFixture, BracketKeysMarkARangeAndFixTheOrder)
{
    auto window = makeWindow();
    window->showFrames(makeViews(5), QRect{0, 0, 1920, 1080}, QStringLiteral("DISPLAY1"), false, QString());

    auto *list = window->findChild<QListWidget *>(QStringLiteral("rewindFilmstrip"));
    ASSERT_NE(list, nullptr);
    list->setCurrentRow(3);
    pressKey(*window, Qt::Key_BracketLeft);
    EXPECT_EQ(window->rangeStartForTesting(), std::optional<quint64>{4}) << "起点没标在光标那一帧";
    EXPECT_FALSE(window->rangeEndForTesting().has_value());

    list->setCurrentRow(1);
    pressKey(*window, Qt::Key_BracketRight);
    ASSERT_TRUE(window->rangeStartForTesting().has_value());
    ASSERT_TRUE(window->rangeEndForTesting().has_value());
    EXPECT_EQ(*window->rangeStartForTesting(), quint64{2}) << "终点在起点之前时该自动交换";
    EXPECT_EQ(*window->rangeEndForTesting(), quint64{4});

    auto *range = window->findChild<QLabel *>(QStringLiteral("rewindRange"));
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->text().contains(QStringLiteral("3"))) << "读数没报帧数：" << range->text().toStdString();
}

/**
 * @brief 钉住范围高亮：区间内带角色、区间外不带
 * @details 区间内外落成数据角色，涂色只是它的可见投影；端点本身也算在区间内。
 */
TEST_F(RewindPickerFixture, MarksTheRangeItems)
{
    auto window = makeWindow();
    window->showFrames(makeViews(5), QRect{0, 0, 1920, 1080}, QStringLiteral("DISPLAY1"), false, QString());
    auto *list = window->findChild<QListWidget *>(QStringLiteral("rewindFilmstrip"));
    ASSERT_NE(list, nullptr);

    list->setCurrentRow(1);
    pressKey(*window, Qt::Key_BracketLeft);
    list->setCurrentRow(3);
    pressKey(*window, Qt::Key_BracketRight);

    for (int row = 0; row < list->count(); ++row)
    {
        const bool inside = list->item(row)->data(RewindPickerWindow::kRangeRole).toBool();
        EXPECT_EQ(inside, row >= 1 && row <= 3) << "第 " << row << " 行的范围标记不对";
    }
}

/**
 * @brief 钉住导出按钮：标记不完整时禁用，完整时启用并按纠正后的端点发信号
 * @details 同一帧标两次也算标记完整，至少两帧由协调器再判。
 */
TEST_F(RewindPickerFixture, ExportButtonStaysDisabledUntilTwoFramesAreMarked)
{
    auto window = makeWindow();
    window->showFrames(makeViews(5), QRect{0, 0, 1920, 1080}, QStringLiteral("DISPLAY1"), false, QString());
    auto *list         = window->findChild<QListWidget *>(QStringLiteral("rewindFilmstrip"));
    auto *exportButton = window->findChild<QPushButton *>(QStringLiteral("rewindExportClip"));
    ASSERT_NE(list, nullptr);
    ASSERT_NE(exportButton, nullptr);
    EXPECT_FALSE(exportButton->isEnabled()) << "还没标范围时不该能导出";

    list->setCurrentRow(2);
    pressKey(*window, Qt::Key_BracketLeft);
    EXPECT_FALSE(exportButton->isEnabled()) << "只有起点时不该能导出";

    pressKey(*window, Qt::Key_BracketRight);
    EXPECT_TRUE(exportButton->isEnabled());

    quint64 from = 0;
    quint64 to   = 0;
    QObject::connect(window.get(), &RewindPickerWindow::clipRequested,
                     [&](const quint64 left, const quint64 right)
                     {
                         from = left;
                         to   = right;
                     });
    exportButton->click();
    EXPECT_EQ(from, quint64{3});
    EXPECT_EQ(to, quint64{3}) << "同一帧标两次：端点相等但按钮该允许（协调器再判至少两帧）";
}

/**
 * @brief 钉住导出中的按钮态：文案换成「正在导出…」且不可再点，结束后按标记恢复
 * @details 导出在后台跑时再点一次最多只换来一句「正在导出」的提示；按钮自己先拦住这一下。
 *          同时 `[`/`]` 仍要能改标记，下一次导出按新区间走，所以刷新范围不能把按钮重新点亮。
 */
TEST_F(RewindPickerFixture, ExportingSwapsButtonTextAndRestoresItAfterCompletion)
{
    auto window = makeWindow();
    window->showFrames(makeViews(5), QRect{0, 0, 1920, 1080}, QStringLiteral("DISPLAY1"), false, QString());
    auto *list         = window->findChild<QListWidget *>(QStringLiteral("rewindFilmstrip"));
    auto *exportButton = window->findChild<QPushButton *>(QStringLiteral("rewindExportClip"));
    ASSERT_NE(list, nullptr);
    ASSERT_NE(exportButton, nullptr);
    ASSERT_FALSE(exportButton->isEnabled());
    const QString idleText = exportButton->text();

    window->setExporting(true);
    EXPECT_TRUE(window->isExportingForTesting());
    EXPECT_TRUE(exportButton->text().contains(QStringLiteral("正在导出"))) << exportButton->text().toStdString();
    EXPECT_FALSE(exportButton->isEnabled()) << "导出中按钮还能再点";

    // 导出中仍可改标记：范围刷新不该改掉导出中文案，也不该把按钮点亮
    list->setCurrentRow(1);
    pressKey(*window, Qt::Key_BracketLeft);
    list->setCurrentRow(3);
    pressKey(*window, Qt::Key_BracketRight);
    ASSERT_EQ(window->rangeStartForTesting(), std::optional<quint64>{2});
    ASSERT_EQ(window->rangeEndForTesting(), std::optional<quint64>{4});
    EXPECT_FALSE(exportButton->isEnabled()) << "标记完整后按钮在导出中又被点亮";
    EXPECT_TRUE(exportButton->text().contains(QStringLiteral("正在导出")));

    window->setExporting(false);
    EXPECT_FALSE(window->isExportingForTesting());
    EXPECT_EQ(exportButton->text(), idleText);
    EXPECT_TRUE(exportButton->isEnabled()) << "导出结束后，完整区间该让按钮恢复可点";
}
