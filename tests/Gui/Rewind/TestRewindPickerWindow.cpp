#include "Gui/Rewind/RewindPickerWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QImage>
#include <QListWidget>
#include <QRect>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <gtest/gtest.h>

#include <memory>
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
        view.thumbnail.fill(Qt::red);
        view.capturedAt  = QDateTime::currentDateTime();
        view.isCandidate = candidate;
        return view;
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
}
