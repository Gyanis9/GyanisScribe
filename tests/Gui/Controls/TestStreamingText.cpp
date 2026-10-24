// 流式覆盖层的判据：时间轴逐字错峰，覆盖层每帧真的重绘、模糊层保持语义色、收工后让开原生标签。
// QApplication 由 TestControlsChrome 的全局 Environment 提供，本文件不自建。
#include "Gui/Controls/StreamingText.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QFont>
#include <QImage>
#include <QWidget>

#include <cmath>

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
 * @brief 钉住覆盖层真的画字、每帧发出真实绘制、结束时隐藏自己
 * @details 计数装在 paintEvent 上：Qt 6 会把子控件的重绘请求合并到顶层窗口，控件本身收不到
 *          UpdateRequest；数真实绘制才能保证摘掉 update() 时用例变红，而不是靠 grab 掩盖。
 */
TEST(StreamingTextOverlay, PaintsVisibleTextAndRepaintsRealFrames)
{
    QWidget host;
    host.setStyleSheet(QStringLiteral("background-color: #FFFFFF;"));
    host.resize(240, 60);
    auto *overlay = new StreamingTextOverlay{&host, QStringLiteral("接好了"), QFont{}, QColor(Qt::black), QColor(Qt::white)};
    overlay->setGeometry(host.rect());
    host.show();
    // 显示与几何落定会留下一串重绘请求；多走几轮事件队列，把基线拖到「初始帧已经画过」之后
    for (int round = 0; round < 5; ++round)
    {
        QCoreApplication::processEvents();
    }

    const int paintsBefore = overlay->paintCount();
    ASSERT_GT(paintsBefore, 0) << "初始帧没有画出来，重绘判据没有可信的基线";
    overlay->advanceForTesting(16);
    QCoreApplication::processEvents();
    EXPECT_GT(overlay->paintCount(), paintsBefore) << "动画帧没有触发真实绘制；这条不靠 grab，update() 被摘掉就该变红";

    // 先取「最后一帧」的像素，再推进 1ms 让它收工；finish 会隐藏覆盖层，之后 grab 只剩父窗口
    overlay->advanceForTesting(overlay->totalDurationMs() - 16);
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

/**
 * @brief 钉住中途帧的模糊层仍是语义色的同族色
 * @details 半透明的模糊层只该把语义色与气泡底色相混：任一像素在 R、G 通道上的跌幅之比都恒定。
 *          把透明像素当成直通道黑来平均，边缘就会朝灰黑偏，这个比值随之跑掉。
 */
TEST(StreamingTextOverlay, BlurredPhaseKeepsTheSemanticColorFamily)
{
    QWidget host;
    host.setStyleSheet(QStringLiteral("background-color: #FFFFFF;"));
    host.resize(280, 80);
    QFont font;
    font.setPixelSize(28);
    const QColor tint{59, 125, 33};
    auto *overlay = new StreamingTextOverlay{&host, QStringLiteral("接住了"), font, tint, QColor(Qt::white)};
    overlay->setGeometry(host.rect());
    host.show();
    QCoreApplication::processEvents();

    // 首字刚走到半程：模糊层仍占大头，正是直通道模糊会把颜色拖黑的位置
    overlay->advanceForTesting(20);
    QCoreApplication::processEvents();
    const auto image = host.grab().toImage().convertToFormat(QImage::Format_ARGB32);

    const double expectedGreenRatio = static_cast<double>(255 - tint.green()) / static_cast<double>(255 - tint.red());
    int          sampled            = 0;
    int          offFamily          = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            const double redDrop = 255.0 - qRed(line[x]);
            if (redDrop < 40.0)
            {
                // 底色附近与整数舍入的噪声区不参与判据
                continue;
            }
            ++sampled;
            const double ratio = (255.0 - qGreen(line[x])) / redDrop;
            if (std::abs(ratio - expectedGreenRatio) > 0.08)
            {
                ++offFamily;
            }
        }
    }
    EXPECT_GT(sampled, 100) << "中途帧没有足够多的半透明字像素，判据没覆盖到模糊层";
    EXPECT_EQ(offFamily, 0) << "模糊层混进了透明黑：像素偏离语义色与底色之间的混色线";
}

/**
 * @brief 钉住清晰层逐字走完后的颜色与语义色逐位一致
 * @details 模糊修正不能顺手把清晰层调暗或调灰：进度为 1 时只画清晰层，字心必须就是传入的语义色。
 */
TEST(StreamingTextOverlay, SharpPhaseKeepsTheExactSemanticColor)
{
    QWidget host;
    host.setStyleSheet(QStringLiteral("background-color: #FFFFFF;"));
    host.resize(240, 80);
    QFont font;
    font.setPixelSize(40);
    const QColor tint{59, 125, 33};
    auto *overlay = new StreamingTextOverlay{&host, QStringLiteral("接"), font, tint, QColor(Qt::white)};
    overlay->setGeometry(host.rect());
    host.show();
    QCoreApplication::processEvents();

    overlay->advanceForTesting(overlay->totalDurationMs());
    QCoreApplication::processEvents();
    const auto image = host.grab().toImage().convertToFormat(QImage::Format_ARGB32);

    int exact = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            if (qRed(line[x]) == tint.red() && qGreen(line[x]) == tint.green() && qBlue(line[x]) == tint.blue())
            {
                ++exact;
            }
        }
    }
    EXPECT_GT(exact, 0) << "清晰层的字心不等于语义色：模糊修正污染了清晰层";
}
