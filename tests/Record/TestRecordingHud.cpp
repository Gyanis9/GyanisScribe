#include "Record/RecordingHud.h"

#include "Record/RecordingSession.h"
#include "Gui/Resources/IconProvider.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QPoint>
#include <QRect>
#include <QString>

#include <memory>
#include <optional>

using GyanisScribe::Record::countdownTextFor;
using GyanisScribe::Record::formatDuration;
using GyanisScribe::Record::hudStatusText;
using GyanisScribe::Record::kHudEdgeGap;
using GyanisScribe::Record::RecordingHud;
using GyanisScribe::Record::RecordingState;
using GyanisScribe::Record::snapHudRect;
using GyanisScribe::Gui::IconProvider;

namespace
{
    std::unique_ptr<QApplication> application;
    std::unique_ptr<IconProvider> icons;

    class RecordingHudFixture : public ::testing::Test
    {
    protected:
        static void SetUpTestSuite()
        {
            // argv 与 argc 的缓冲要有静态存储期：Qt 只存指针不拷贝，
            // 而 QCoreApplication::applicationDirPath() 这类调用是在装置函数返回之后才读它们。
            static int   argumentCount  = 1;
            static char  argumentZero[] = "TestRecordingHud";
            static char *arguments[]    = {argumentZero, nullptr};
            application                 = std::make_unique<QApplication>(argumentCount, arguments);
            icons                       = std::make_unique<IconProvider>();
        }

        static void TearDownTestSuite()
        {
            icons.reset();
            application.reset();
        }

        [[nodiscard]] std::unique_ptr<RecordingHud> makeHud() const
        {
            return std::make_unique<RecordingHud>(*icons, QRect{0, 0, 1920, 1040});
        }
    };

    /**
     * @brief 按无障碍名字找控制条上的一枚按钮
     * @return 找到返回那枚按钮，所有权在控制条；没有则返回空指针
     * @details 认名字不认顺序：控制条加一枚键不该让所有用例都去改下标。
     */
    [[nodiscard]] QPushButton *buttonOf(const RecordingHud &hud, const QString &accessibleName)
    {
        const auto candidates = hud.findChildren<QPushButton *>();
        for (auto *candidate: candidates)
        {
            if (candidate->accessibleName() == accessibleName)
            {
                return candidate;
            }
        }
        return nullptr;
    }

    /// 按下再放开：QPushButton 的 clicked 要两半齐了才发，只发按下事件会让用例以为按钮没接上
    void clickOn(QPushButton *target)
    {
        const QPointF where{5.0, 5.0};
        QMouseEvent   press{QEvent::MouseButtonPress, where, where, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier};
        QMouseEvent   release{QEvent::MouseButtonRelease, where, where, Qt::LeftButton, Qt::NoButton, Qt::NoModifier};
        QApplication::sendEvent(target, &press);
        QApplication::sendEvent(target, &release);
    }

} // namespace

/**
 * @brief 钉住时长的时钟写法
 * @details 期望值逐个手算：控制条上的数字是用户决定「该停了」的唯一依据，
 *          「9 秒」写成「9」会让人和「90 秒」混起来。
 */
TEST(RecordingHudText, DurationReadsAsAClock)
{
    EXPECT_EQ(formatDuration(0), QStringLiteral("00:00"));
    EXPECT_EQ(formatDuration(9), QStringLiteral("00:09"));
    EXPECT_EQ(formatDuration(75), QStringLiteral("01:15"));
    EXPECT_EQ(formatDuration(3599), QStringLiteral("59:59"));
    EXPECT_EQ(formatDuration(3600), QStringLiteral("1:00:00"));
    EXPECT_EQ(formatDuration(3725), QStringLiteral("1:02:05"));
    EXPECT_EQ(formatDuration(-5), QStringLiteral("00:00")) << "负数不该写成 -00:05";
}

/**
 * @brief 钉住倒计时的显示范围
 * @details 0 与负数不给大字：录制开始后还挂着「1」，用户会以为随时会断。
 */
