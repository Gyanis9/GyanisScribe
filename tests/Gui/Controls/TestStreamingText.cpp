// 流式覆盖层的判据：时间轴逐字错峰，覆盖层自己请求重绘、画得出字、收工后让开原生标签。
// QApplication 由 TestControlsChrome 的全局 Environment 提供，本文件不自建。
#include "Gui/Controls/StreamingText.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QImage>
#include <QWidget>

using GyanisScribe::Gui::StreamingTextOverlay;
using GyanisScribe::Gui::StreamingTimeline;

/**
 * @brief 钉住每个字晚 30ms 起步、单字 90ms 走完
 * @details 总时长是 90 + 30×(字数−1)；第 i 个字从 i×30 开始，到 i×30+90 时进度为 1。
 */
TEST(StreamingTimeline, StaggersCharactersByThirtyMilliseconds)
{
    const StreamingTimeline timeline{3};
    EXPECT_EQ(timeline.totalDurationMs(), 150);
    EXPECT_NEAR(timeline.progressFor(0, 0), 0.0, 1e-6);
    EXPECT_NEAR(timeline.progressFor(0, 90), 1.0, 1e-6);
    EXPECT_NEAR(timeline.progressFor(1, 30), 0.0, 1e-6);
    EXPECT_NEAR(timeline.progressFor(1, 120), 1.0, 1e-6);
    EXPECT_NEAR(timeline.progressFor(2, 60), 0.0, 1e-6);
    EXPECT_NEAR(timeline.progressFor(2, 150), 1.0, 1e-6);
}

/**
 * @brief 钉住覆盖层真的画字、每帧请求重绘、结束时隐藏自己
 * @details 只看 grab 的像素会漏掉「画面只在收尾时被强制重绘」这种缺陷，
 *          所以先数 UpdateRequest，再取最后一帧的像素，最后确认收工后不再盖住原生标签。
 */
TEST(StreamingTextOverlay, PaintsVisibleTextAndRequestsRepaints)
{
    QWidget host;
    host.setStyleSheet(QStringLiteral("background-color: #FFFFFF;"));
    host.resize(240, 60);
    auto *overlay = new StreamingTextOverlay{&host, QStringLiteral("接好了"), QFont{}, QColor(Qt::black), QColor(Qt::white)};
    overlay->setGeometry(host.rect());
    host.show();

    struct UpdateCounter final : QObject
    {
        int  count = 0;
        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::UpdateRequest)
            {
                ++count;
            }
            return false;
        }
    } counter;
    overlay->installEventFilter(&counter);

    overlay->advanceForTesting(16);
    QCoreApplication::processEvents();
    EXPECT_GT(counter.count, 0) << "动画帧没有请求重绘，grab 会掩盖这个缺陷";

    // 先取「最后一帧」的像素，再推进 1ms 让它收工；finish 会隐藏覆盖层，之后 grab 只剩父窗口
    overlay->advanceForTesting(overlay->totalDurationMs() - 1);
    QCoreApplication::processEvents();
    const auto shot = host.grab().toImage();
    int        dark = 0;
    for (int y = 0; y < shot.height(); ++y)
    {
        for (int x = 0; x < shot.width(); ++x)
        {
            if (shot.pixelColor(x, y).lightness() < 100)
            {
                ++dark;
            }
        }
    }
    EXPECT_GT(dark, 10) << "时间轴走完没有画出文字";

    overlay->advanceForTesting(1);
    QCoreApplication::processEvents();
    EXPECT_TRUE(overlay->isFinished());
    EXPECT_FALSE(overlay->isVisible()) << "动画结束仍盖着原生 QLabel";
}