TEST(RecordingHudText, CountdownDigitsOnlyWhileCounting)
{
    ASSERT_TRUE(countdownTextFor(3).has_value());
    EXPECT_EQ(*countdownTextFor(3), QStringLiteral("3"));
    EXPECT_EQ(*countdownTextFor(1), QStringLiteral("1"));
    EXPECT_FALSE(countdownTextFor(0).has_value());
    EXPECT_FALSE(countdownTextFor(-1).has_value());
    EXPECT_EQ(*countdownTextFor(10), QStringLiteral("10"));
}

/**
 * @brief 钉住六种状态各有一句能读懂的文本
 * @details 「已暂停」必须与「未在录制」分得开：暂停还占着屏幕区域，而后者是彻底结束。
 */
TEST(RecordingHudText, StatusSeparatesPausedFromIdle)
{
    EXPECT_EQ(hudStatusText(RecordingState::Idle, 0), QStringLiteral("未在录制"));
    EXPECT_EQ(hudStatusText(RecordingState::Countdown, 0), QStringLiteral("准备开始"));
    EXPECT_EQ(hudStatusText(RecordingState::Recording, 12), QStringLiteral("录制中"));
    EXPECT_EQ(hudStatusText(RecordingState::Paused, 12), QStringLiteral("已暂停（时长不计）"));
    EXPECT_EQ(hudStatusText(RecordingState::Done, 75), QStringLiteral("已录 01:15"));
    EXPECT_EQ(hudStatusText(RecordingState::Failed, 0), QStringLiteral("已中止"));
}

/**
 * @brief 钉住拖动后的夹取与贴边
 * @details 期望值手算（可用区 1920x1040、控制条 260x44、边距 12）：
 *          不夹住的话用户可以把它拖到屏幕外，之后就再也没法停止录制。
 */
TEST(RecordingHudGeometry, DraggedPanelIsClampedThenSnapped)
{
    const QRect available{0, 0, 1920, 1040};
    const QRect panel{800, 500, 260, 44};

    // 屏幕中间：保持原位，贴边只在挨着边时才发生
    EXPECT_EQ(snapHudRect(panel, available), panel);

    // 拖到右边之外：先夹回屏内，再因为挨着右边而贴边
    EXPECT_EQ(snapHudRect(QRect{1800, 500, 260, 44}, available), QRect(1920 - 260 - kHudEdgeGap, 500, 260, 44));

    // 拖到底部之外：夹住并贴到可用区下沿
    EXPECT_EQ(snapHudRect(QRect{800, 1100, 260, 44}, available), QRect(800, 1040 - 44 - kHudEdgeGap, 260, 44));

    // 左上角：两条边都贴
    EXPECT_EQ(snapHudRect(QRect{-200, -200, 260, 44}, available), QRect(kHudEdgeGap, kHudEdgeGap, 260, 44));

    // 可用区不成立时不挪位置：给个 (0,0,0,0) 再把面板搬过去会更糟
    EXPECT_EQ(snapHudRect(panel, QRect{}), panel);
}

/**
 * @brief 钉住状态推送真的落到了文本上，且倒计时大字随状态出现与消失
 * @details 控制条是录制期间唯一的读数；数字没刷比没有控制条更危险。
 */
TEST_F(RecordingHudFixture, StatePushRefreshesReadoutsAndCountdownOverlay)
{
    auto hud = makeHud();

    hud->applyState(RecordingState::Recording, 0, 42);
    EXPECT_EQ(hud->statusText(), QStringLiteral("录制中"));
    EXPECT_EQ(hud->elapsedText(), QStringLiteral("00:42"));
    EXPECT_FALSE(hud->isCountdownVisible()) << "录制中不该还挂着倒计时大字";

    hud->applyState(RecordingState::Countdown, 3, 0);
    EXPECT_EQ(hud->statusText(), QStringLiteral("准备开始"));
    EXPECT_TRUE(hud->isCountdownVisible());
    EXPECT_EQ(hud->countdownText(), QStringLiteral("3"));

    hud->applyState(RecordingState::Paused, 0, 42);
    EXPECT_EQ(hud->statusText(), QStringLiteral("已暂停（时长不计）"));
    EXPECT_EQ(hud->elapsedText(), QStringLiteral("00:42")) << "暂停时时长要停在暂停那一刻，不该继续跳";
    EXPECT_FALSE(hud->isCountdownVisible());
}

/**
 * @brief 钉住暂停键随状态改口，并且按下去发的是对应请求
 * @details 暂停中再按还发「暂停」，用户按第二下时录制不会恢复，而按钮看起来一切正常。
 */
TEST_F(RecordingHudFixture, PauseButtonFollowsStateAndEmitsMatchingRequest)
{
    auto hud = makeHud();

    int pauses  = 0;
    int resumes = 0;
    QObject::connect(hud.get(), &RecordingHud::pauseRequested, hud.get(), [&pauses] { ++pauses; });
    QObject::connect(hud.get(), &RecordingHud::resumeRequested, hud.get(), [&resumes] { ++resumes; });

    hud->applyState(RecordingState::Recording, 0, 1);
    auto *button = buttonOf(*hud, QStringLiteral("暂停"));
    ASSERT_NE(button, nullptr) << "控制条上该有一枚暂停/继续键";
    EXPECT_EQ(button->accessibleName(), QStringLiteral("暂停"));

    clickOn(button);
    EXPECT_EQ(pauses, 1);
    EXPECT_EQ(resumes, 0);

    hud->applyState(RecordingState::Paused, 0, 1);
    EXPECT_EQ(button->accessibleName(), QStringLiteral("继续"));
    clickOn(button);
    EXPECT_EQ(resumes, 1) << "暂停中按下去该恢复录制";
    EXPECT_EQ(pauses, 1) << "恢复不该被当成再一次暂停";
}

/**
 * @brief 钉住暂停键只在会话认这一步的状态里可按
 * @details 会话在倒计时里收到暂停请求是什么都不做的，留着可按就是把「按了没反应」摆上控制条。
 *          两态都断：只断不可按的那一半，一句常驻的 setEnabled(false) 也能骗过去。
 */
TEST_F(RecordingHudFixture, PauseButtonIsOnlyEnabledWhenTheSessionAcceptsIt)
{
    auto hud = makeHud();

    hud->applyState(RecordingState::Countdown, 3, 0);
    auto *button = buttonOf(*hud, QStringLiteral("暂停"));
    ASSERT_NE(button, nullptr) << "控制条上该有一枚暂停/继续键";
    EXPECT_FALSE(button->isEnabled()) << "倒计时里这一枚还可按：按下去会话什么都不做，用户却以为已经停了";

    hud->applyState(RecordingState::Recording, 0, 1);
    EXPECT_TRUE(button->isEnabled()) << "开始录制后暂停键没跟着恢复可按";

    hud->applyState(RecordingState::Paused, 0, 1);
    EXPECT_TRUE(button->isEnabled()) << "暂停中的「继续」也得可按，否则这一轮再也恢复不了";

    hud->applyState(RecordingState::Done, 0, 5);
    EXPECT_FALSE(button->isEnabled()) << "已经收尾的这一轮还留着可按的暂停键";
}

/**
 * @brief 钉住「取消」那一枚键发的是取消请求，而不是停止并保存
 * @details 两颗键挨在同一个条上：按错的下场完全不同——停止会编出一份成品，取消什么也不留。
 *          因此这里既断「发了 cancelRequested」，也断「没有顺带发 stopRequested」。
 */
TEST_F(RecordingHudFixture, CancelButtonRequestsCancelAndNotSave)
{
    auto hud = makeHud();

    int cancels = 0;
    int stops   = 0;
    QObject::connect(hud.get(), &RecordingHud::cancelRequested, hud.get(), [&cancels] { ++cancels; });
    QObject::connect(hud.get(), &RecordingHud::stopRequested, hud.get(), [&stops] { ++stops; });

    hud->applyState(RecordingState::Recording, 0, 5);
    auto *button = buttonOf(*hud, QStringLiteral("取消录屏"));
    ASSERT_NE(button, nullptr) << "控制条上该有一枚取消键：录坏了的那一段得能整段扔掉";

    clickOn(button);
    EXPECT_EQ(cancels, 1) << "按下去没发出取消请求";
    EXPECT_EQ(stops, 0) << "取消走成了停止并保存，用户会多一份根本不想要的成品";
}
